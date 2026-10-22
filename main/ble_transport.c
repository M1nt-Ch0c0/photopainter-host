/* Radio callbacks only copy bounded frames. The transport worker waits for the
 * existing App Manager; neither callback nor worker calls ELF/Flash/display. */
#include "ble_transport.h"
#include "ble_protocol.h"
#include "app_manager.h"
#include "app_ble.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "psa/crypto.h"
#include <string.h>
#include <stdatomic.h>

/* UUID in conventional notation: 7b9d0001-6c82-4f31-9a48-6f5061696e74.
 * Characteristics replace 0001 with 0002(info), 0003(challenge), 0004(RX), 0005(TX). */
#define UUID_BYTES(x) 0x74,0x6e,0x69,0x61,0x50,0x6f,0x48,0x9a,0x31,0x4f,0x82,0x6c,x,0,0x9d,0x7b
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(UUID_BYTES(1));
static const ble_uuid128_t info_uuid=BLE_UUID128_INIT(UUID_BYTES(2));
static const ble_uuid128_t challenge_uuid=BLE_UUID128_INIT(UUID_BYTES(3));
static const ble_uuid128_t rx_uuid=BLE_UUID128_INIT(UUID_BYTES(4));
static const ble_uuid128_t tx_uuid=BLE_UUID128_INIT(UUID_BYTES(5));
static SemaphoreHandle_t lock;
static uint8_t info[20],ssid_hash[8],challenge[32],response[512],own_addr_type;
static size_t response_size;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static uint32_t epoch,challenge_epoch;
static bool synced,pending,processing,authenticated;
static atomic_bool started;
static uint32_t fragment_target,request_target;
static pp_ble_fragments fragments;
static pp_ble_session *session;
static char device_token[129];
static struct ble_npl_event advertise_event;
static struct ble_npl_event app_event;
static ble_uuid128_t app_uuids[5];
static struct ble_gatt_svc_def app_services[2];
static struct ble_gatt_chr_def app_characteristics[5];
static uint16_t app_handles[4];
static uint32_t published_revision;
static uint32_t subscribed;
static int access_gatt(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);

static void take(void) {xSemaphoreTake(lock,portMAX_DELAY);}
static void give(void) {xSemaphoreGive(lock);}
static uint64_t now_ms(void) {return (uint64_t)esp_timer_get_time()/1000;}
static int gap_event(struct ble_gap_event *,void *);

void photopainter_ble_app_changed(void) {
    if(!atomic_load(&started))return;
    take();if(synced)ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&app_event);give();
}
static void app_changed(struct ble_npl_event *unused) {
    (void)unused;
    take();bool ready=synced;uint16_t conn=connection;bool authorized=authenticated;
    uint32_t subscriptions=subscribed;give();
    if(!ready)return;
    app_ble_service_v2_t definition;uint32_t revision;
    bool present=app_ble_schema(&definition,NULL,&revision);
    if(published_revision && (!present || revision!=published_revision)) {
        int rc=ble_gatts_delete_svc(&app_uuids[0].u);
        if(rc && rc!=BLE_HS_ENOENT){app_ble_error(revision);return;}
        take();subscribed=0;give();subscriptions=0;
        published_revision=0;
    }
    if(present && !published_revision) {
        memset(app_services,0,sizeof(app_services));memset(app_characteristics,0,sizeof(app_characteristics));
        memset(app_handles,0,sizeof(app_handles));
        for(unsigned i=0;i<=definition.count;i++) {
            const uint8_t *uuid=i?definition.characteristics[i-1].uuid:definition.uuid;
            app_uuids[i].u.type=BLE_UUID_TYPE_128;
            for(unsigned j=0;j<16;j++)app_uuids[i].value[j]=uuid[15-j];
        }
        app_services[0]=(struct ble_gatt_svc_def){.type=BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid=&app_uuids[0].u,.characteristics=app_characteristics};
        for(unsigned i=0;i<definition.count;i++) {
            uint32_t flags=definition.characteristics[i].properties;
            app_characteristics[i]=(struct ble_gatt_chr_def){.uuid=&app_uuids[i+1].u,
                .access_cb=access_gatt,.arg=(void *)(uintptr_t)(256+i),.val_handle=&app_handles[i],
                .flags=((flags&APP_BLE_READ)?BLE_GATT_CHR_F_READ:0)|
                    ((flags&APP_BLE_WRITE)?BLE_GATT_CHR_F_WRITE:0)|
                    ((flags&APP_BLE_NOTIFY)?BLE_GATT_CHR_F_NOTIFY:0)};
        }
        int rc=ble_gatts_add_dynamic_svcs(app_services);
        if(rc){
            /* Keep definitions alive if the stack cannot clean up a partial
             * registration. Retry deletion before ever reusing these arrays. */
            int cleanup=ble_gatts_delete_svc(&app_uuids[0].u);
            if(cleanup && cleanup!=BLE_HS_ENOENT)published_revision=revision;
            ESP_LOGE("ble","application service registration failed: %d",rc);
            app_ble_error(revision);return;
        }
        published_revision=revision;
    }
    if(present && authorized && conn!=BLE_HS_CONN_HANDLE_NONE) {
        uint32_t dirty=app_ble_notifications(published_revision)&subscriptions;
        for(unsigned i=0;i<definition.count;i++)if(dirty&(1u<<i)) {
            uint8_t value[128];size_t size;
            if(app_ble_value(published_revision,i,value,&size)) {
                /* Notifications are advisory and bounded by the negotiated
                 * MTU. Authenticated APP_READ retrieves the complete value. */
                size_t max=ble_att_mtu(conn)-3u;if(size>max)size=max;
                struct os_mbuf *om=ble_hs_mbuf_from_flat(value,size);
                if(om)(void)ble_gatts_notify_custom(conn,app_handles[i],om);
            }
        }
    }
}

static void advertise(struct ble_npl_event *unused) {
    (void)unused;
    uint8_t data[22]={0xff,0xff};
    take();bool ready=synced && connection==BLE_HS_CONN_HANDLE_NONE;
    memcpy(data+2,info,20);give();
    if(!ready) return;
    if(ble_gap_adv_active()) (void)ble_gap_adv_stop();
    struct ble_hs_adv_fields adv={0},scan={0};
    adv.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128=(ble_uuid128_t *)&service_uuid;adv.num_uuids128=1;adv.uuids128_is_complete=1;
    scan.mfg_data=data;scan.mfg_data_len=sizeof(data);
    scan.name=(uint8_t *)"PPBLE";scan.name_len=5;scan.name_is_complete=1;
    int rc=ble_gap_adv_set_fields(&adv);
    if(!rc) rc=ble_gap_adv_rsp_set_fields(&scan);
    struct ble_gap_adv_params params={0};
    params.conn_mode=BLE_GAP_CONN_MODE_UND;params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    params.itvl_min=320;params.itvl_max=480; /* 200–300 ms */
    if(!rc) rc=ble_gap_adv_start(own_addr_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
    if(rc) ESP_LOGE("ble","advertising failed: %d",rc);
}
static void schedule_advertise(void) {
    if(synced) ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&advertise_event);
}
void photopainter_ble_network(uint8_t state,uint32_t ip,const char *ssid) {
    if(!lock) return;
    uint8_t hash[32];bool have_hash=ssid && pp_ble_sha256((const uint8_t *)ssid,strlen(ssid),hash);
    take();
    if(ssid) {memset(ssid_hash,0,8);if(have_hash) memcpy(ssid_hash,hash,8);}
    info[1]=state;
    memset(info+8,0,12);
    if(state==2) memcpy(info+8,&ip,4);
    if(state==1 || state==2) memcpy(info+12,ssid_hash,8);
    schedule_advertise();give();
}
static int access_gatt(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg) {
    (void)attr;uintptr_t which=(uintptr_t)arg;int result=0;
    take();
    if(conn!=connection) {give();return BLE_ATT_ERR_UNLIKELY;}
    uint32_t target=0;
    if(which>=256) {
        if(!authenticated){give();return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;}
        target=(uint32_t)(which-256)+1;
        if(ctx->op==BLE_GATT_ACCESS_OP_READ_CHR) {
            uint8_t value[128];size_t size;
            result=app_ble_value(published_revision,target-1,value,&size)?
                (os_mbuf_append(ctx->om,value,size)?BLE_ATT_ERR_INSUFFICIENT_RES:0):BLE_ATT_ERR_UNLIKELY;
            give();return result;
        }
        which=4; /* Same signed, fragmented protocol; bound to this characteristic. */
    }
    if(which==2) result=os_mbuf_append(ctx->om,info,sizeof(info))?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    else if(which==3) {
        if(challenge_epoch!=epoch) result=BLE_ATT_ERR_UNLIKELY;
        else result=os_mbuf_append(ctx->om,challenge,sizeof(challenge))?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    } else if(which==5) {
        /* A one-byte zero is a transport pending marker, never a successful
         * authenticated response. Avoid platform-specific ATT error caching. */
        if(processing || !response_size) {uint8_t waiting=0;result=os_mbuf_append(ctx->om,&waiting,1)?BLE_ATT_ERR_INSUFFICIENT_RES:0;}
        else result=os_mbuf_append(ctx->om,response,response_size)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    } else if(which==4) {
        uint8_t bytes[512];uint16_t size=OS_MBUF_PKTLEN(ctx->om);
        if(processing) result=BLE_ATT_ERR_INSUFFICIENT_RES;
        else if(size>sizeof(bytes) || os_mbuf_copydata(ctx->om,0,size,bytes)) result=BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        else {
            if(!fragments.received)fragment_target=target;
            int complete=fragment_target==target?pp_ble_fragment(&fragments,bytes,size):-1;
            if(complete>0 && target && fragments.data[0]!=PP_BLE_APP_WRITE)complete=-1;
            if(complete<0) result=BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            else if(complete) {pending=processing=true;response_size=0;}
            if(complete<0)fragments.received=fragments.total=0;
        }
    } else result=BLE_ATT_ERR_UNLIKELY;
    give();return result;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&info_uuid.u,.access_cb=access_gatt,.arg=(void *)2,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=&challenge_uuid.u,.access_cb=access_gatt,.arg=(void *)3,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=&rx_uuid.u,.access_cb=access_gatt,.arg=(void *)4,.flags=BLE_GATT_CHR_F_WRITE},
        {.uuid=&tx_uuid.u,.access_cb=access_gatt,.arg=(void *)5,.flags=BLE_GATT_CHR_F_READ},
        {0}}},
    {0}
};
static int gap_event(struct ble_gap_event *event,void *arg) {
    (void)arg;
    if(event->type==BLE_GAP_EVENT_CONNECT || event->type==BLE_GAP_EVENT_DISCONNECT) {
        take();epoch++;challenge_epoch=0;pending=processing=false;response_size=0;
        authenticated=false;app_ble_connection(false);
        subscribed=0;
        memset(&fragments,0,sizeof(fragments));memset(challenge,0,sizeof(challenge));
        connection=event->type==BLE_GAP_EVENT_CONNECT && !event->connect.status?
            event->connect.conn_handle:BLE_HS_CONN_HANDLE_NONE;
        schedule_advertise();give();
    } else if(event->type==BLE_GAP_EVENT_ADV_COMPLETE) {
        take();schedule_advertise();give();
    } else if(event->type==BLE_GAP_EVENT_SUBSCRIBE) {
        take();
        if(connection==event->subscribe.conn_handle && published_revision) {
            for(unsigned i=0;i<4;i++)if(app_handles[i] && event->subscribe.attr_handle==app_handles[i]) {
                if(event->subscribe.cur_notify)subscribed|=1u<<i;
                else subscribed&=~(1u<<i);
            }
        }
        give();
    }
    return 0;
}
static int map_error(esp_err_t err,bool stage) {
    if(err==ESP_OK) return PP_BLE_OK;
    if(err==ESP_ERR_NO_MEM) return PP_BLE_MEMORY;
    if(err==ESP_ERR_INVALID_STATE || err==ESP_ERR_NOT_FOUND) return PP_BLE_STATE;
    if(err==ESP_ERR_INVALID_ARG || err==ESP_ERR_INVALID_SIZE) return stage?PP_BLE_PACKAGE:PP_BLE_INVALID;
    return PP_BLE_STORAGE;
}
static void *allocate_package(size_t size) {return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
static int stage(const char *id,const uint8_t *data,size_t size) {return map_error(app_manager_stage(id,data,size),true);}
static int manage(uint8_t op,const char *id) {
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if(op==PP_BLE_ACTIVATE || op==PP_BLE_SWITCH) err=app_manager_select_from(id,op==PP_BLE_ACTIVATE,"ble");
    else if(op==PP_BLE_ROLLBACK) err=app_manager_rollback_from(id,"ble");
    else if(op==PP_BLE_REMOVE) err=app_manager_remove(id);
    return map_error(err,false);
}
static int app_operation(uint8_t op,const uint8_t *p,size_t n,uint8_t *out,size_t *size) {
    uint32_t index=pp_ble_u32(p+8);
    if(request_target && (op!=PP_BLE_APP_WRITE || index!=request_target-1))return PP_BLE_INVALID;
    int result=app_ble_request(op==PP_BLE_APP_WRITE,pp_ble_u32(p),pp_ble_u32(p+4),index,p+12,n-12,out,size);
    if(result==PP_BLE_OK && op==PP_BLE_APP_WRITE)result=map_error(app_manager_ble_flush(pp_ble_u32(p)),false);
    return result;
}
static const pp_ble_ops operations={app_manager_acquire,app_manager_release,allocate_package,
    heap_caps_free,stage,manage,app_manager_status,app_operation};
static void transport_worker(void *arg) {
    (void)arg;uint32_t local_epoch=0;
    for(;;) {
        uint8_t request[512],out[512],current_info[20];size_t size=0;
        take();uint32_t current_epoch=epoch;uint16_t conn=connection;
        memcpy(current_info,info,20);
        if(pending) {size=fragments.total;request_target=fragment_target;memcpy(request,fragments.data,size);pending=false;fragments.received=fragments.total=0;}
        give();
        if(local_epoch!=current_epoch) {
            pp_ble_close(session);local_epoch=current_epoch;
            if(conn!=BLE_HS_CONN_HANDLE_NONE) {
                uint8_t nonce[32];esp_fill_random(nonce,sizeof(nonce));
                pp_ble_open(session,&operations,current_info+2,nonce,now_ms());
                take();if(epoch==local_epoch) {memcpy(challenge,nonce,32);challenge_epoch=epoch;}give();
            }
        }
        if(size && conn!=BLE_HS_CONN_HANDLE_NONE) {
            size_t n=pp_ble_request(session,device_token,current_info,request,size,now_ms(),out);
            /* A valid manager job can take 650 seconds; idle timeout starts
             * after it finishes, never while borrowed input is still in use. */
            if(n>=39 && session->authenticated) session->touched=now_ms();
            take();if(epoch==local_epoch) {
                memcpy(response,out,n);response_size=n;processing=false;
                if(authenticated!=session->authenticated) {
                    authenticated=session->authenticated;app_ble_connection(authenticated);
                }
            }give();
        }
        if(conn!=BLE_HS_CONN_HANDLE_NONE && pp_ble_expire(session,now_ms()))
            (void)ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
static void on_sync(void) {
    int rc=ble_hs_util_ensure_addr(0);
    if(!rc) rc=ble_hs_id_infer_auto(0,&own_addr_type);
    if(rc) {ESP_LOGE("ble","address setup failed: %d",rc);return;}
    take();synced=true;schedule_advertise();give();photopainter_ble_app_changed();
}
static void on_reset(int reason) {
    take();synced=false;connection=BLE_HS_CONN_HANDLE_NONE;epoch++;
    authenticated=false;app_ble_connection(false);
    subscribed=0;
    challenge_epoch=0;pending=processing=false;response_size=0;give();
    ESP_LOGW("ble","host reset: %d",reason);
}
static void host_worker(void *arg) {(void)arg;nimble_port_run();nimble_port_freertos_deinit();}
esp_err_t photopainter_ble_start(const char *token) {
    if(started) return ESP_ERR_INVALID_STATE;
    if(!token || strnlen(token,129)>128) return ESP_ERR_INVALID_ARG;
    if(psa_crypto_init()!=PSA_SUCCESS) return ESP_FAIL;
    lock=xSemaphoreCreateMutex();
    session=heap_caps_calloc(1,sizeof(*session),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!lock || !session) return ESP_ERR_NO_MEM;
    info[0]=PP_BLE_VERSION;
    esp_err_t err=esp_read_mac(info+2,ESP_MAC_WIFI_STA);
    if(err!=ESP_OK) return err;
    memcpy(device_token,token,strlen(token)+1);
    err=nimble_port_init();if(err!=ESP_OK) return err;
    ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("PhotoPainter");
    ble_npl_event_init(&advertise_event,advertise,NULL);
    ble_npl_event_init(&app_event,app_changed,NULL);
    ble_hs_cfg.sync_cb=on_sync;ble_hs_cfg.reset_cb=on_reset;
    if(ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) return ESP_FAIL;
    if(xTaskCreate(transport_worker,"ble_transfer",6144,NULL,4,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    started=true;nimble_port_freertos_init(host_worker);
    return ESP_OK;
}
