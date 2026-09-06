#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "host_config.h"
#include "nvs_flash.h"
#include "app_manager.h"
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
    }

    /* Local applications boot independently of Wi-Fi and incoming HTTP. */
    err = app_manager_start();
    if (err != ESP_OK) ESP_LOGE(TAG,"application manager unavailable: %s",esp_err_to_name(err));
    photopainter_host_config_t config;
    err = photopainter_host_config_load(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"network configuration unavailable; local applications remain active");
        clear_config(&config);return;
    }
    if (config.wifi.count) {
        err=photopainter_wifi_connect(&config.wifi);
        if(err==ESP_OK)err=photopainter_push_server_start(config.push_token);
        if(err!=ESP_OK)ESP_LOGE(TAG,"network service unavailable: %s",esp_err_to_name(err));
    } else ESP_LOGW(TAG,"no Wi-Fi profiles; local applications remain active");
    clear_config(&config);
}
