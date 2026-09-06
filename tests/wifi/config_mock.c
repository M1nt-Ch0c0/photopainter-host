#include "host_config.h"
#include "wifi_sd.h"
#include "nvs.h"
#include <assert.h>
#include <string.h>
static int mode;
void mock_config_mode(int value) { mode = value; }
esp_err_t nvs_open(const char *name, int flags, nvs_handle_t *handle)
{
    (void)name; (void)flags; *handle = 1;
    return mode == 0 || mode == 9 ? ESP_ERR_NVS_NOT_FOUND : mode == 11 ? ESP_FAIL : ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size)
{
    (void)h; assert(!strcmp(key, "wifi_profiles"));
    if (mode != 2 && mode != 3 && mode != 4 && mode != 5 && mode != 12) return ESP_ERR_NVS_NOT_FOUND;
    wifi_profiles_t profiles = {.version = mode == 4 || mode == 5 ? 99 : 1, .count = mode == 3 ? 0 : 2};
    strcpy(profiles.items[0].ssid, "first"); strcpy(profiles.items[1].ssid, "second");
    assert(*size >= sizeof(profiles)); memcpy(out, &profiles, sizeof(profiles));
    *size = mode == 12 ? 500 : sizeof(profiles); return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *size)
{
    (void)h;
    if (mode == 8 && !strcmp(key,"push_token")) return ESP_ERR_INVALID_SIZE;
    const char *value = !strcmp(key,"push_token") ? "tttttttttttttttttttttttttttttttt" : !strcmp(key,"wifi_ssid") ? "legacy" : "password";
    assert(*size > strlen(value)); strcpy(out,value); *size=strlen(value)+1; return ESP_OK;
}
esp_err_t wifi_sd_load(const wifi_profiles_t *fallback, bool migrate, wifi_profiles_t *out)
{
    if (mode == 4 || mode == 5 || mode == 11 || mode == 12) assert(!fallback && !migrate);
    else assert(migrate);
    if (mode == 0 || mode == 9) assert(!fallback);
    if (mode == 10) { assert(fallback && fallback->count==1); *out=*fallback; return ESP_OK; }
    if (mode == 5 || mode == 6 || mode == 9) {
        memset(out,0,sizeof(*out)); out->version=1; out->count=mode==6 ? 0 : 1;
        if (out->count) strcpy(out->items[0].ssid,"from-sd");
        return ESP_OK;
    }
    return mode == 7 ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_FOUND;
}
