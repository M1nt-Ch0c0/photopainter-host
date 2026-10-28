#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t photopainter_ble_start(void);
/* Physical unbind only: revoke authorization immediately, terminate on the
 * NimBLE event queue, and let the manager drain app disconnect events. */
void photopainter_ble_revoke(void);
/* True only after transport worker has finished its local request, closed the
 * old session and released any upload admission for this exact reset serial. */
bool photopainter_ble_reset_ready(uint32_t serial);
/* state: 0 offline, 1 connecting, 2 IPv4 ready, 3 invalid network config.
 * ip has esp_ip4_addr_t.addr byte order. NULL ssid retains the current candidate. */
void photopainter_ble_network(uint8_t state, uint32_t ip, const char *ssid);
