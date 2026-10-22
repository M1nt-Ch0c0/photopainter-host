#include "ble_protocol.h"
#include "psa/crypto.h"

bool pp_ble_sha256(const uint8_t *p,size_t n,uint8_t out[32]) {
    size_t written=0;
    return psa_hash_compute(PSA_ALG_SHA_256,p,n,out,32,&written)==PSA_SUCCESS && written==32;
}
bool pp_ble_hmac(const uint8_t *key,size_t key_size,const uint8_t *p,size_t n,uint8_t out[32]) {
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes,PSA_KEY_TYPE_HMAC);
    psa_set_key_algorithm(&attributes,PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_SIGN_MESSAGE);
    mbedtls_svc_key_id_t id=0;
    psa_status_t status=psa_import_key(&attributes,key,key_size,&id);
    psa_reset_key_attributes(&attributes);
    if(status!=PSA_SUCCESS) return false;
    size_t written=0;
    status=psa_mac_compute(id,PSA_ALG_HMAC(PSA_ALG_SHA_256),p,n,out,32,&written);
    psa_destroy_key(id);
    return status==PSA_SUCCESS && written==32;
}
