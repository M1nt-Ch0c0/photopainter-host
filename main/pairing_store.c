#include "pairing_store.h"
#include "nvs.h"
#include <string.h>
#define OWNER_BYTES 138u
#define CHECKSUM_OFFSET 106u
static void wipe(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static bool same(const uint8_t *a,const uint8_t *b,size_t n){unsigned v=0;while(n--)v|=*a++^*b++;return v==0;}
static bool zero(const uint8_t *p,size_t n){unsigned v=0;while(n--)v|=*p++;return v==0;}
static bool write_record(const pp_pair_binding *owner){
    if(owner&&!pp_pair_binding_valid(owner))return false;
    uint8_t record[OWNER_BYTES]={0};memcpy(record,"PPB2",4);pp_ble_put32(record+4,2);record[8]=owner?1:0;
    if(owner){memcpy(record+9,owner->client,16);memcpy(record+25,owner->binding,16);memcpy(record+41,owner->root,32);memcpy(record+73,owner->name,33);}
    bool ok=pp_ble_sha256(record,CHECKSUM_OFFSET,record+CHECKSUM_OFFSET);nvs_handle_t h;
    if(ok){
        ok=nvs_open("pp_pair",NVS_READWRITE,&h)==ESP_OK;
        if(ok){ok=nvs_set_blob(h,"owner",record,sizeof(record))==ESP_OK&&nvs_commit(h)==ESP_OK;nvs_close(h);}
    }
    wipe(record,sizeof(record));return ok;
}
bool pp_pair_store_save(const pp_pair_binding *owner){return owner&&write_record(owner);}
bool pp_pair_store_clear(void){return write_record(NULL);}
esp_err_t pp_pair_store_load(pp_pair_binding *owner,bool *bound){
    if(!owner||!bound)return ESP_ERR_INVALID_ARG;
    memset(owner,0,sizeof(*owner));*bound=false;nvs_handle_t h;
    esp_err_t result=nvs_open("pp_pair",NVS_READONLY,&h);
    if(result==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(result!=ESP_OK)return result;
    uint8_t record[OWNER_BYTES]={0},digest[32];size_t bytes=sizeof(record);
    result=nvs_get_blob(h,"owner",record,&bytes);nvs_close(h);
    /* NVS can erase a CRC-invalid blob and report NOT_FOUND. Once the namespace
     * exists, this is not evidence of an intentional physical unbind. */
    if(result==ESP_ERR_NVS_NOT_FOUND)result=ESP_ERR_INVALID_STATE;
    if(result==ESP_OK){
        if(bytes!=sizeof(record)||memcmp(record,"PPB2",4)||pp_ble_u32(record+4)!=2||record[8]>1||
           !pp_ble_sha256(record,CHECKSUM_OFFSET,digest)||!same(digest,record+CHECKSUM_OFFSET,32))result=ESP_ERR_INVALID_CRC;
        else if(record[8]==0){if(!zero(record+9,97))result=ESP_ERR_INVALID_CRC;}
        else{
            memcpy(owner->client,record+9,16);memcpy(owner->binding,record+25,16);memcpy(owner->root,record+41,32);memcpy(owner->name,record+73,33);
            if(!pp_pair_binding_valid(owner))result=ESP_ERR_INVALID_CRC;
            else *bound=true;
        }
    }
    wipe(record,sizeof(record));wipe(digest,sizeof(digest));
    if(result!=ESP_OK){wipe(owner,sizeof(*owner));*bound=false;}
    return result;
}
