/* SPDX-License-Identifier: MIT */
#pragma once
#include "../sdk/photopainter_app.h"
#include <stdbool.h>
#include <stddef.h>
#define APP_COC_PSM 0x81u
#define APP_COC_SDU_MAX 4096u
#define APP_COC_OVERHEAD 38u
#define APP_COC_SLOTS_MAX 8u
void app_coc_bind(uint32_t generation);
void app_coc_cancel(void);
int app_coc_open(uint32_t generation,const app_coc_config_v22_t *config);
int app_coc_close(uint32_t generation);
int app_coc_info(uint32_t generation,app_coc_info_v22_t *info);
int app_coc_recv(uint32_t generation,uint32_t session,void *dst,uint32_t capacity,uint32_t *received);
int app_coc_send(uint32_t generation,uint32_t session,const void *src,uint32_t bytes);
bool app_coc_take(uint32_t generation,app_coc_event_v22_t *event);
/* Radio adapter never receives APP pointers. Revision invalidates old work. */
bool app_coc_registration(uint32_t *generation,uint32_t *revision,uint32_t *payload_max);
uint32_t app_coc_connected(uint32_t revision,uint32_t peer_payload_max);
void app_coc_disconnected(uint32_t revision,uint32_t session,int error);
bool app_coc_valid(uint32_t revision,uint32_t session);
bool app_coc_rx_space(uint32_t revision,uint32_t session);
int app_coc_receive(uint32_t revision,uint32_t session,const void *data,uint32_t size);
int app_coc_transmit(uint32_t revision,uint32_t session,void *data,uint32_t capacity,uint32_t *size);
void photopainter_coc_wakeup(void);
