#include "app_ble.h"
#include "ble_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
static app_ble_service_v2_t schema;
static uint32_t owner,revision,dirty;
static bool registered,authenticated;
static uint8_t values[4][128];
static uint32_t lengths[4];
static app_ble_event_v2_t events[8];
static unsigned head,count;
static void enter(void){taskENTER_CRITICAL(&mutex);}
static void leave(void){taskEXIT_CRITICAL(&mutex);}
static void next_revision(void){if(!++revision)++revision;}
static void clear(void) {
    registered=false;dirty=head=count=0;next_revision();
    memset(&schema,0,sizeof(schema));memset(values,0,sizeof(values));memset(lengths,0,sizeof(lengths));
    memset(events,0,sizeof(events));
}
static bool uuid_valid(const uint8_t *uuid) {
    static const uint8_t sig_suffix[12]={0,0,0x10,0,0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb};
    unsigned nonzero=0;for(unsigned i=0;i<16;i++)nonzero|=uuid[i];
    return nonzero && !(uuid[0]==0x7b && uuid[1]==0x9d) && memcmp(uuid+4,sig_suffix,12)!=0;
}
static bool valid(const app_ble_service_v2_t *s) {
    if(!s || s->size!=sizeof(*s) || !s->count || s->count>4 || !uuid_valid(s->uuid))return false;
    for(unsigned i=0;i<s->count;i++) {
        const app_ble_characteristic_v2_t *c=&s->characteristics[i];
        if(!uuid_valid(c->uuid) || !memcmp(c->uuid,s->uuid,16) || !c->max_bytes || c->max_bytes>128 ||
           !(c->properties&(APP_BLE_READ|APP_BLE_WRITE)) || (c->properties&~7u) ||
           ((c->properties&APP_BLE_NOTIFY) && !(c->properties&APP_BLE_READ)))return false;
        for(unsigned j=0;j<i;j++)if(!memcmp(c->uuid,s->characteristics[j].uuid,16))return false;
    }
    return true;
}
static bool enqueue(uint32_t type,uint32_t index,const uint8_t *data,size_t size) {
    if(count==8)return false;
    app_ble_event_v2_t *e=&events[(head+count)%8];
    memset(e,0,sizeof(*e));e->type=type;e->characteristic=index;e->data_size=(uint32_t)size;
    if(size)memcpy(e->data,data,size);
    count++;return true;
}
void app_ble_bind(uint32_t generation) {
    enter();clear();owner=generation;leave();photopainter_ble_app_changed();
}
void app_ble_cancel(void) {
    enter();clear();owner=0;leave();photopainter_ble_app_changed();
}
int app_ble_register(uint32_t generation,const app_ble_service_v2_t *s) {
    if(!valid(s))return APP_ERR_ARGUMENT;
    enter();
    if(!owner || owner!=generation || registered){leave();return APP_ERR_RUNTIME;}
    schema=*s;registered=true;next_revision();
    if(authenticated)(void)enqueue(APP_BLE_CONNECTED,0,NULL,0);
    leave();photopainter_ble_app_changed();return APP_OK;
}
int app_ble_unregister(uint32_t generation) {
    enter();if(!owner || generation!=owner){leave();return APP_ERR_RUNTIME;}
    clear();leave();photopainter_ble_app_changed();return APP_OK;
}
int app_ble_set(uint32_t generation,uint32_t index,const uint8_t *data,uint32_t bytes) {
    enter();
    if(!registered || owner!=generation){leave();return APP_ERR_RUNTIME;}
    if(index>=schema.count || bytes>schema.characteristics[index].max_bytes || (!data && bytes)){leave();return APP_ERR_ARGUMENT;}
    if(bytes)memcpy(values[index],data,bytes);
    lengths[index]=bytes;
    if(schema.characteristics[index].properties&APP_BLE_NOTIFY)dirty|=1u<<index;
    leave();photopainter_ble_app_changed();return APP_OK;
}
bool app_ble_take(uint32_t generation,app_ble_event_v2_t *event) {
    enter();bool ready=registered && owner==generation && count;
    if(ready){*event=events[head];head=(head+1)%8;count--;}
    leave();return ready;
}
void app_ble_connection(bool value) {
    enter();
    if(value!=authenticated && registered) {
        /* Connection boundaries invalidate undelivered writes from that peer.
         * Reserve delivery of the boundary even when the queue was full. */
        head=count=0;(void)enqueue(value?APP_BLE_CONNECTED:APP_BLE_DISCONNECTED,0,NULL,0);
    }
    authenticated=value;leave();
}
void app_ble_error(uint32_t rev) {
    enter();if(registered && rev==revision){head=count=0;(void)enqueue(APP_BLE_ERROR,0,NULL,0);}leave();
}
bool app_ble_schema(app_ble_service_v2_t *out,uint32_t *generation,uint32_t *rev) {
    enter();bool ready=registered;
    if(out)*out=schema;
    if(generation)*generation=owner;
    if(rev)*rev=revision;
    leave();return ready;
}
int app_ble_request(bool write,uint32_t generation,uint32_t rev,uint32_t index,
                    const uint8_t *data,size_t size,uint8_t *out,size_t *out_size) {
    *out_size=0;enter();
    if(!authenticated){leave();return PP_BLE_UNAUTHORIZED;}
    if(!registered || owner!=generation || revision!=rev){leave();return PP_BLE_STATE;}
    if(index>=schema.count){leave();return PP_BLE_INVALID;}
    const app_ble_characteristic_v2_t *c=&schema.characteristics[index];
    if(write) {
        if(!(c->properties&APP_BLE_WRITE) || size>c->max_bytes || (!data && size)){leave();return PP_BLE_INVALID;}
        if(!enqueue(APP_BLE_WRITTEN,index,data,size)){leave();return PP_BLE_BUSY;}
    } else {
        if(!(c->properties&APP_BLE_READ) || size){leave();return PP_BLE_INVALID;}
        memcpy(out,values[index],lengths[index]);*out_size=lengths[index];
    }
    leave();return PP_BLE_OK;
}
bool app_ble_value(uint32_t rev,uint32_t index,uint8_t out[128],size_t *size) {
    enter();bool ready=authenticated && registered && rev==revision && index<schema.count &&
        (schema.characteristics[index].properties&APP_BLE_READ);
    if(ready){*size=lengths[index];memcpy(out,values[index],*size);}
    leave();return ready;
}
uint32_t app_ble_notifications(uint32_t rev) {
    enter();uint32_t bits=0;
    if(authenticated && registered && rev==revision){bits=dirty;dirty=0;}
    leave();return bits;
}
