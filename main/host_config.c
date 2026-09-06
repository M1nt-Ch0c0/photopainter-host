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

static esp_err_t read_networks(nvs_handle_t handle, wifi_profiles_t *wifi)
{
    size_t size = sizeof(*wifi);
    esp_err_t err = nvs_get_blob(handle, "wifi_profiles", wifi, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(wifi, 0, sizeof(*wifi));
        wifi->version = 1;
        err = read_optional_string(handle, "wifi_ssid", wifi->items[0].ssid,
                                   sizeof(wifi->items[0].ssid));
        if (err == ESP_OK)
            err = read_optional_string(handle, "wifi_pass", wifi->items[0].password,
                                       sizeof(wifi->items[0].password));
        if (err == ESP_OK && !wifi->items[0].ssid[0]) return ESP_ERR_NOT_FOUND;
        wifi->count = 1;
    } else if (err == ESP_OK && size != sizeof(*wifi)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK && !wifi_profiles_valid(wifi)) return ESP_ERR_INVALID_ARG;
    return err;
}
esp_err_t photopainter_host_config_load(photopainter_host_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    config->wifi.version = 1;
    wifi_profiles_t fallback = {0};
    esp_err_t network_error = ESP_ERR_NOT_FOUND;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("photo", NVS_READONLY, &handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (err == ESP_OK) {
        err = read_optional_string(handle, "push_token", config->push_token,
                                   sizeof(config->push_token));
        if (err == ESP_OK) network_error = read_networks(handle, &fallback);
        nvs_close(handle);
        if (err != ESP_OK) return err;
    }
    /* Existing SD JSON remains authoritative, including empty lists. Migration
     * uses valid NVS first, otherwise wifi.txt; damaged NVS does not trigger it. */
    err = wifi_sd_load(network_error == ESP_OK ? &fallback : NULL,
                       network_error == ESP_OK || network_error == ESP_ERR_NOT_FOUND,
                       &config->wifi);
    if (err == ESP_OK) return ESP_OK;
    if (err != ESP_ERR_NOT_FOUND) return err;
    if (network_error == ESP_OK) config->wifi = fallback;
    else if (network_error != ESP_ERR_NOT_FOUND) return network_error;
    return ESP_OK;
}
