#include "push_server.h"

#include "app_catalog.h"
#include "wifi_sd.h"
#include "wifi_profiles_json.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "photoframe_plugin.h"

#define MAX_PNG_BYTES (5U * 1024U * 1024U)
#define RECEIVE_DEADLINE_US (30LL * 1000LL * 1000LL)
#define AUTH_SCHEME "Bearer "
#define AUTH_SCHEME_LENGTH 7U
#define PUSH_TOKEN_MAX 128U

static const char *TAG = "push";
static char s_push_token[PUSH_TOKEN_MAX + 1];
static size_t s_push_token_length;
static SemaphoreHandle_t s_display_lock;

static esp_err_t send_text(httpd_req_t *request, const char *status,
                           const char *message)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_sendstr(request, message);
}

static bool token_matches(const char *candidate, size_t candidate_length)
{
    uint32_t difference = (uint32_t)(candidate_length ^ s_push_token_length);
    /* Always inspect the maximum token width.  Only the public request length
     * affects the branch below; the configured token length does not affect
     * the loop count. */
    for (size_t index = 0; index < PUSH_TOKEN_MAX; ++index)
    {
        unsigned char byte =
            index < candidate_length ? (unsigned char)candidate[index] : 0U;
        difference |= byte ^ (unsigned char)s_push_token[index];
    }
    return difference == 0U;
}

static bool token_is_valid(const char *token, size_t token_length)
{
    if (token_length < 32U || token_length > PUSH_TOKEN_MAX)
    {
        return false;
    }
    for (size_t index = 0; index < token_length; ++index)
    {
        unsigned char byte = (unsigned char)token[index];
        if (byte < 0x21U || byte > 0x7eU)
        {
            return false;
        }
    }
    return true;
}

static bool request_is_authorized(httpd_req_t *request)
{
    size_t header_length = httpd_req_get_hdr_value_len(request, "Authorization");
    if (header_length < AUTH_SCHEME_LENGTH ||
        header_length > AUTH_SCHEME_LENGTH + PUSH_TOKEN_MAX)
    {
        return false;
    }

    char header[AUTH_SCHEME_LENGTH + PUSH_TOKEN_MAX + 1];
    if (httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header)) !=
        ESP_OK)
    {
        return false;
    }
    if (memcmp(header, AUTH_SCHEME, AUTH_SCHEME_LENGTH) != 0)
    {
        return false;
    }
    return token_matches(header + AUTH_SCHEME_LENGTH,
                         header_length - AUTH_SCHEME_LENGTH);
}

static bool request_is_raw_png(httpd_req_t *request)
{
    size_t length = httpd_req_get_hdr_value_len(request, "Content-Type");
    static const char expected[] = "image/png";
    if (length != sizeof(expected) - 1)
    {
        return false;
    }
    char content_type[sizeof(expected)];
    return httpd_req_get_hdr_value_str(request, "Content-Type", content_type,
                                       sizeof(content_type)) == ESP_OK &&
           strcasecmp(content_type, expected) == 0;
}

static bool has_png_signature(const uint8_t *data, size_t size)
{
    static const uint8_t signature[8] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
    };
    return size >= sizeof(signature) && memcmp(data, signature, sizeof(signature)) == 0;
}

static esp_err_t receive_body(httpd_req_t *request, uint8_t *destination, size_t size)
{
    size_t offset = 0;
    int64_t deadline = esp_timer_get_time() + RECEIVE_DEADLINE_US;
    while (offset < size)
    {
        if (esp_timer_get_time() >= deadline)
        {
            return ESP_ERR_TIMEOUT;
        }
        int received =
            httpd_req_recv(request, (char *)destination + offset, size - offset);
        if (received > 0)
        {
            offset += (size_t)received;
            continue;
        }
        if (received == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t push_handler(httpd_req_t *request)
{
    if (s_push_token_length == 0)
    {
        return send_text(request, "503 Service Unavailable",
                         "push token is not configured\n");
    }
    if (!request_is_authorized(request))
    {
        return send_text(request, "401 Unauthorized", "unauthorized\n");
    }
    if (request->content_len > MAX_PNG_BYTES)
    {
        return send_text(request, "413 Content Too Large", "PNG exceeds 5 MiB\n");
    }
    if (request->content_len == 0)
    {
        return send_text(request, "400 Bad Request", "empty PNG body\n");
    }
    if (!request_is_raw_png(request))
    {
        return send_text(request, "415 Unsupported Media Type",
                         "Content-Type must be image/png\n");
    }
    if (!photoframe_plugin_is_ready())
    {
        return send_text(request, "503 Service Unavailable",
                         "photoframe payload is unavailable\n");
    }
    if (xSemaphoreTake(s_display_lock, 0) != pdTRUE)
    {
        return send_text(request, "409 Conflict", "display is busy\n");
    }

    size_t app_length = httpd_req_get_hdr_value_len(request, "X-PhotoPainter-App");
    {
        /* Legacy pushers target photoframe; alternate apps require explicit ID. */
        char expected[APP_ID_BYTES] = "photoframe";
        const app_catalog_t *c = app_catalog_get();
        int running = photoframe_plugin_app();
        if (app_length >= sizeof(expected) ||
            (app_length && httpd_req_get_hdr_value_str(request, "X-PhotoPainter-App", expected, sizeof(expected)) != ESP_OK) ||
            !app_id_valid(expected) || !c || running < 0 || strcmp(expected, c->apps[running].id)) {
            xSemaphoreGive(s_display_lock);
            return send_text(request, "409 Conflict", "requested application is not running\n");
        }
    }

    size_t size = (size_t)request->content_len;
    uint8_t *png = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (png == NULL)
    {
        xSemaphoreGive(s_display_lock);
        return send_text(request, "503 Service Unavailable",
                         "insufficient image memory\n");
    }

    esp_err_t response;
    if (receive_body(request, png, size) != ESP_OK)
    {
        response = send_text(request, "408 Request Timeout", "incomplete PNG body\n");
    }
    else if (!has_png_signature(png, size))
    {
        response = send_text(request, "422 Unprocessable Content", "invalid PNG\n");
    }
    else
    {
        int result = photoframe_plugin_render(png, size);
        switch (result)
        {
        case PHOTOFRAME_RESULT_OK:
            response = send_text(request, "200 OK", "display refreshed\n");
            break;
        case PHOTOFRAME_RESULT_ARGUMENT:
        case PHOTOFRAME_RESULT_PNG:
        case PHOTOFRAME_RESULT_DIMENSIONS:
        case PHOTOFRAME_RESULT_COLOR:
            response = send_text(request, "422 Unprocessable Content",
                                 "PNG is not an 800x480 six-color frame\n");
            break;
        case PHOTOFRAME_RESULT_MEMORY:
            response = send_text(request, "503 Service Unavailable",
                                 "insufficient image memory\n");
            break;
        case PHOTOFRAME_RESULT_UNAVAILABLE:
            response = send_text(request, "503 Service Unavailable",
                                 "photoframe payload is unavailable\n");
            break;
        case PHOTOFRAME_RESULT_BUSY_TIMEOUT:
            response =
                send_text(request, "504 Gateway Timeout", "display busy timeout\n");
            break;
        default:
            ESP_LOGE(TAG, "photoframe payload failed: %d", result);
            response = send_text(request, "500 Internal Server Error",
                                 "display refresh failed\n");
            break;
        }
    }

    heap_caps_free(png);
    xSemaphoreGive(s_display_lock);
    return response;
}

static esp_err_t module_handler(httpd_req_t *request)
{
    if (!s_push_token_length)
        return send_text(request, "503 Service Unavailable", "token not configured\n");
    if (!request_is_authorized(request))
        return send_text(request, "401 Unauthorized", "unauthorized\n");
    if (xSemaphoreTake(s_display_lock, 0) != pdTRUE)
        return send_text(request, "409 Conflict", "busy\n");
    esp_err_t response;
    char query[128] = {0}, op[20] = {0}, app[APP_ID_BYTES] = "photoframe";
    size_t query_len = httpd_req_get_url_query_len(request);
    if (query_len >= sizeof(query) || (query_len && httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK)) {
        xSemaphoreGive(s_display_lock);
        return send_text(request, "400 Bad Request", "invalid query\n");
    }
    if (query_len) {
        char value[APP_ID_BYTES] = {0};
        esp_err_t e = httpd_query_key_value(query, "app", value, sizeof(value));
        if (e == ESP_OK) memcpy(app, value, sizeof(app));
        else if (e != ESP_ERR_NOT_FOUND) app[0] = 0;
        e = httpd_query_key_value(query, "op", op, sizeof(op));
        if (e != ESP_OK && e != ESP_ERR_NOT_FOUND) app[0] = 0;
    }
    if (!app_id_valid(app)) {
        xSemaphoreGive(s_display_lock);
        return send_text(request, "400 Bad Request", "invalid app id\n");
    }
    if (request->method == HTTP_GET)
    {
        module_state_t state = module_slots_state();
        const app_catalog_t *c = app_catalog_get();
        char json[4096];
        int used = snprintf(json, sizeof(json),
                 "{\"abi\":1,\"active\":%ld,\"previous\":%ld,\"pending\":%ld,\"trial\":"
                 "%ld,\"loaded\":%d,\"version\":%lu,\"ready\":%s,\"selected\":%d,\"trial_app\":%d,\"loaded_app\":%d,\"capacity\":%d,\"apps\":[",
                 (long)state.active, (long)state.previous, (long)state.pending,
                 (long)state.trial, photoframe_plugin_slot(),
                 (unsigned long)photoframe_plugin_version(),
                 photoframe_plugin_is_ready() ? "true" : "false",
                 c ? (int)c->selected : -1, c ? (int)c->trial_app : -1,
                 photoframe_plugin_app(), APP_LIMIT);
        bool comma = false;
        for (int i = 0; c && i < APP_LIMIT; ++i) {
            const app_entry_t *a = &c->apps[i];
            if (!a->id[0]) continue;
            char versions[2][16];
            for (int slot = 0; slot < 2; ++slot) {
                uint8_t *data = NULL;
                module_header_t h;
                if (app_catalog_read(i, slot, &data, &h) == ESP_OK)
                    snprintf(versions[slot], sizeof(versions[slot]), "%lu", (unsigned long)h.version);
                else strcpy(versions[slot], "null");
                free(data);
            }
            used += snprintf(json + used, sizeof(json) - used,
                "%s{\"id\":\"%s\",\"bank\":%d,\"slot_bytes\":%u,\"active\":%ld,\"previous\":%ld,\"pending\":%ld,\"trial\":%ld,\"versions\":[%s,%s]}",
                comma ? "," : "", a->id, i, MODULE_SLOT_BYTES,
                (long)a->slots.active, (long)a->slots.previous, (long)a->slots.pending, (long)a->slots.trial,
                versions[0], versions[1]);
            comma = true;
        }
        snprintf(json + used, sizeof(json) - used, "]}");
        httpd_resp_set_type(request, "application/json");
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        response = httpd_resp_sendstr(request, json);
    }
    else
    {
        if (!strcmp(op, "activate") || !strcmp(op, "switch") || !strcmp(op, "rollback") || !strcmp(op, "remove"))
        {
            esp_err_t e = ESP_ERR_INVALID_ARG;
            if (!request->content_len) {
                if (!strcmp(op, "remove")) e = app_catalog_remove(app_catalog_find(app));
                else if (!strcmp(op, "rollback")) e = photoframe_plugin_rollback_app(app);
                else e = photoframe_plugin_select(app, !strcmp(op, "activate"));
            }
            response = send_text(request, e == ESP_OK ? "200 OK" : "409 Conflict",
                e == ESP_OK ? "application operation completed\n" : "operation rejected or rolled back\n");
        }
        else if (op[0])
        {
            response = send_text(request, "400 Bad Request", "unknown operation\n");
        }
        else if (request->content_len > MODULE_SLOT_BYTES)
        {
            response =
                send_text(request, "413 Content Too Large", "module too large\n");
        }
        else if (request->content_len < MODULE_HEADER_BYTES + 52)
        {
            response =
                send_text(request, "400 Bad Request", "module package required\n");
        }
        else
        {
            char type[40] = {0};
            httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type));
            if (strcmp(type, "application/octet-stream") != 0)
            {
                response = send_text(request, "415 Unsupported Media Type",
                                     "module must be application/octet-stream\n");
            }
            else
            {
                size_t size = request->content_len;
                uint8_t *package =
                    heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!package)
                    response = send_text(request, "503 Service Unavailable",
                                         "insufficient memory\n");
                else
                {
                    esp_err_t e = receive_body(request, package, size);
                    if (e == ESP_OK)
                        e = app_catalog_stage(app, package, size);
                    free(package);
                    const char *status = e == ESP_OK            ? "202 Accepted"
                                         : e == ESP_ERR_TIMEOUT ? "408 Request Timeout"
                                         : e == ESP_ERR_INVALID_STATE
                                             ? "409 Conflict"
                                             : "422 Unprocessable Content";
                    response = send_text(request, status,
                                         e == ESP_OK ? "staged; activate explicitly\n"
                                                     : "module rejected\n");
                }
            }
        }
    }
    xSemaphoreGive(s_display_lock);
    return response;
}

static esp_err_t wifi_handler(httpd_req_t *request)
{
    /* Do not mount/read SD or receive credentials before authentication. */
    if (!s_push_token_length)
        return send_text(request, "503 Service Unavailable", "token not configured\n");
    if (!request_is_authorized(request))
        return send_text(request, "401 Unauthorized", "unauthorized\n");
    if (request->content_len > 8192)
        return send_text(request, "413 Content Too Large", "Wi-Fi JSON exceeds 8192 bytes\n");
    if (xSemaphoreTake(s_display_lock, 0) != pdTRUE)
        return send_text(request, "409 Conflict", "busy\n");
    wifi_profiles_t profiles;
    esp_err_t response;
    if (request->method == HTTP_GET) {
        esp_err_t e = wifi_sd_load(NULL, false, &profiles);
        char *json = NULL;
        if (e == ESP_OK) e = wifi_profiles_encode(&profiles, &json);
        if (e == ESP_OK) {
            httpd_resp_set_type(request, "application/json");
            httpd_resp_set_hdr(request, "Cache-Control", "no-store");
            httpd_resp_set_hdr(request, "Connection", "close");
            response = httpd_resp_sendstr(request, json);
        } else {
            response = send_text(request, e == ESP_ERR_NOT_FOUND ? "404 Not Found" :
                e == ESP_ERR_INVALID_ARG ? "409 Conflict" : "503 Service Unavailable",
                "SD Wi-Fi configuration unavailable; no changes made\n");
        }
        free(json);
    } else {
        char type[40] = {0};
        httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type));
        if (strcasecmp(type, "application/json")) {
            response = send_text(request, "415 Unsupported Media Type", "application/json required\n");
        } else {
            uint8_t *body = malloc(request->content_len + 1);
            esp_err_t e = body ? receive_body(request, body, request->content_len) : ESP_ERR_NO_MEM;
            if (e == ESP_OK) e = wifi_profiles_parse((char *)body, request->content_len, &profiles);
            free(body);
            if (e == ESP_OK) e = wifi_sd_save(&profiles);
            const char *status = e == ESP_OK ? "200 OK" :
                e == ESP_ERR_TIMEOUT ? "408 Request Timeout" :
                e == ESP_ERR_INVALID_ARG ? "422 Unprocessable Content" :
                e == ESP_ERR_INVALID_STATE ? "409 Conflict" : "503 Service Unavailable";
            response = send_text(request, status, e == ESP_OK ?
                "SD Wi-Fi saved; effective on next reboot\n" : "SD Wi-Fi update failed\n");
        }
    }
    xSemaphoreGive(s_display_lock);
    return response;
}

esp_err_t photopainter_push_server_start(const char *push_token)
{
    if (push_token == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_display_lock != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    size_t token_length = strnlen(push_token, sizeof(s_push_token));
    if (token_length >= sizeof(s_push_token))
    {
        return ESP_ERR_INVALID_SIZE;
    }
    if (token_length > 0 && !token_is_valid(push_token, token_length))
    {
        ESP_LOGE(TAG, "configured push token is invalid; "
                      "treating it as unconfigured");
        token_length = 0;
    }
    memset(s_push_token, 0, sizeof(s_push_token));
    if (token_length > 0)
    {
        memcpy(s_push_token, push_token, token_length);
    }
    s_push_token_length = token_length;

    s_display_lock = xSemaphoreCreateMutex();
    if (s_display_lock == NULL)
    {
        memset(s_push_token, 0, sizeof(s_push_token));
        s_push_token_length = 0;
        return ESP_ERR_NO_MEM;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 16384;
    config.max_uri_handlers = 5;
    config.max_open_sockets = 3;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 60;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        vSemaphoreDelete(s_display_lock);
        s_display_lock = NULL;
        memset(s_push_token, 0, sizeof(s_push_token));
        s_push_token_length = 0;
        ESP_LOGE(TAG, "start HTTP server failed: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t push = {
        .uri = "/api/push",
        .method = HTTP_POST,
        .handler = push_handler,
        .user_ctx = NULL,
    };
    err = httpd_register_uri_handler(server, &push);
    if (err != ESP_OK)
    {
        (void)httpd_stop(server);
        vSemaphoreDelete(s_display_lock);
        s_display_lock = NULL;
        memset(s_push_token, 0, sizeof(s_push_token));
        s_push_token_length = 0;
        ESP_LOGE(TAG, "register push endpoint failed: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t module_get = {
        .uri = "/api/module", .method = HTTP_GET, .handler = module_handler};
    const httpd_uri_t module_post = {
        .uri = "/api/module", .method = HTTP_POST, .handler = module_handler};
    err = httpd_register_uri_handler(server, &module_get);
    if (err == ESP_OK)
        err = httpd_register_uri_handler(server, &module_post);
    if (err != ESP_OK)
    {
        httpd_stop(server);
        return err;
    }
    const httpd_uri_t wifi_get = {
        .uri = "/api/wifi", .method = HTTP_GET, .handler = wifi_handler};
    const httpd_uri_t wifi_post = {
        .uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_handler};
    err = httpd_register_uri_handler(server, &wifi_get);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &wifi_post);
    if (err != ESP_OK) { httpd_stop(server); return err; }
    ESP_LOGI(TAG, "POST /api/push ready");
    return ESP_OK;
}
