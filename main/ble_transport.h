#pragma once
#include "esp_err.h"
#include <stdint.h>

esp_err_t photopainter_ble_start(const char *token);
/* state: 0 offline, 1 connecting, 2 IPv4 ready, 3 invalid network config.
 * ip has esp_ip4_addr_t.addr byte order. NULL ssid retains the current candidate. */
void photopainter_ble_network(uint8_t state, uint32_t ip, const char *ssid);
