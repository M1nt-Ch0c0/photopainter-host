#include "app_ble.h"
#include "ble_protocol.h"
#include <assert.h>
#include <string.h>
static int changes;
void photopainter_ble_app_changed(void){changes++;}
int main(void) {
    app_ble_service_v2_t s={.size=sizeof(s),.count=1,.uuid={0x91},
        .characteristics={{.uuid={0x92},.properties=APP_BLE_READ|APP_BLE_WRITE|APP_BLE_NOTIFY,.max_bytes=128}}};
    assert(app_ble_register(1,&s)==APP_ERR_RUNTIME);
    app_ble_bind(1);assert(app_ble_register(1,&s)==APP_OK);
    app_ble_service_v2_t copy;uint32_t gen,rev;
    assert(app_ble_schema(&copy,&gen,&rev) && gen==1 && !memcmp(&s,&copy,sizeof(s)));
    memset(s.uuid,0xff,16); /* Description was copied, not borrowed from ELF. */
    assert(app_ble_schema(&copy,NULL,NULL) && copy.uuid[0]==0x91);
    assert(app_ble_register(1,&s)==APP_ERR_RUNTIME);
    uint8_t out[128];size_t size=0;
    assert(app_ble_request(true,1,rev,0,(const uint8_t *)"x",1,out,&size)==PP_BLE_UNAUTHORIZED);
    assert(!app_ble_value(rev,0,out,&size));
    app_ble_connection(true);app_ble_event_v2_t e;
    assert(app_ble_take(1,&e) && e.type==APP_BLE_CONNECTED);
    assert(!app_ble_take(2,&e));
    assert(app_ble_request(true,2,rev,0,NULL,0,out,&size)==PP_BLE_STATE);
    assert(app_ble_request(true,1,rev+1,0,NULL,0,out,&size)==PP_BLE_STATE);
    assert(app_ble_request(true,1,rev,1,NULL,0,out,&size)==PP_BLE_INVALID);
    assert(app_ble_request(true,1,rev,0,out,129,out,&size)==PP_BLE_INVALID);
    for(unsigned i=0;i<8;i++)assert(app_ble_request(true,1,rev,0,(const uint8_t *)"echo",4,out,&size)==0);
    assert(app_ble_request(true,1,rev,0,NULL,0,out,&size)==PP_BLE_BUSY);
    assert(app_ble_take(1,&e) && e.type==APP_BLE_WRITTEN && e.data_size==4 && !memcmp(e.data,"echo",4));
    assert(app_ble_set(1,0,e.data,e.data_size)==APP_OK);
    assert(app_ble_request(false,1,rev,0,NULL,0,out,&size)==0 && size==4 && !memcmp(out,"echo",4));
    assert(app_ble_notifications(rev)==1 && app_ble_notifications(rev)==0);
    app_ble_connection(false);assert(app_ble_take(1,&e) && e.type==APP_BLE_DISCONNECTED && !app_ble_take(1,&e));
    assert(!app_ble_value(rev,0,out,&size));
    app_ble_connection(true);app_ble_cancel();
    assert(!app_ble_schema(NULL,NULL,NULL) && !app_ble_take(1,&e));
    assert(app_ble_set(1,0,NULL,0)==APP_ERR_RUNTIME);
    app_ble_bind(2);assert(app_ble_register(2,&s)==APP_OK);
    uint32_t next;assert(app_ble_schema(NULL,NULL,&next) && next!=rev);
    assert(app_ble_request(true,1,rev,0,NULL,0,out,&size)==PP_BLE_STATE);
    assert(app_ble_take(2,&e) && e.type==APP_BLE_CONNECTED);
    assert(app_ble_unregister(2)==0);assert(app_ble_register(2,&s)==0);
    assert(app_ble_request(true,2,next,0,NULL,0,out,&size)==PP_BLE_STATE);
    assert(app_ble_unregister(2)==0);
    s.uuid[0]=0x7b;s.uuid[1]=0x9d;assert(app_ble_register(2,&s)==APP_ERR_ARGUMENT);
    s.uuid[0]=0x91;s.count=5;assert(app_ble_register(2,&s)==APP_ERR_ARGUMENT);
    s.count=1;s.characteristics[0].max_bytes=129;assert(app_ble_register(2,&s)==APP_ERR_ARGUMENT);
    s.characteristics[0].max_bytes=128;s.characteristics[0].properties=8;assert(app_ble_register(2,&s)==APP_ERR_ARGUMENT);
    assert(changes>8);app_ble_cancel();return 0;
}
