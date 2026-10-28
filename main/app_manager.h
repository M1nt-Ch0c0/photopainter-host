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
/* Reset REQUEST atomically blocks new display/admission, including during an
 * existing display. Actual reset waits for that display and exact BLE drain. */
uint32_t app_manager_pairing_reset_pending(void);
bool app_manager_request_pairing_reset(void);
bool app_manager_display_begin(void);
void app_manager_display_end(void);
bool app_manager_display_busy(void);
void app_manager_status(char *out,size_t size);
/* Read-only target check; caller holds admission across reception/render. */
bool app_manager_target_is_running(const char *id);
/* Synchronous jobs borrow arguments until return; call with admission held. */
int app_manager_render(const uint8_t *png,size_t size);
esp_err_t app_manager_select(const char *id,bool update);
/* source must remain valid until return (transport uses literal http/ble). */
esp_err_t app_manager_select_from(const char *id,bool update,const char *source);
esp_err_t app_manager_rollback(const char *id);
esp_err_t app_manager_rollback_from(const char *id,const char *source);
esp_err_t app_manager_stage(const char *id,const uint8_t *data,size_t size);
esp_err_t app_manager_remove(const char *id);
esp_err_t app_manager_wifi_read(wifi_profiles_t *out);
esp_err_t app_manager_wifi_save(const wifi_profiles_t *in);
/* Execute queued BLE boundary/write events with admission held; keeps the
 * transport response pending until the application's write event completes. */
esp_err_t app_manager_ble_flush(uint32_t generation);
/* BLE variants pin the physical-owner epoch into the queued job. */
int app_manager_render_authorized(const uint8_t *,size_t,uint32_t authority);
esp_err_t app_manager_select_authorized(const char *,bool,const char *,uint32_t authority);
esp_err_t app_manager_rollback_authorized(const char *,const char *,uint32_t authority);
esp_err_t app_manager_stage_authorized(const char *,const uint8_t *,size_t,uint32_t authority);
esp_err_t app_manager_remove_authorized(const char *,uint32_t authority);
esp_err_t app_manager_ble_request_authorized(bool write,const uint8_t *,size_t,uint8_t *,size_t *,uint32_t authority);
void app_manager_network_changed(bool connected);
