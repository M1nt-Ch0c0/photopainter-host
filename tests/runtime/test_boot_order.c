#include "app_manager.h"
#include "host_config.h"
#include "push_server.h"
#include "wifi_station.h"
#include <assert.h>
#include <string.h>
void app_main(void);
static int mode,local,network,http,ble,pairing;
esp_err_t app_pairing_init(void){assert(local==0);pairing++;return ESP_OK;}
esp_err_t photopainter_ble_start(void){assert(local==1&&pairing==1);ble++;return ESP_OK;}
void photopainter_ble_network(uint8_t state,uint32_t ip,const char *ssid){assert(state==3 && ip==0 && ssid==NULL);}
esp_err_t nvs_flash_init(void){return mode==3?ESP_FAIL:ESP_OK;}
esp_err_t app_manager_start(void){local++;return ESP_OK;}
esp_err_t photopainter_host_config_load(photopainter_host_config_t *c) {
    assert(local==1);memset(c,0,sizeof(*c));
    c->wifi.version=1;c->wifi.count=mode==0?0:1;
    if(mode==1)return ESP_ERR_INVALID_ARG; /* damaged authoritative SD JSON */
    if(mode==2)c->token_status=ESP_ERR_INVALID_SIZE; /* token disabled, Wi-Fi independent */
    return ESP_OK;
}
esp_err_t photopainter_wifi_connect(const wifi_profiles_t *c){assert(local==1&&c->count==1);network++;return ESP_OK;}
esp_err_t photopainter_push_server_start(void){assert(local==1&&network==1);http++;return ESP_OK;}
int main(void) {
    for(mode=0;mode<4;mode++) {
        local=network=http=ble=pairing=0;app_main();assert(local==1 && ble==1&&pairing==1);
        assert(network==(mode>=2));assert(http==(mode>=2));
    }
    return 0;
}
