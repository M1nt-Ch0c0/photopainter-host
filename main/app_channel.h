/* SPDX-License-Identifier: MIT */
#pragma once
#include "../sdk/photopainter_app.h"
#include <stdbool.h>
/* Framework mailbox; adapters supply a nonzero peer epoch after authentication.
 * No adapter receives an ELF pointer. Caller pins authorization through Manager. */
void app_channel_bind(uint32_t generation);
void app_channel_cancel(void);
int app_channel_register(uint32_t generation,const app_channel_config_v23_t *config);
int app_channel_close(uint32_t generation);
int app_channel_info(uint32_t generation,app_channel_info_v23_t *info);
bool app_channel_schema(app_channel_config_v23_t *,uint32_t *generation,uint32_t *revision);
int app_channel_connect(uint32_t generation,uint32_t revision,uint32_t peer,uint32_t *session);
void app_channel_disconnect(uint32_t peer);
void app_channel_peer_authorized(uint32_t peer,bool authorized);
int app_channel_receive(uint32_t generation,uint32_t revision,uint32_t session,const void *,uint32_t bytes);
int app_channel_recv(uint32_t generation,uint32_t session,void *,uint32_t capacity,uint32_t *received);
int app_channel_send(uint32_t generation,uint32_t session,const void *,uint32_t bytes);
int app_channel_transmit(uint32_t generation,uint32_t session,void *,uint32_t capacity,uint32_t *bytes);
bool app_channel_take(uint32_t generation,app_channel_event_v23_t *);
