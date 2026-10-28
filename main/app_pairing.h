#pragma once
#include "pairing.h"
#include "esp_err.h"
esp_err_t app_pairing_init(void);
size_t app_pairing_request(pp_ble_session *,const uint8_t info[20],const uint8_t *,size_t,uint64_t,uint8_t out[512]);
uint32_t app_pairing_authorization_epoch(void);
/* Safe summary only; never returns owner root or ephemeral private material. */
unsigned app_pairing_state(uint32_t *screen_serial);
bool app_pairing_blocks_app(void);
bool app_pairing_take_view(pp_pair_view *);
void app_pairing_view_complete(uint32_t serial,bool success);
esp_err_t app_pairing_button(bool reset,uint32_t expected_serial);
/* Called by the sole manager with display admission, not radio callbacks. */
int app_pairing_draw(const pp_pair_view *);
