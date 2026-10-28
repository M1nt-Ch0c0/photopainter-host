#include "pairing.h"
#include "esp_random.h"
#include "psa/crypto.h"
#include <string.h>

bool pp_pair_random(uint8_t *out,size_t size){if(!out)return false;esp_fill_random(out,size);return true;}
bool pp_pair_keygen(uint8_t private_key[32],uint8_t public_key[65]){
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes,256);psa_set_key_algorithm(&attributes,PSA_ALG_ECDH);
    psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_DERIVE|PSA_KEY_USAGE_EXPORT);
    mbedtls_svc_key_id_t key=0;psa_status_t status=psa_generate_key(&attributes,&key);psa_reset_key_attributes(&attributes);
    if(status!=PSA_SUCCESS)return false;
    size_t n=0;bool ok=psa_export_key(key,private_key,32,&n)==PSA_SUCCESS&&n==32;
    if(ok)ok=psa_export_public_key(key,public_key,65,&n)==PSA_SUCCESS&&n==65&&public_key[0]==4;
    psa_destroy_key(key);if(!ok){memset(private_key,0,32);memset(public_key,0,65);}return ok;
}
bool pp_pair_ecdh(const uint8_t private_key[32],const uint8_t public_key[65],uint8_t shared[32]){
    if(public_key[0]!=4)return false;
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes,256);psa_set_key_algorithm(&attributes,PSA_ALG_ECDH);psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_DERIVE);
    mbedtls_svc_key_id_t key=0;psa_status_t status=psa_import_key(&attributes,private_key,32,&key);psa_reset_key_attributes(&attributes);
    if(status!=PSA_SUCCESS)return false;
    size_t n=0;status=psa_raw_key_agreement(PSA_ALG_ECDH,key,public_key,65,shared,32,&n);psa_destroy_key(key);
    if(status!=PSA_SUCCESS||n!=32){memset(shared,0,32);return false;}return true;
}
