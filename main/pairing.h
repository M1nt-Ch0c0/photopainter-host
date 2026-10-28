#pragma once
#include "ble_protocol.h"

#define PP_PAIR_VERSION 2u
#define PP_PAIR_NAME_MAX 32u
#define PP_PAIR_COMMIT_MS 30000u
#define PP_PAIR_DISPLAY_MS 650000u
#define PP_PAIR_CONFIRM_MS 120000u
#define PP_PAIR_COOLDOWN_MS 60000u

enum pp_pair_op {PP_PAIR_INFO=18,PP_PAIR_COMMIT=19,PP_PAIR_REVEAL=20,PP_PAIR_POLL=21,PP_PAIR_CANCEL=22,PP_BOND_AUTH=23};
enum pp_pair_state {PP_PAIR_UNBOUND,PP_PAIR_WAIT_REVEAL,PP_PAIR_WAIT_CLIENT,PP_PAIR_DISPLAYING,PP_PAIR_AWAIT_BUTTON,PP_PAIR_BOUND,PP_PAIR_CANCELLED,PP_PAIR_EXPIRED,PP_PAIR_FAULT};
enum pp_pair_view_kind {PP_PAIR_VIEW_READY,PP_PAIR_VIEW_PROMPT,PP_PAIR_VIEW_BOUND,PP_PAIR_VIEW_CANCELLED,PP_PAIR_VIEW_EXPIRED,PP_PAIR_VIEW_ERROR};
typedef struct {uint8_t client[16],binding[16],root[32];char name[PP_PAIR_NAME_MAX+1];} pp_pair_binding;
typedef struct {
    uint32_t serial,kind,code;
    uint8_t binding[16];char name[PP_PAIR_NAME_MAX+1];
} pp_pair_view;
typedef struct {bool (*save)(const pp_pair_binding *);bool (*clear)(void);} pp_pair_ops;
typedef struct {
    const pp_pair_ops *ops;
    pp_pair_binding owner,candidate;
    bool bound,storage_fault,has_candidate,has_root,view_pending;
    uint8_t state,error,id[6],client_commit[32],server_commit[32];
    uint8_t private_key[32],public_key[65],nonce[32],client_public[65],client_nonce[32];
    uint32_t code,view_serial,authorization_epoch;
    uint64_t deadline,next_start;
    pp_pair_view view;
} pp_pairing;

/* Platform crypto providers. Never log these inputs/outputs. */
bool pp_pair_random(uint8_t *,size_t);
bool pp_pair_keygen(uint8_t private_key[32],uint8_t public_key[65]);
bool pp_pair_ecdh(const uint8_t private_key[32],const uint8_t public_key[65],uint8_t shared[32]);

void pp_pair_init(pp_pairing *,const pp_pair_ops *,const uint8_t id[6],const pp_pair_binding *owner,bool storage_fault);
void pp_pair_tick(pp_pairing *,uint64_t now);
/* Pairing state machine is serialized by its platform lock, not radio callbacks. */
size_t pp_pair_request(pp_pairing *,pp_ble_session *,const uint8_t info[20],const uint8_t *,size_t,uint64_t,uint8_t out[512]);
bool pp_pair_take_view(pp_pairing *,pp_pair_view *);
void pp_pair_view_complete(pp_pairing *,uint32_t serial,bool success,uint64_t now);
/* The expected serial is captured at the physical press edge. */
bool pp_pair_confirm(pp_pairing *,uint32_t expected_serial,uint64_t now);
bool pp_pair_reset(pp_pairing *,uint64_t now);
bool pp_pair_blocks_app(const pp_pairing *);
bool pp_pair_binding_valid(const pp_pair_binding *);
