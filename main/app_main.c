#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "host_config.h"
#include "nvs_flash.h"
#include "photoframe_plugin.h"
#include "push_server.h"
#include "wifi_station.h"

static const char *TAG = "photopainter_host";

static void clear_config(photopainter_host_config_t *config)
{
    if (config != NULL) {
        memset(config, 0, sizeof(*config));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        return;
    }

    photopainter_host_config_t config;
    err = photopainter_host_config_load(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "configuration load failed: %s", esp_err_to_name(err));
        clear_config(&config);
        return;
    }
    if (config.wifi.count == 0) {
        ESP_LOGE(TAG, "Wi-Fi is not provisioned");
        clear_config(&config);
        return;
    }

    err = photoframe_plugin_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "photoframe payload unavailable: %s",
                 esp_err_to_name(err));
        /* Keep starting the server: authenticated pushes will fail safely and
         * never touch the panel. */
    }

    err = photopainter_wifi_connect(&config.wifi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi startup failed: %s", esp_err_to_name(err));
        clear_config(&config);
        return;
    }
    err = photopainter_push_server_start(config.push_token);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "push server startup failed: %s", esp_err_to_name(err));
    }

    clear_config(&config);
}
