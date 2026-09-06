#include "wifi_station.h"
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "wifi";
#define CONNECTED BIT0
#define DISCONNECTED BIT1
#define STOPPED BIT2
static EventGroupHandle_t events;
static wifi_profiles_t saved;

/* Event callbacks never initiate a connection. One worker owns the radio, and
 * waits for STOP before starting another profile so stale events cannot win. */
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP)
        xEventGroupSetBits(events, STOPPED);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, CONNECTED);
        xEventGroupSetBits(events, DISCONNECTED);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "IPv4: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(events, CONNECTED);
    }
}
static void connect_worker(void *arg)
{
    (void)arg;
    for (;;) {
        for (uint32_t i = 0; i < saved.count; ++i) {
            wifi_config_t station = {0};
            const wifi_profile_t *p = &saved.items[i];
            memcpy(station.sta.ssid, p->ssid, strlen(p->ssid));
            memcpy(station.sta.password, p->password, strlen(p->password));
            station.sta.threshold.authmode = p->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
            station.sta.pmf_cfg.capable = true;
            xEventGroupClearBits(events, CONNECTED | DISCONNECTED | STOPPED);
            esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &station);
            memset(&station, 0, sizeof(station));
            if (err != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
            err = esp_wifi_start();
            if (err != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));
            ESP_LOGI(TAG, "trying profile %lu/%lu", (unsigned long)i + 1, (unsigned long)saved.count);
            err = esp_wifi_connect();
            EventBits_t bits = 0;
            if (err == ESP_OK)
                bits = xEventGroupWaitBits(events, CONNECTED | DISCONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
            if ((bits & CONNECTED) && !(bits & DISCONNECTED)) {
                /* Stay on the successful AP; after loss restart priority order. */
                xEventGroupWaitBits(events, DISCONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
                i = saved.count - 1;
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
            xEventGroupWaitBits(events, STOPPED, pdFALSE, pdTRUE, portMAX_DELAY);
        }
        ESP_LOGW(TAG, "retrying saved profiles after 5 seconds; credentials retained");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
esp_err_t photopainter_wifi_connect(const wifi_profiles_t *profiles)
{
    if (!wifi_profiles_valid(profiles) || !profiles->count) return ESP_ERR_INVALID_ARG;
    if (events) return ESP_ERR_INVALID_STATE;
    saved = *profiles;
    events = xEventGroupCreate();
    if (!events) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "RAM credentials");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL), TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL), TAG, "IP events");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode");
    if (xTaskCreate(connect_worker, "wifi_profiles", 4096, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    /* HTTP starts immediately; reconnection remains independent of app loading. */
    return ESP_OK;
}
