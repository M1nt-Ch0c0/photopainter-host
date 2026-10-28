#include "app_pairing.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "pairing_store.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stddef.h>
#include <string.h>

static pp_pairing pairing;
static SemaphoreHandle_t lock;
static uint64_t now(void){return (uint64_t)esp_timer_get_time()/1000;}
static void wipe(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static const pp_pair_ops operations={.save=pp_pair_store_save,.clear=pp_pair_store_clear};
esp_err_t app_pairing_init(void){
    if(lock)return ESP_ERR_INVALID_STATE;
    if(psa_crypto_init()!=PSA_SUCCESS)return ESP_FAIL;
    uint8_t id[6];esp_err_t result=esp_read_mac(id,ESP_MAC_WIFI_STA);if(result!=ESP_OK)return result;
    lock=xSemaphoreCreateMutex();if(!lock)return ESP_ERR_NO_MEM;
    bool present=false;pp_pair_binding owner={0};
    esp_err_t e=pp_pair_store_load(&owner,&present);
    pp_pair_init(&pairing,&operations,id,present?&owner:NULL,e!=ESP_OK);
    wipe(&owner,sizeof(owner));return ESP_OK;
}
size_t app_pairing_request(pp_ble_session *session,const uint8_t info[20],const uint8_t *r,size_t n,uint64_t ms,uint8_t out[512]){
    if(!lock){out[0]=n?r[0]:0;memset(out+1,0,6);out[5]=PP_BLE_STORAGE;return 7;}
    xSemaphoreTake(lock,portMAX_DELAY);size_t result=pp_pair_request(&pairing,session,info,r,n,ms,out);xSemaphoreGive(lock);return result;
}
uint32_t app_pairing_authorization_epoch(void){
    if(!lock)return 0;
    xSemaphoreTake(lock,portMAX_DELAY);uint32_t e=pairing.authorization_epoch;xSemaphoreGive(lock);return e;
}
unsigned app_pairing_state(uint32_t *serial){
    if(!lock){if(serial)*serial=0;return PP_PAIR_FAULT;}
    xSemaphoreTake(lock,portMAX_DELAY);pp_pair_tick(&pairing,now());unsigned state=pairing.state;if(serial)*serial=pairing.view_serial;xSemaphoreGive(lock);return state;
}
bool app_pairing_blocks_app(void){unsigned s=app_pairing_state(NULL);return s==PP_PAIR_DISPLAYING||s==PP_PAIR_AWAIT_BUTTON;}
bool app_pairing_take_view(pp_pair_view *out){
    if(!lock)return false;
    xSemaphoreTake(lock,portMAX_DELAY);pp_pair_tick(&pairing,now());bool have=pp_pair_take_view(&pairing,out);xSemaphoreGive(lock);return have;
}
void app_pairing_view_complete(uint32_t serial,bool success){
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);pp_pair_view_complete(&pairing,serial,success,now());xSemaphoreGive(lock);
}
esp_err_t app_pairing_button(bool reset,uint32_t serial){
    if(!lock)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock,portMAX_DELAY);bool ok=reset?pp_pair_reset(&pairing,now()):pp_pair_confirm(&pairing,serial,now());xSemaphoreGive(lock);
    return ok?ESP_OK:ESP_ERR_INVALID_STATE;
}
