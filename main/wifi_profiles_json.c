#include "wifi_profiles_json.h"
#include "cJSON.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

esp_err_t wifi_profiles_encode(const wifi_profiles_t *profiles, char **out)
{
    if (!out || !wifi_profiles_valid(profiles)) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");
    bool ok = networks && cJSON_AddNumberToObject(root, "version", 1);
    for (uint32_t i = 0; ok && i < profiles->count; ++i) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry) { ok = false; break; }
        cJSON_AddItemToArray(networks, entry);
        ok = cJSON_AddStringToObject(entry, "ssid", profiles->items[i].ssid) &&
             cJSON_AddStringToObject(entry, "password", profiles->items[i].password);
    }
    char *text = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!text) return ESP_ERR_NO_MEM;
    *out = text;
    return ESP_OK;
}

esp_err_t wifi_profiles_save_file(const char *path, const wifi_profiles_t *profiles)
{
    if (!path || !wifi_profiles_valid(profiles)) return ESP_ERR_INVALID_ARG;
    char temporary[256], backup[256];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary) ||
        snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup))
        return ESP_ERR_INVALID_ARG;
    char *text = NULL;
    esp_err_t encoded = wifi_profiles_encode(profiles, &text);
    if (encoded != ESP_OK) return encoded;
    bool ok;
    FILE *file = fopen(temporary, "wb");
    if (!file) { free(text); return ESP_FAIL; }
    size_t size = strlen(text);
    ok = fwrite(text, 1, size, file) == size;
    free(text);
    if (fflush(file) || fsync(fileno(file))) ok = false;
    if (fclose(file)) ok = false;
    if (!ok) { unlink(temporary); return ESP_FAIL; }
    /* FAT rename cannot replace an existing destination. Keep the old complete
     * file as .bak until the new generation is published. Boot ignores .tmp. */
    if (access(path, F_OK) == 0) {
        if (unlink(backup) && errno != ENOENT) { unlink(temporary); return ESP_FAIL; }
        if (rename(path, backup)) { unlink(temporary); return ESP_FAIL; }
    }
    if (rename(temporary, path)) { unlink(temporary); return ESP_FAIL; }
    /* Leaving .bak after a failed unlink is harmless; valid main takes priority. */
    (void)unlink(backup);
    return ESP_OK;
}
static esp_err_t read_legacy(const char *path, wifi_profiles_t *out)
{
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    char text[101];
    size_t size = fread(text, 1, sizeof(text), file);
    bool failed = ferror(file);
    if (fclose(file)) failed = true;
    if (failed) return ESP_FAIL;
    if (size >= sizeof(text) || memchr(text, 0, size)) return ESP_ERR_INVALID_ARG;
    text[size] = 0;
    char *password = strchr(text, '\n');
    if (!password) return ESP_ERR_INVALID_ARG;
    *password++ = 0;
    char *end = strchr(password, '\n');
    if (end) { if (end[1]) return ESP_ERR_INVALID_ARG; *end = 0; }
    size_t ssid_size = strlen(text), password_size = strlen(password);
    if (ssid_size && text[ssid_size - 1] == '\r') text[--ssid_size] = 0;
    if (password_size && password[password_size - 1] == '\r') password[--password_size] = 0;
    if (ssid_size > 32 || password_size > 64) return ESP_ERR_INVALID_ARG;
    wifi_profiles_t parsed = {.version = 1, .count = 1};
    memcpy(parsed.items[0].ssid, text, ssid_size + 1);
    memcpy(parsed.items[0].password, password, password_size + 1);
    if (!wifi_profiles_valid(&parsed)) return ESP_ERR_INVALID_ARG;
    *out = parsed;
    return ESP_OK;
}
esp_err_t wifi_profiles_migrate(const char *mount, const wifi_profiles_t *fallback,
                               wifi_profiles_t *out)
{
    if (!mount || !out) return ESP_ERR_INVALID_ARG;
    char path[256], directory[256], legacy[256];
    if (snprintf(path, sizeof(path), "%s/config/wifi.json", mount) >= (int)sizeof(path) ||
        snprintf(directory, sizeof(directory), "%s/config", mount) >= (int)sizeof(directory) ||
        snprintf(legacy, sizeof(legacy), "%s/wifi.txt", mount) >= (int)sizeof(legacy))
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = wifi_profiles_read_file(path, out);
    if (e != ESP_ERR_NOT_FOUND) return e;
    if (fallback && !wifi_profiles_valid(fallback)) return ESP_ERR_INVALID_ARG;
    wifi_profiles_t migrated;
    if (fallback) migrated = *fallback;
    else {
        e = read_legacy(legacy, &migrated);
        if (e != ESP_OK) return e;
    }
    if (mkdir(directory, 0700) && errno != EEXIST) return ESP_FAIL;
    e = wifi_profiles_save_file(path, &migrated);
    if (e == ESP_OK) *out = migrated;
    return e;
}
