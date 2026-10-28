#include "pairing_store.h"
#include "nvs.h"
#include <assert.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <string.h>
static bool namespace_exists,key_exists,crc_bad,fail_commit;
static uint8_t record[256],staged[256];static size_t record_bytes,staged_bytes;
static unsigned commits;
bool pp_ble_sha256(const uint8_t *p,size_t n,uint8_t out[32]){return SHA256(p,n,out)!=NULL;}
bool pp_ble_hmac(const uint8_t *k,size_t kn,const uint8_t *p,size_t n,uint8_t out[32]){unsigned z=0;return HMAC(EVP_sha256(),k,(int)kn,p,n,out,&z)&&z==32;}
bool pp_pair_random(uint8_t *p,size_t n){(void)p;(void)n;return false;}
bool pp_pair_keygen(uint8_t a[32],uint8_t b[65]){(void)a;(void)b;return false;}
bool pp_pair_ecdh(const uint8_t a[32],const uint8_t b[65],uint8_t c[32]){(void)a;(void)b;(void)c;return false;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"pp_pair"));if(!namespace_exists&&mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;namespace_exists=true;*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1);}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t bytes){assert(h==1&&!strcmp(key,"owner")&&bytes<=sizeof(staged));memcpy(staged,data,bytes);staged_bytes=bytes;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==1);commits++;if(fail_commit)return ESP_FAIL;memcpy(record,staged,staged_bytes);record_bytes=staged_bytes;key_exists=true;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *data,size_t *bytes){
 assert(h==1&&!strcmp(key,"owner"));
 if(crc_bad){key_exists=false;crc_bad=false;return ESP_ERR_NVS_NOT_FOUND;}
 if(!key_exists)return ESP_ERR_NVS_NOT_FOUND;
 if(*bytes<record_bytes)return ESP_ERR_INVALID_SIZE;*bytes=record_bytes;memcpy(data,record,record_bytes);return ESP_OK;
}
int main(void){
 pp_pair_binding owner={0},loaded;bool bound=true;
 assert(pp_pair_store_load(&loaded,&bound)==ESP_OK&&!bound&&!namespace_exists);
 namespace_exists=true;assert(pp_pair_store_load(&loaded,&bound)!=ESP_OK&&!bound);
 memset(owner.client,0x11,16);memset(owner.binding,0x22,16);memset(owner.root,0x33,32);strcpy(owner.name,"Test Browser");
 assert(pp_pair_store_save(&owner)&&commits==1&&namespace_exists&&key_exists);
 assert(pp_pair_store_load(&loaded,&bound)==ESP_OK&&bound&&!memcmp(&owner,&loaded,sizeof(owner)));
 // Exact ESP-IDF lower-layer CRC behavior: erase entry, return NOT_FOUND.
 crc_bad=true;assert(pp_pair_store_load(&loaded,&bound)!=ESP_OK&&!bound&&!key_exists);
 assert(pp_pair_store_load(&loaded,&bound)!=ESP_OK&&!bound);
 // Reset saves a versioned, checksummed tombstone that survives a fresh load.
 assert(pp_pair_store_clear()&&key_exists&&record[8]==0);
 memset(&loaded,0xaa,sizeof(loaded));bound=true;
 assert(pp_pair_store_load(&loaded,&bound)==ESP_OK&&!bound);
 for(unsigned i=0;i<sizeof(loaded);i++)assert(((uint8_t *)&loaded)[i]==0);
 // Even a correctly hashed tombstone may not contain residual owner material.
 record[9]=1;assert(pp_ble_sha256(record,106,record+106));
 assert(pp_pair_store_load(&loaded,&bound)!=ESP_OK&&!bound);
 assert(pp_pair_store_save(&owner));fail_commit=true;
 assert(!pp_pair_store_clear());
 assert(pp_pair_store_load(&loaded,&bound)==ESP_OK&&bound&&!memcmp(&owner,&loaded,sizeof(owner)));
 return 0;
}
