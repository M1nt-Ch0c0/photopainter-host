#include "wifi_profiles_json.h"
#include "cJSON.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bound recursion before calling the general JSON parser on a small task stack. */
static bool bounded(const char *text, size_t size)
{
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < size; ++i) {
        char c = text[i];
        if (!c) return false;
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') {
                /* cJSON strings cannot represent embedded NUL; reject its escape. */
                if (size - i >= 6 && !memcmp(text + i, "\\u0000", 6)) return false;
                escaped = true;
            } else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 4) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    return !depth && !quoted;
}
static bool unique_keys(const cJSON *o)
{
    if (!cJSON_IsObject(o)) return false;
    for (const cJSON *a = o->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) return false;
    return true;
}
esp_err_t wifi_profiles_parse(const char *text, size_t size, wifi_profiles_t *out)
{
    if (!text || !out || size > 8192 || !bounded(text, size)) return ESP_ERR_INVALID_ARG;
    char *copy = malloc(size + 1);
    if (!copy) return ESP_ERR_NO_MEM;
    memcpy(copy, text, size); copy[size] = 0;
    cJSON *root = cJSON_ParseWithOpts(copy, NULL, true);
    free(copy);
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *networks = cJSON_GetObjectItemCaseSensitive(root, "networks");
    bool ok = unique_keys(root) && cJSON_IsNumber(version) && version->valuedouble == 1 &&
              cJSON_IsArray(networks) && cJSON_GetArraySize(networks) <= WIFI_PROFILE_LIMIT;
    wifi_profiles_t parsed = {.version = 1};
    if (ok) {
        const cJSON *n;
        cJSON_ArrayForEach(n, networks) {
            const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(n, "ssid");
            const cJSON *password = cJSON_GetObjectItemCaseSensitive(n, "password");
            if (!unique_keys(n) || !cJSON_IsString(ssid) || !cJSON_IsString(password) ||
                strlen(ssid->valuestring) > 32 || strlen(password->valuestring) > 64) { ok = false; break; }
            wifi_profile_t *p = &parsed.items[parsed.count++];
            strcpy(p->ssid, ssid->valuestring);
            strcpy(p->password, password->valuestring);
        }
    }
    cJSON_Delete(root);
    if (!ok || !wifi_profiles_valid(&parsed)) return ESP_ERR_INVALID_ARG;
    *out = parsed;
    return ESP_OK;
}
esp_err_t wifi_profiles_read_file(const char *path, wifi_profiles_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f && errno == ENOENT) {
        char backup[256];
        if (snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) return ESP_ERR_INVALID_ARG;
        f = fopen(backup, "rb");
    }
    if (!f) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    char *text = malloc(8193);
    if (!text) { fclose(f); return ESP_ERR_NO_MEM; }
    size_t n = fread(text, 1, 8193, f);
    bool failed = ferror(f);
    if (fclose(f)) failed = true;
    esp_err_t e = failed ? ESP_FAIL : wifi_profiles_parse(text, n, out);
    free(text);
    return e;
}
