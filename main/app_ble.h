#pragma once
#include "../sdk/photopainter_app.h"
#include <stdbool.h>

/* Lifecycle and event consumption run on the App Manager. Radio-side entry
 * points below copy bounded data and never invoke application code. */
void app_ble_bind(uint32_t generation);
void app_ble_cancel(void);
int app_ble_register(uint32_t generation,const app_ble_service_v2_t *);
int app_ble_set(uint32_t generation,uint32_t index,const uint8_t *,uint32_t bytes);
int app_ble_unregister(uint32_t generation);
bool app_ble_take(uint32_t generation,app_ble_event_v2_t *);
void app_ble_connection(bool authenticated);
void app_ble_error(uint32_t revision);
bool app_ble_schema(app_ble_service_v2_t *,uint32_t *generation,uint32_t *revision);
/* request returns protocol errors; write is accepted only if event queue has room. */
int app_ble_request(bool write,uint32_t generation,uint32_t revision,uint32_t index,
                    const uint8_t *data,size_t size,uint8_t *out,size_t *out_size);
bool app_ble_value(uint32_t revision,uint32_t index,uint8_t out[128],size_t *size);
uint32_t app_ble_notifications(uint32_t revision);
/* Implemented by the radio adapter; called after releasing the state lock. */
void photopainter_ble_app_changed(void);
