#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "image_limits.h"

/* Wire contract is documented in docs/ble-protocol.md. No RTOS or radio here. */
#define PP_BLE_VERSION 2
#define PP_BLE_FRAME_MAX 512
#define PP_BLE_INFO_SIZE 20
#define PP_BLE_IDLE_MS 60000u
#define PP_BLE_SESSION_MS (2u * 60u * 60u * 1000u)
enum pp_ble_op {
    PP_BLE_AUTH=1, PP_BLE_INFO=2, PP_BLE_STATUS=3, PP_BLE_BEGIN=4,
    PP_BLE_CHUNK=5, PP_BLE_COMMIT=6, PP_BLE_ABORT=7, PP_BLE_PROGRESS=8,
    PP_BLE_ACTIVATE=9, PP_BLE_SWITCH=10, PP_BLE_ROLLBACK=11, PP_BLE_REMOVE=12,
    PP_BLE_APP_READ=13, PP_BLE_APP_WRITE=14,
    PP_BLE_IMAGE_CAPS=15, PP_BLE_IMAGE_BEGIN=16, PP_BLE_IMAGE_RESULT=17,
};
enum pp_ble_error {
    PP_BLE_OK=0, PP_BLE_UNAUTHORIZED=1, PP_BLE_UNCONFIGURED=2, PP_BLE_BUSY=3,
    PP_BLE_INVALID=4, PP_BLE_OFFSET=5, PP_BLE_HASH=6, PP_BLE_MEMORY=7,
    PP_BLE_STATE=8, PP_BLE_PACKAGE=9, PP_BLE_STORAGE=10, PP_BLE_EXPIRED=11,
    PP_BLE_IMAGE_INVALID=12, PP_BLE_DISPLAY_FAILED=13, PP_BLE_BOOT_CHANGED=14,
};
#define PP_BLE_IMAGE_RECEIPTS 4u
enum {PP_BLE_IMAGE_UNKNOWN=0, PP_BLE_IMAGE_RUNNING=1, PP_BLE_IMAGE_COMPLETE=2};
typedef struct {
    uint8_t id[16],digest[32];
    char app[32];
    uint32_t size;
    uint16_t result;
    uint8_t state;
} pp_ble_image_receipt;
typedef struct {
    uint8_t boot[16];
    unsigned next;
    pp_ble_image_receipt receipts[PP_BLE_IMAGE_RECEIPTS];
} pp_ble_images;
typedef struct {
    bool (*acquire)(void);
    void (*release)(void);
    void *(*alloc)(size_t);
    void (*free)(void *);
    int (*stage)(const char *, const uint8_t *, size_t);
    int (*manage)(uint8_t, const char *);
    void (*status)(char *, size_t);
    int (*app)(uint8_t,const uint8_t *,size_t,uint8_t *,size_t *);
    pp_ble_images *images;
    int (*image_ready)(const char *);
    /* Synchronous: success means physical refresh AND final POWER_OFF done. */
    int (*render)(const uint8_t *,size_t);
} pp_ble_ops;
typedef struct {
    uint8_t nonce[32], key[32], id[6];
    bool challenge, authenticated, held;
    uint32_t sequence;
    uint64_t started, touched;
    uint8_t *package, digest[32];
    uint32_t total, received;
    bool image;
    uint8_t image_id[16];
    char app[32], snapshot[4096];
    const pp_ble_ops *ops;
} pp_ble_session;
typedef struct {
    uint8_t data[PP_BLE_FRAME_MAX];
    uint16_t received, total;
} pp_ble_fragments;

/* Per boot, outside connection/session state; never persisted to Flash. */
void pp_ble_images_init(pp_ble_images *,const uint8_t boot[16]);

/* Crypto providers fail closed. Production uses ESP-IDF PSA; native uses OpenSSL. */
bool pp_ble_sha256(const uint8_t *, size_t, uint8_t out[32]);
bool pp_ble_hmac(const uint8_t *, size_t, const uint8_t *, size_t, uint8_t out[32]);
bool pp_ble_tag(const uint8_t *key, size_t key_size, const char *domain,
                const uint8_t *data, size_t size, uint8_t out[32]);
void pp_ble_open(pp_ble_session *, const pp_ble_ops *, const uint8_t id[6],
                 const uint8_t nonce[32], uint64_t now);
void pp_ble_close(pp_ble_session *);
/* True for a closed/expired session until pp_ble_open; transport must disconnect. */
bool pp_ble_expire(pp_ble_session *, uint64_t now);
/* Returns a complete response. Caller serializes calls and owns connection epochs. */
size_t pp_ble_request(pp_ble_session *, const char *token, const uint8_t info[20],
                      const uint8_t *request, size_t size, uint64_t now, uint8_t out[512]);
/* 0=incomplete, 1=complete, -1=invalid. Duplicate identical fragments are accepted. */
int pp_ble_fragment(pp_ble_fragments *, const uint8_t *, size_t);
uint32_t pp_ble_u32(const uint8_t *);
void pp_ble_put32(uint8_t *, uint32_t);
