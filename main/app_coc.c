/* SPDX-License-Identifier: MIT */
#include "app_coc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t owner,revision,serial,connection,event_session,events,tx_max;
static int event_error;
static app_coc_config_v22_t cfg;
typedef struct {uint8_t *data;uint32_t sizes[APP_COC_SLOTS_MAX],head,count;} fifo;
static fifo rx,tx;
static void take(void){taskENTER_CRITICAL(&lock);}
static void give(void){taskEXIT_CRITICAL(&lock);}
static void advance(void){if(!++revision)++revision;}
static bool valid(uint32_t rev,uint32_t sid){return cfg.size && revision==rev && sid && connection==sid;}
static void drop_connection(void){connection=0;rx.head=rx.count=tx.head=tx.count=0;}
void app_coc_cancel(void){
    take();uint8_t *r=rx.data,*t=tx.data;owner=0;advance();drop_connection();
    memset(&cfg,0,sizeof(cfg));memset(&rx,0,sizeof(rx));memset(&tx,0,sizeof(tx));events=0;give();
    free(r);free(t);photopainter_coc_wakeup();
}
void app_coc_bind(uint32_t gen){app_coc_cancel();take();owner=gen;give();}
int app_coc_open(uint32_t gen,const app_coc_config_v22_t *c){
    if(!c || c->size!=sizeof(*c) || c->rx_sdu_bytes<128 || c->rx_sdu_bytes>APP_COC_SDU_MAX ||
       !c->rx_slots || c->rx_slots>APP_COC_SLOTS_MAX || !c->tx_slots || c->tx_slots>APP_COC_SLOTS_MAX)
        return APP_ERR_ARGUMENT;
    take();bool ours=owner && owner==gen,opened=cfg.size!=0;give();
    if(!ours)return APP_ERR_RUNTIME;
    if(opened)return APP_COC_BUSY;
    uint8_t *r=malloc(c->rx_slots*c->rx_sdu_bytes),*t=malloc(c->tx_slots*APP_COC_SDU_MAX);
    if(!r || !t){free(r);free(t);return APP_ERR_MEMORY;}
    take();if(owner!=gen || cfg.size){give();free(r);free(t);return APP_COC_BUSY;}
    cfg=*c;rx.data=r;tx.data=t;advance();events=0;event_error=0;give();
    photopainter_coc_wakeup();return APP_COC_OK;
}
int app_coc_close(uint32_t gen){
    take();if(!owner || owner!=gen){give();return APP_ERR_RUNTIME;}
    uint8_t *r=rx.data,*t=tx.data;advance();drop_connection();
    memset(&cfg,0,sizeof(cfg));memset(&rx,0,sizeof(rx));memset(&tx,0,sizeof(tx));events=0;give();
    free(r);free(t);photopainter_coc_wakeup();return APP_COC_OK;
}
int app_coc_info(uint32_t gen,app_coc_info_v22_t *out){
    if(!out || out->size!=sizeof(*out))return APP_ERR_ARGUMENT;
    take();if(!owner || owner!=gen){give();return APP_ERR_RUNTIME;}
    *out=(app_coc_info_v22_t){sizeof(*out),cfg.size?(connection?2:1):0,APP_COC_PSM,
        connection,cfg.size?cfg.rx_sdu_bytes-APP_COC_OVERHEAD:0,connection?tx_max:0};give();return 0;
}
bool app_coc_registration(uint32_t *gen,uint32_t *rev,uint32_t *max){
    take();bool ready=owner && cfg.size;
    if(gen)*gen=owner;
    if(rev)*rev=revision;
    if(max)*max=ready?cfg.rx_sdu_bytes-APP_COC_OVERHEAD:0;
    give();return ready;
}
uint32_t app_coc_connected(uint32_t rev,uint32_t max){
    take();if(!cfg.size || rev!=revision || connection || serial==UINT32_MAX || !max){give();return 0;}
    connection=++serial;tx_max=max<APP_COC_SDU_MAX-APP_COC_OVERHEAD?max:APP_COC_SDU_MAX-APP_COC_OVERHEAD;
    rx.head=rx.count=tx.head=tx.count=0;events=APP_COC_CONNECTED;event_session=connection;event_error=0;
    uint32_t sid=connection;give();return sid;
}
void app_coc_disconnected(uint32_t rev,uint32_t sid,int error){
    take();if(valid(rev,sid)){drop_connection();events=APP_COC_DISCONNECTED|(error?APP_COC_ERROR:0);
        event_session=sid;event_error=error;}give();
}
bool app_coc_valid(uint32_t rev,uint32_t sid){take();bool ok=valid(rev,sid);give();return ok;}
bool app_coc_rx_space(uint32_t rev,uint32_t sid){take();bool ok=valid(rev,sid)&&rx.count<cfg.rx_slots;give();return ok;}
static void put(fifo *q,uint32_t slots,uint32_t stride,const void *p,uint32_t n){
    uint32_t i=(q->head+q->count)%slots;memcpy(q->data+i*stride,p,n);q->sizes[i]=n;q->count++;
}
static int get(fifo *q,uint32_t slots,uint32_t stride,void *p,uint32_t cap,uint32_t *n){
    *n=0;if(!q->count)return APP_COC_AGAIN;*n=q->sizes[q->head];
    if(cap<*n)return APP_COC_TOO_SMALL;
    memcpy(p,q->data+q->head*stride,*n);q->head=(q->head+1)%slots;q->count--;return 0;
}
int app_coc_receive(uint32_t rev,uint32_t sid,const void *p,uint32_t n){
    take();int r=APP_COC_CLOSED;
    if(valid(rev,sid)){
        if(!p || !n || n>cfg.rx_sdu_bytes-APP_COC_OVERHEAD)r=APP_ERR_ARGUMENT;
        else if(rx.count==cfg.rx_slots)r=APP_COC_AGAIN;
        else {put(&rx,cfg.rx_slots,cfg.rx_sdu_bytes,p,n);r=0;}
    }give();return r;
}
int app_coc_recv(uint32_t gen,uint32_t sid,void *p,uint32_t cap,uint32_t *n){
    if(!p || !n)return APP_ERR_ARGUMENT;
    *n=0;take();int r;
    if(!owner || gen!=owner)r=APP_ERR_RUNTIME;
    else if(!connection)r=APP_COC_CLOSED;
    else if(sid!=connection)r=APP_COC_STALE;
    else r=get(&rx,cfg.rx_slots,cfg.rx_sdu_bytes,p,cap,n);
    give();if(!r)photopainter_coc_wakeup();return r;
}
int app_coc_send(uint32_t gen,uint32_t sid,const void *p,uint32_t n){
    if(!p || !n)return APP_ERR_ARGUMENT;
    take();int r;
    if(!owner || gen!=owner)r=APP_ERR_RUNTIME;
    else if(!connection)r=APP_COC_CLOSED;
    else if(sid!=connection)r=APP_COC_STALE;
    else if(n>tx_max)r=APP_ERR_ARGUMENT;
    else if(tx.count==cfg.tx_slots)r=APP_COC_AGAIN;
    else {put(&tx,cfg.tx_slots,APP_COC_SDU_MAX,p,n);r=0;}
    give();if(!r)photopainter_coc_wakeup();return r;
}
int app_coc_transmit(uint32_t rev,uint32_t sid,void *p,uint32_t cap,uint32_t *n){
    take();int r=valid(rev,sid)?get(&tx,cfg.tx_slots,APP_COC_SDU_MAX,p,cap,n):APP_COC_CLOSED;
    if(!r)events|=APP_COC_TX_READY;
    give();return r;
}
bool app_coc_take(uint32_t gen,app_coc_event_v22_t *e){
    take();uint32_t flags=events|(rx.count?APP_COC_RX_READY:0);
    bool ready=cfg.size && owner==gen && flags;
    if(ready){*e=(app_coc_event_v22_t){sizeof(*e),flags,connection?connection:event_session,
        rx.count,cfg.tx_slots-tx.count,event_error};events=0;event_error=0;}
    give();return ready;
}
