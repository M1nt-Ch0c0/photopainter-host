#pragma once
#include "app_manifest.h"
#include "esp_err.h"
esp_err_t app_services_init(void);
void app_services_open(const char *id,uint32_t generation);
void app_services_close(void);
esp_err_t app_services_begin(const app_context_v2_t *context);
void app_services_end(void);
bool app_services_display_completed(void);
void app_services_drop_cache(const char *id);
uint32_t app_services_cache_bytes(void);
bool app_services_timer_due(uint32_t *id);
/* Called only by the sole application manager, never by a Wi-Fi callback. */
void app_services_network(bool connected);
bool app_services_connected(void);

esp_err_t app_services_legacy_begin(void);
