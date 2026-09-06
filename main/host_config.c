#include "host_config.h"
#include "wifi_sd.h"

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

    /* SD presence is checked even on a device with no NVS namespace yet. */
    wifi_profiles_t sd = {0};
    esp_err_t sd_error = wifi_sd_load(&sd);
    if (sd_error != ESP_OK && sd_error != ESP_ERR_NOT_FOUND) return sd_error;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("photo", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        config->wifi = sd_error == ESP_OK ? sd : (wifi_profiles_t){.version = 1};
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    if (sd_error == ESP_OK) {
        config->wifi = sd;
    } else {
        size_t size = sizeof(config->wifi);
        err = nvs_get_blob(handle, "wifi_profiles", &config->wifi, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            /* Only absence permits legacy fallback. Empty/corrupt lists are authoritative. */
            memset(&config->wifi, 0, sizeof(config->wifi));
            config->wifi.version = 1;
            err = read_optional_string(handle, "wifi_ssid", config->wifi.items[0].ssid,
                                       sizeof(config->wifi.items[0].ssid));
            if (err == ESP_OK)
                err = read_optional_string(handle, "wifi_pass", config->wifi.items[0].password,
                                           sizeof(config->wifi.items[0].password));
            config->wifi.count = config->wifi.items[0].ssid[0] ? 1 : 0;
        } else if (err == ESP_OK && size != sizeof(config->wifi)) {
            err = ESP_ERR_INVALID_SIZE;
        }
    }
    if (err == ESP_OK && !wifi_profiles_valid(&config->wifi))
        err = ESP_ERR_INVALID_ARG;
    if (err == ESP_OK) {
        err = read_optional_string(handle, "push_token", config->push_token,
                                   sizeof(config->push_token));
    }
    nvs_close(handle);
    return err;
}
