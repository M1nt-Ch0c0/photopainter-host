#pragma once
#include "esp_err.h"
#include "wifi_profiles.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
esp_err_t app_manager_start(void);
/* Admission covers receipt, operation and response; one large body at a time. */
bool app_manager_acquire(void);
void app_manager_release(void);
bool app_manager_busy(void);
uint32_t app_manager_epoch(void);
void app_manager_status(char *out,size_t size);
/* Synchronous jobs borrow arguments until return; call with admission held. */
int app_manager_render(const uint8_t *png,size_t size);
esp_err_t app_manager_select(const char *id,bool update);
esp_err_t app_manager_rollback(const char *id);
esp_err_t app_manager_stage(const char *id,const uint8_t *data,size_t size);
esp_err_t app_manager_remove(const char *id);
esp_err_t app_manager_wifi_read(wifi_profiles_t *out);
esp_err_t app_manager_wifi_save(const wifi_profiles_t *in);
void app_manager_network_changed(bool connected);
