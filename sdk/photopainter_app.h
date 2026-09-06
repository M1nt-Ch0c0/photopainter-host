/* SPDX-License-Identifier: MIT
 * PhotoPainter application ABI 2.0. Canonical source: photopainter-host/sdk.
 * Plain ELF32 C ABI; no ESP-IDF types or application callbacks cross this ABI.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define APP_ABI_MAJOR 2u
#define APP_ABI_MINOR 0u
#define APP_MANIFEST_MAGIC 0x32505041u
#define APP_FRAME_WIDTH 800u
#define APP_FRAME_HEIGHT 480u
#define APP_FRAME_BYTES (APP_FRAME_WIDTH * APP_FRAME_HEIGHT)
#define APP_INPUT_PNG 1u
#define APP_REQUIRES_DISPLAY 1u
#define APP_EVENT_START 1u
#define APP_EVENT_INPUT 2u
#define APP_EVENT_TIMER 3u
#define APP_EVENT_NETWORK 4u
#define APP_EVENT_STOP 5u
#define APP_READY 1u
#define APP_OK 0
#define APP_ERR_ARGUMENT (-1)
#define APP_ERR_INPUT (-2)
#define APP_ERR_DIMENSIONS (-3)
#define APP_ERR_COLOR (-4)
#define APP_ERR_MEMORY (-5)
#define APP_ERR_DISPLAY (-6)
#define APP_ERR_TIMEOUT (-7)
#define APP_ERR_RUNTIME (-8)
#define APP_ERR_UNSUPPORTED (-9)
/* Logical, top-left pixels; board service alone rotates/packs to E6 wire order. */
#define APP_BLACK 0u
#define APP_WHITE 1u
#define APP_YELLOW 2u
#define APP_RED 3u
#define APP_BLUE 4u
#define APP_GREEN 5u
typedef struct {
    uint32_t magic, size, abi_major, abi_minor, inputs, flags;
    char id[32], name[64];
    uint32_t reserved[2];
} app_manifest_v2_t;
_Static_assert(sizeof(app_manifest_v2_t)==128,"manifest ABI");
/* Mach-O spelling is solely for native entry tests; deployed payloads are ELF32. */
#ifdef __APPLE__
#define APP_MANIFEST_SECTION "__TEXT,__app_manifest"
#else
#define APP_MANIFEST_SECTION ".app_manifest"
#endif
#define APP_MANIFEST(ID, NAME, INPUTS, FLAGS) \
    __attribute__((used,visibility("default"),section(APP_MANIFEST_SECTION),aligned(4))) \
    const app_manifest_v2_t app_manifest = {APP_MANIFEST_MAGIC,128,2,0,INPUTS,FLAGS,ID,NAME,{0,0}}
typedef struct {
    uint32_t size, abi_major, abi_minor, event, event_id, generation;
    uint32_t input_type, data_size;
    const uint8_t *data; /* borrowed until the entry returns */
    uint32_t timer_id, network_connected;
} app_context_v2_t;
typedef struct {
    uint32_t size, event_id, generation;
    int32_t status;
    uint32_t flags;
} app_result_v2_t;
_Static_assert(sizeof(app_result_v2_t)==20,"result ABI");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(app_context_v2_t)==44,"context ELF32 ABI");
#endif
const app_context_v2_t *app_host_context_v2(void);
void app_host_complete_v2(const app_result_v2_t *result);
/* At most one display per event. Full frame validated before any hardware I/O. */
int32_t app_host_display_v2(const uint8_t *pixels, uint32_t bytes);
/* Cache is optional RAM, isolated by app ID; no credentials or native pointers. */
int32_t app_host_cache_put_v2(uint32_t version, const uint8_t *data, uint32_t bytes);
const uint8_t *app_host_cache_get_v2(uint32_t version, uint32_t *bytes);
/* One timer per app, coalesced; zero interval cancels. Minimum 1000 milliseconds. */
int32_t app_host_timer_v2(uint32_t timer_id, uint32_t interval_ms);
uint64_t app_host_monotonic_ms_v2(void);
/* Unix seconds, zero if wall clock has not been set to a plausible date. */
uint64_t app_host_wall_time_v2(void);
static inline void app_complete(const app_context_v2_t *c,int32_t status,uint32_t flags) {
    app_result_v2_t r={sizeof(r),c->event_id,c->generation,status,flags};
    app_host_complete_v2(&r);
}
