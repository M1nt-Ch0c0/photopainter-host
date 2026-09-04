#include "host_config.h"

#include <stddef.h>
#include <string.h>

#include "nvs.h"

static esp_err_t read_optional_string(nvs_handle_t handle, const char *key,
                                      char *destination, size_t capacity)
{
    size_t required = capacity;
    esp_err_t err = nvs_get_str(handle, key, destination, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        destination[0] = '\0';
        return ESP_OK;
    }
    if (err != ESP_OK) {
        destination[0] = '\0';
        return err;
    }
    if (required == 0 || required > capacity) {
        destination[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t photopainter_host_config_load(photopainter_host_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(config, 0, sizeof(*config));

    nvs_handle_t handle;
    esp_err_t err = nvs_open("photo", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = read_optional_string(handle, "wifi_ssid", config->wifi_ssid,
                               sizeof(config->wifi_ssid));
    if (err == ESP_OK) {
        err = read_optional_string(handle, "wifi_pass", config->wifi_password,
                                   sizeof(config->wifi_password));
    }
    if (err == ESP_OK) {
        err = read_optional_string(handle, "push_token", config->push_token,
                                   sizeof(config->push_token));
    }
    nvs_close(handle);
    return err;
}
