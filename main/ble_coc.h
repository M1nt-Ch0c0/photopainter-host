/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
int photopainter_coc_init(void);
int photopainter_coc_start(void);
void photopainter_coc_reset(void);
void photopainter_coc_wakeup(void);
bool photopainter_coc_active(uint16_t connection);
/* Implemented by the existing GATT authentication transport. */
bool photopainter_ble_coc_key(uint16_t connection,uint8_t key[32]);
