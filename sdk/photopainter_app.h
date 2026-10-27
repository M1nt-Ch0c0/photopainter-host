/* SPDX-License-Identifier: MIT
 * PhotoPainter application ABI 2.2. Canonical source: photopainter-host/sdk.
 * Plain ELF32 C ABI; no ESP-IDF types or application callbacks cross this ABI.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define APP_ABI_MAJOR 2u
#define APP_ABI_MINOR 2u
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
#define APP_EVENT_BLE 6u
#define APP_EVENT_COC 7u
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
/* Opt in to BLE imports/events. Existing APP_MANIFEST stays at ABI 2.0. */
#define APP_MANIFEST_BLE(ID, NAME, INPUTS, FLAGS) \
    __attribute__((used,visibility("default"),section(APP_MANIFEST_SECTION),aligned(4))) \
    const app_manifest_v2_t app_manifest = {APP_MANIFEST_MAGIC,128,2,1,INPUTS,FLAGS,ID,NAME,{0,0}}
#define APP_MANIFEST_COC(ID, NAME, INPUTS, FLAGS) \
    __attribute__((used,visibility("default"),section(APP_MANIFEST_SECTION),aligned(4))) \
    const app_manifest_v2_t app_manifest = {APP_MANIFEST_MAGIC,128,2,2,INPUTS,FLAGS,ID,NAME,{0,0}}
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

/* ABI 2.1: one service and at most four characteristics, copied into host RAM.
 * UUID bytes are in conventional printed order, not NimBLE's reversed order.
 * No ELF callback pointers are retained. BLE events use context.data/data_size.
 * Services and queued events are cancelled before unloading the application.
 * Discovery/management UUIDs and Bluetooth SIG UUIDs are reserved to the host. */
#define APP_BLE_MAX_CHARACTERISTICS 4u
#define APP_BLE_MAX_VALUE 128u
#define APP_BLE_READ 1u
#define APP_BLE_WRITE 2u
#define APP_BLE_NOTIFY 4u
#define APP_BLE_CONNECTED 1u
#define APP_BLE_DISCONNECTED 2u
#define APP_BLE_WRITTEN 3u
#define APP_BLE_ERROR 4u
typedef struct {
    uint8_t uuid[16];
    uint32_t properties, max_bytes;
} app_ble_characteristic_v2_t;
typedef struct {
    uint32_t size, count;
    uint8_t uuid[16];
    app_ble_characteristic_v2_t characteristics[APP_BLE_MAX_CHARACTERISTICS];
} app_ble_service_v2_t;
typedef struct {
    uint32_t type, characteristic, data_size;
    uint8_t data[APP_BLE_MAX_VALUE];
} app_ble_event_v2_t;
_Static_assert(sizeof(app_ble_service_v2_t)==120,"BLE service ABI");
_Static_assert(sizeof(app_ble_event_v2_t)==140,"BLE event ABI");
/* Register once per active lifetime (unregister permits registering again).
 * Publication is asynchronous; APP_BLE_ERROR reports a radio registration error.
 * A CONNECTED event means an authenticated host session, not an anonymous link. */
int32_t app_host_ble_register_v2(const app_ble_service_v2_t *service);
int32_t app_host_ble_set_v2(uint32_t characteristic,const uint8_t *data,uint32_t bytes);
int32_t app_host_ble_unregister_v2(void);
/* ABI 2.2: one foreground owner and one authenticated CoC connection.
 * These I/O statuses are not app_complete statuses. All calls are nonblocking.
 * Records are copied; no APP pointer/callback survives an entry invocation.
 * Closing/unloading discards queued records. Disconnect preserves registration.
 */
#define APP_COC_OK 0
#define APP_COC_AGAIN 1
#define APP_COC_CLOSED 2
#define APP_COC_STALE 3
#define APP_COC_TOO_SMALL 4
#define APP_COC_BUSY 5
#define APP_COC_CONNECTED 1u
#define APP_COC_DISCONNECTED 2u
#define APP_COC_RX_READY 4u
#define APP_COC_TX_READY 8u
#define APP_COC_ERROR 16u
typedef struct {
    uint32_t size, rx_sdu_bytes, rx_slots, tx_slots;
} app_coc_config_v22_t;
typedef struct {
    uint32_t size, state, psm, session, rx_payload_max, tx_payload_max;
} app_coc_info_v22_t;
typedef struct {
    uint32_t size, type, session, rx_pending, tx_free;
    int32_t status;
} app_coc_event_v22_t;
_Static_assert(sizeof(app_coc_config_v22_t)==16,"CoC config ABI");
_Static_assert(sizeof(app_coc_info_v22_t)==24,"CoC info ABI");
_Static_assert(sizeof(app_coc_event_v22_t)==24,"CoC event ABI");
int32_t app_host_coc_open_v22(const app_coc_config_v22_t *config);
int32_t app_host_coc_info_v22(app_coc_info_v22_t *info);
/* TOO_SMALL reports required bytes without consuming the record. */
int32_t app_host_coc_recv_v22(uint32_t session,void *dst,uint32_t capacity,uint32_t *received);
/* OK means copied into the host queue, not remote business completion. */
int32_t app_host_coc_send_v22(uint32_t session,const void *src,uint32_t bytes);
int32_t app_host_coc_close_v22(void);
static inline void app_complete(const app_context_v2_t *c,int32_t status,uint32_t flags) {
    app_result_v2_t r={sizeof(r),c->event_id,c->generation,status,flags};
    app_host_complete_v2(&r);
}
