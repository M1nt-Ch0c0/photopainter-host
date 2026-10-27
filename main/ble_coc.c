/* SPDX-License-Identifier: MIT
 * One CoC channel. All radio state runs on the NimBLE event queue. APP work
 * stays on App Manager. Length framing also supports CoreBluetooth streams.
 */
#include "ble_coc.h"
#include "app_coc.h"
#include "app_manager.h"
#include "ble_protocol.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_l2cap.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"
#include "os/os_mempool.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define POOL_COUNT 6
#define POOL_BLOCK (APP_COC_SDU_MAX+128)
static struct os_mempool memory_pool;
static struct os_mbuf_pool mbuf_pool;
static os_membuf_t *pool_memory;
static struct ble_npl_event work;
static atomic_bool initialized;
static atomic_uint active_connection; /* GAP handle + 1; zero means absent. */
static struct ble_l2cap_chan *channel;
static uint16_t gap_connection;
static uint32_t generation,revision,sid,rx_sequence,tx_sequence,rx_max,tx_max;
static uint8_t parent_key[32],receive_key[32],send_key[32];
static uint8_t input[APP_COC_SDU_MAX],output[APP_COC_SDU_MAX];
static uint32_t input_used,output_size;
static struct os_mbuf *incoming;
static uint32_t incoming_offset;
static bool receive_armed,stalled,closing;
static uint64_t activity;
static uint64_t now(void){return (uint64_t)esp_timer_get_time()/1000;}
static uint16_t get16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static void put16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static bool equal(const uint8_t *a,const uint8_t *b,size_t n){unsigned v=0;while(n--)v|=*a++^*b++;return v==0;}
static bool tagged(const uint8_t key[32],const char *domain,const uint8_t *p,size_t n,uint8_t tag[32]){
    size_t d=strlen(domain);uint8_t *message=malloc(d+n);
    if(!message)return false;
    memcpy(message,domain,d);memcpy(message+d,p,n);
    bool ok=pp_ble_hmac(key,32,message,d+n,tag);free(message);return ok;
}
void photopainter_coc_wakeup(void){
    if(atomic_load(&initialized))ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&work);
}
bool photopainter_coc_active(uint16_t conn){return atomic_load(&active_connection)==(unsigned)conn+1;}
static void ended(int error){
    if(sid)app_coc_disconnected(revision,sid,error);
    if(incoming)os_mbuf_free_chain(incoming);
    incoming=NULL;channel=NULL;sid=0;input_used=output_size=incoming_offset=0;
    receive_armed=stalled=closing=false;atomic_store(&active_connection,0);
    memset(parent_key,0,32);memset(receive_key,0,32);memset(send_key,0,32);
}
void photopainter_coc_reset(void){ended(BLE_HS_ENOTCONN);}
static void fail(int error){
    if(sid)app_coc_disconnected(revision,sid,error);
    if(channel && !closing){closing=true;(void)ble_l2cap_disconnect(channel);}
}
static int arm_receive(void){
    struct os_mbuf *m=os_mbuf_get_pkthdr(&mbuf_pool,0);
    if(!m)return BLE_HS_ENOMEM;
    int rc=ble_l2cap_recv_ready(channel,m);
    if(rc){os_mbuf_free_chain(m);return rc;}
    receive_armed=true;return 0;
}
static bool consume(void){
    while(incoming && app_coc_rx_space(revision,sid)){
        uint32_t wanted=2;
        if(input_used>=2){
            wanted=2u+get16(input);
            if(wanted<APP_COC_OVERHEAD+1 || wanted>rx_max+APP_COC_OVERHEAD){fail(BLE_HS_EBADDATA);return false;}
        }
        uint32_t left=OS_MBUF_PKTLEN(incoming)-incoming_offset;
        uint32_t n=wanted-input_used;if(n>left)n=left;
        if(n && os_mbuf_copydata(incoming,incoming_offset,n,input+input_used)){fail(BLE_HS_EBADDATA);return false;}
        incoming_offset+=n;input_used+=n;
        if(input_used>=2 && (get16(input)<37 || 2u+get16(input)>rx_max+APP_COC_OVERHEAD)){
            fail(BLE_HS_EBADDATA);return false;
        }
        if(input_used>=2 && input_used==2u+get16(input)){
            uint32_t bytes=input_used-APP_COC_OVERHEAD;uint8_t tag[32];
            if(rx_sequence==UINT32_MAX || pp_ble_u32(input+2)!=rx_sequence+1 ||
               !pp_ble_hmac(receive_key,32,input+2,bytes+4,tag) || !equal(tag,input+6+bytes,32)){
                fail(BLE_HS_EBADDATA);return false;
            }
            if(app_coc_receive(revision,sid,input+6,bytes)!=0){fail(BLE_HS_EBUSY);return false;}
            rx_sequence++;input_used=0;activity=now();
        }
        if(incoming_offset==OS_MBUF_PKTLEN(incoming)){
            os_mbuf_free_chain(incoming);incoming=NULL;incoming_offset=0;
        }
    }
    return true;
}
static void pump(struct ble_npl_event *unused){
    (void)unused;if(!channel || !sid || closing)return;
    uint8_t key[32];bool authorized=photopainter_ble_coc_key(gap_connection,key) && equal(key,parent_key,32);
    memset(key,0,32);
    if(!authorized || !app_coc_valid(revision,sid)){fail(BLE_HS_ENOTCONN);return;}
    if(!app_manager_busy() && now()-activity>60000){fail(BLE_HS_ETIMEOUT);return;}
    if(!consume() || closing)return;
    if(!incoming && !receive_armed && app_coc_rx_space(revision,sid)){
        int rc=arm_receive();if(rc && rc!=BLE_HS_ENOMEM){fail(rc);return;}
    }
    /* Bounded work per radio event; ESTALLED transfers mbuf ownership to NimBLE. */
    for(unsigned batch=0;batch<4 && !stalled;batch++){
        if(!output_size){
            uint32_t bytes=0;
            int rc=app_coc_transmit(revision,sid,output+6,tx_max,&bytes);
            if(rc!=0)break;
            if(tx_sequence==UINT32_MAX){fail(BLE_HS_EBADDATA);return;}
            pp_ble_put32(output+2,++tx_sequence);put16(output,(uint16_t)(bytes+36));
            if(!pp_ble_hmac(send_key,32,output+2,bytes+4,output+6+bytes)){fail(BLE_HS_EUNKNOWN);return;}
            output_size=bytes+APP_COC_OVERHEAD;
        }
        struct os_mbuf *m=os_mbuf_get_pkthdr(&mbuf_pool,0);if(!m)break;
        if(os_mbuf_append(m,output,output_size)){os_mbuf_free_chain(m);break;}
        int rc=ble_l2cap_send(channel,m);
        /* Pinned IDF: EBUSY/EBADDATA reject before consuming; continuation
         * failures free the mbuf internally. Do not double free on ENOMEM. */
        if(rc==BLE_HS_EBUSY){os_mbuf_free_chain(m);break;}
        if(rc==BLE_HS_EBADDATA)os_mbuf_free_chain(m);
        if(rc && rc!=BLE_HS_ESTALLED){fail(rc);return;}
        output_size=0;stalled=rc==BLE_HS_ESTALLED;activity=now();
    }
}
static int coc_event(struct ble_l2cap_event *e,void *arg){
    (void)arg;
    switch(e->type){
    case BLE_L2CAP_EVENT_COC_ACCEPT:
        if(channel || e->accept.peer_sdu_size<128 ||
           !app_coc_registration(&generation,&revision,&rx_max) ||
           !photopainter_ble_coc_key(e->accept.conn_handle,parent_key))return BLE_HS_EAUTHEN;
        channel=e->accept.chan;gap_connection=e->accept.conn_handle;
        if(arm_receive()!=0){ended(BLE_HS_ENOMEM);return BLE_HS_ENOMEM;}
        return 0;
    case BLE_L2CAP_EVENT_COC_CONNECTED:{
        if(e->connect.status && e->connect.conn_handle==gap_connection){ended(e->connect.status);return 0;}
        if(e->connect.chan!=channel)return 0;
        if(e->connect.status){ended(e->connect.status);return 0;}
        struct ble_l2cap_chan_info info;
        if(ble_l2cap_get_chan_info(channel,&info) || info.peer_coc_mtu<128){fail(BLE_HS_EBADDATA);return 0;}
        tx_max=info.peer_coc_mtu-APP_COC_OVERHEAD;
        if(tx_max>APP_COC_SDU_MAX-APP_COC_OVERHEAD)tx_max=APP_COC_SDU_MAX-APP_COC_OVERHEAD;
        sid=app_coc_connected(revision,tx_max);if(!sid){fail(BLE_HS_EBUSY);return 0;}
        rx_sequence=tx_sequence=0;activity=now();
        put16(output,80);memcpy(output+2,"PPC1",4);pp_ble_put32(output+6,generation);pp_ble_put32(output+10,sid);
        put16(output+14,(uint16_t)rx_max);put16(output+16,(uint16_t)tx_max);esp_fill_random(output+18,32);
        if(!tagged(parent_key,"pp-coc-v1/hello",output+2,48,output+50) ||
           !tagged(parent_key,"pp-coc-v1/c2s",output+18,32,receive_key) ||
           !tagged(parent_key,"pp-coc-v1/s2c",output+18,32,send_key)){fail(BLE_HS_EUNKNOWN);return 0;}
        output_size=82;atomic_store(&active_connection,(unsigned)gap_connection+1);photopainter_coc_wakeup();return 0;
    }
    case BLE_L2CAP_EVENT_COC_DISCONNECTED:
        if(e->disconnect.chan==channel)ended(0);
        return 0;
    case BLE_L2CAP_EVENT_COC_DATA_RECEIVED:
        if(e->receive.chan!=channel || incoming || closing){
            os_mbuf_free_chain(e->receive.sdu_rx);if(e->receive.chan==channel)fail(BLE_HS_EBUSY);return 0;
        }
        receive_armed=false;incoming=e->receive.sdu_rx;incoming_offset=0;photopainter_coc_wakeup();return 0;
    case BLE_L2CAP_EVENT_COC_TX_UNSTALLED:
        if(e->tx_unstalled.chan==channel){
            stalled=false;if(e->tx_unstalled.status)fail(e->tx_unstalled.status);else photopainter_coc_wakeup();
        }return 0;
    default:return 0;
    }
}
int photopainter_coc_init(void){
    size_t bytes=OS_MEMPOOL_SIZE(POOL_COUNT,POOL_BLOCK)*sizeof(os_membuf_t);
    pool_memory=malloc(bytes);if(!pool_memory)return BLE_HS_ENOMEM;
    int rc=os_mempool_init(&memory_pool,POOL_COUNT,POOL_BLOCK,pool_memory,"pp_coc");
    if(!rc)rc=os_mbuf_pool_init(&mbuf_pool,&memory_pool,POOL_BLOCK,POOL_COUNT);
    if(rc)return rc;
    ble_npl_event_init(&work,pump,NULL);atomic_store(&initialized,true);return 0;
}
int photopainter_coc_start(void){
    int rc=ble_l2cap_create_server(APP_COC_PSM,APP_COC_SDU_MAX,coc_event,NULL);
    return rc==BLE_HS_EALREADY?0:rc;
}
