#include "push_server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
    uint32_t difference = (uint32_t)(candidate_length ^
                                     s_push_token_length);
    /* Always inspect the maximum token width.  Only the public request length
     * affects the branch below; the configured token length does not affect
     * the loop count. */
    for (size_t index = 0; index < PUSH_TOKEN_MAX; ++index) {
        unsigned char byte = index < candidate_length
                                 ? (unsigned char)candidate[index]
                                 : 0U;
        difference |= byte ^ (unsigned char)s_push_token[index];
    }
    return difference == 0U;
}

static bool token_is_valid(const char *token, size_t token_length)
{
    if (token_length < 32U || token_length > PUSH_TOKEN_MAX) {
        return false;
    }
    for (size_t index = 0; index < token_length; ++index) {
        unsigned char byte = (unsigned char)token[index];
        if (byte < 0x21U || byte > 0x7eU) {
            return false;
        }
    }
    return true;
}

static bool request_is_authorized(httpd_req_t *request)
{
    size_t header_length = httpd_req_get_hdr_value_len(request,
                                                        "Authorization");
    if (header_length < AUTH_SCHEME_LENGTH ||
        header_length > AUTH_SCHEME_LENGTH + PUSH_TOKEN_MAX) {
        return false;
    }

    char header[AUTH_SCHEME_LENGTH + PUSH_TOKEN_MAX + 1];
    if (httpd_req_get_hdr_value_str(request, "Authorization", header,
                                    sizeof(header)) != ESP_OK) {
        return false;
    }
    if (memcmp(header, AUTH_SCHEME, AUTH_SCHEME_LENGTH) != 0) {
        return false;
    }
    return token_matches(header + AUTH_SCHEME_LENGTH,
                         header_length - AUTH_SCHEME_LENGTH);
}

static bool request_is_raw_png(httpd_req_t *request)
{
    size_t length = httpd_req_get_hdr_value_len(request, "Content-Type");
    static const char expected[] = "image/png";
    if (length != sizeof(expected) - 1) {
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
    return size >= sizeof(signature) &&
           memcmp(data, signature, sizeof(signature)) == 0;
}

static esp_err_t receive_body(httpd_req_t *request, uint8_t *destination,
                              size_t size)
{
    size_t offset = 0;
    int64_t deadline = esp_timer_get_time() + RECEIVE_DEADLINE_US;
    while (offset < size) {
        if (esp_timer_get_time() >= deadline) {
            return ESP_ERR_TIMEOUT;
        }
        int received = httpd_req_recv(request, (char *)destination + offset,
                                      size - offset);
        if (received > 0) {
            offset += (size_t)received;
            continue;
        }
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t push_handler(httpd_req_t *request)
{
    if (s_push_token_length == 0) {
        return send_text(request, "503 Service Unavailable",
                         "push token is not configured\n");
    }
    if (!request_is_authorized(request)) {
        return send_text(request, "401 Unauthorized", "unauthorized\n");
    }
    if (request->content_len > MAX_PNG_BYTES) {
        return send_text(request, "413 Content Too Large",
                         "PNG exceeds 5 MiB\n");
    }
    if (request->content_len == 0) {
        return send_text(request, "400 Bad Request", "empty PNG body\n");
    }
    if (!request_is_raw_png(request)) {
        return send_text(request, "415 Unsupported Media Type",
                         "Content-Type must be image/png\n");
    }
    if (!photoframe_plugin_is_ready()) {
        return send_text(request, "503 Service Unavailable",
                         "photoframe payload is unavailable\n");
    }
    if (xSemaphoreTake(s_display_lock, 0) != pdTRUE) {
        return send_text(request, "409 Conflict", "display is busy\n");
    }

    size_t size = (size_t)request->content_len;
    uint8_t *png = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (png == NULL) {
        xSemaphoreGive(s_display_lock);
        return send_text(request, "503 Service Unavailable",
                         "insufficient image memory\n");
    }

    esp_err_t response;
    if (receive_body(request, png, size) != ESP_OK) {
        response = send_text(request, "408 Request Timeout",
                             "incomplete PNG body\n");
    } else if (!has_png_signature(png, size)) {
        response = send_text(request, "422 Unprocessable Content",
                             "invalid PNG\n");
    } else {
        int result = photoframe_plugin_render(png, size);
        switch (result) {
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
            response = send_text(request, "504 Gateway Timeout",
                                 "display busy timeout\n");
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

esp_err_t photopainter_push_server_start(const char *push_token)
{
    if (push_token == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_display_lock != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t token_length = strnlen(push_token, sizeof(s_push_token));
    if (token_length >= sizeof(s_push_token)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (token_length > 0 && !token_is_valid(push_token, token_length)) {
        ESP_LOGE(TAG, "configured push token is invalid; "
                      "treating it as unconfigured");
        token_length = 0;
    }
    memset(s_push_token, 0, sizeof(s_push_token));
    if (token_length > 0) {
        memcpy(s_push_token, push_token, token_length);
    }
    s_push_token_length = token_length;

    s_display_lock = xSemaphoreCreateMutex();
    if (s_display_lock == NULL) {
        memset(s_push_token, 0, sizeof(s_push_token));
        s_push_token_length = 0;
        return ESP_ERR_NO_MEM;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 16384;
    config.max_uri_handlers = 1;
    config.max_open_sockets = 3;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 60;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
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
    if (err != ESP_OK) {
        (void)httpd_stop(server);
        vSemaphoreDelete(s_display_lock);
        s_display_lock = NULL;
        memset(s_push_token, 0, sizeof(s_push_token));
        s_push_token_length = 0;
        ESP_LOGE(TAG, "register push endpoint failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "POST /api/push ready");
    return ESP_OK;
}
