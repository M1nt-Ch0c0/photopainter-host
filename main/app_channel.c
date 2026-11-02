/* SPDX-License-Identifier: MIT */
#include "app_channel.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/* Fixed one-message window keeps memory and application execution bounded.
 * Large copies never retain adapter/ELF pointers after the call returns. */
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t owner,revision,serial,authorized_peer,peer,session,event_session,events;
static app_channel_config_v23_t config;
static uint8_t *rx,*tx;
static uint32_t rx_bytes,tx_bytes;
static void take(void){taskENTER_CRITICAL(&lock);}
static void give(void){taskEXIT_CRITICAL(&lock);}
static void advance(void){if(!++revision)++revision;}
static void disconnect(void){peer=session=rx_bytes=tx_bytes=0;}
static int state(uint32_t gen,uint32_t sid){
    if(!owner || owner!=gen || !config.size)return APP_CHANNEL_STALE;
    if(!session || !peer || authorized_peer!=peer)return APP_CHANNEL_CLOSED;
    return sid==session?APP_CHANNEL_OK:APP_CHANNEL_STALE;
}
void app_channel_cancel(void){
    take();uint8_t *r=rx,*t=tx;rx=tx=NULL;owner=0;advance();disconnect();events=0;
    memset(&config,0,sizeof(config));give();free(r);free(t);
}
void app_channel_bind(uint32_t gen){app_channel_cancel();take();owner=gen;give();}
int app_channel_register(uint32_t gen,const app_channel_config_v23_t *c){
    if(!c || c->size!=sizeof(*c) || !c->max_message_bytes || c->max_message_bytes>APP_CHANNEL_MAX_MESSAGE ||
       !c->max_reply_bytes || c->max_reply_bytes>APP_CHANNEL_MAX_REPLY)return APP_ERR_ARGUMENT;
    uint8_t nonzero=0;for(unsigned i=0;i<16;i++)nonzero|=c->service[i];
    if(!nonzero)return APP_ERR_ARGUMENT;
    take();bool ours=owner && owner==gen,opened=config.size;give();
    if(!ours)return APP_ERR_RUNTIME;
    if(opened)return APP_CHANNEL_BUSY;
    uint8_t *r=malloc(c->max_message_bytes),*t=malloc(c->max_reply_bytes);
    if(!r || !t){free(r);free(t);return APP_ERR_MEMORY;}
    take();if(owner!=gen || config.size){give();free(r);free(t);return APP_CHANNEL_STALE;}
    config=*c;rx=r;tx=t;disconnect();events=0;advance();give();return APP_CHANNEL_OK;
}
int app_channel_close(uint32_t gen){
    take();if(!owner || owner!=gen){give();return APP_ERR_RUNTIME;}
    uint8_t *r=rx,*t=tx;rx=tx=NULL;memset(&config,0,sizeof(config));advance();disconnect();events=0;
    give();free(r);free(t);return APP_CHANNEL_OK;
}
bool app_channel_schema(app_channel_config_v23_t *c,uint32_t *gen,uint32_t *rev){
    take();bool ok=config.size!=0;if(c)*c=config;if(gen)*gen=owner;if(rev)*rev=revision;give();return ok;
}
int app_channel_info(uint32_t gen,app_channel_info_v23_t *i){
    if(!i || i->size!=sizeof(*i))return APP_ERR_ARGUMENT;
    take();if(!owner || owner!=gen){give();return APP_ERR_RUNTIME;}
    *i=(app_channel_info_v23_t){sizeof(*i),config.size?(session?2:1):0,session,config.max_message_bytes,config.max_reply_bytes};
    give();return APP_CHANNEL_OK;
}
int app_channel_connect(uint32_t gen,uint32_t rev,uint32_t who,uint32_t *sid){
    if(!who || !sid)return APP_ERR_ARGUMENT;
    *sid=0;take();int r=APP_CHANNEL_OK;
    if(authorized_peer!=who || !config.size || owner!=gen || revision!=rev)r=APP_CHANNEL_STALE;
    else if(session && peer!=who)r=APP_CHANNEL_BUSY;
    else if(!session){
        if(serial==UINT32_MAX)r=APP_CHANNEL_STALE;
        else {session=++serial;peer=who;events=APP_CHANNEL_CONNECTED;event_session=session;rx_bytes=tx_bytes=0;}
    }
    if(!r)*sid=session;
    give();return r;
}
void app_channel_disconnect(uint32_t who){
    take();if(session && peer==who){event_session=session;disconnect();events=APP_CHANNEL_DISCONNECTED;}give();
}
void app_channel_peer_authorized(uint32_t who,bool authorized){
    if(!who)return;
    take();
    if(authorized){
        if(session && peer!=who){event_session=session;disconnect();events=APP_CHANNEL_DISCONNECTED;}
        authorized_peer=who;
    }else if(authorized_peer==who){
        authorized_peer=0;
        if(session && peer==who){event_session=session;disconnect();events=APP_CHANNEL_DISCONNECTED;}
    }
    give();
}
int app_channel_receive(uint32_t gen,uint32_t rev,uint32_t sid,const void *p,uint32_t n){
    if(!p || !n)return APP_ERR_ARGUMENT;
    take();int r=state(gen,sid);
    if(!r && revision!=rev)r=APP_CHANNEL_STALE;
    if(!r){
        if(n>config.max_message_bytes)r=APP_ERR_ARGUMENT;
        else if(rx_bytes || tx_bytes)r=APP_CHANNEL_AGAIN;
        else {memcpy(rx,p,n);rx_bytes=n;}
    }give();return r;
}
int app_channel_recv(uint32_t gen,uint32_t sid,void *p,uint32_t cap,uint32_t *n){
    if(!p || !n)return APP_ERR_ARGUMENT;
    *n=0;take();int r=state(gen,sid);
    if(!r){
        *n=rx_bytes;
        if(!rx_bytes)r=APP_CHANNEL_AGAIN;
        else if(cap<rx_bytes)r=APP_CHANNEL_TOO_SMALL;
        else {memcpy(p,rx,rx_bytes);rx_bytes=0;}
    }give();return r;
}
int app_channel_send(uint32_t gen,uint32_t sid,const void *p,uint32_t n){
    if(!p || !n)return APP_ERR_ARGUMENT;
    take();int r=state(gen,sid);
    if(!r){
        if(n>config.max_reply_bytes)r=APP_ERR_ARGUMENT;
        else if(tx_bytes)r=APP_CHANNEL_AGAIN;
        else {memcpy(tx,p,n);tx_bytes=n;}
    }give();return r;
}
int app_channel_transmit(uint32_t gen,uint32_t sid,void *p,uint32_t cap,uint32_t *n){
    if(!p || !n)return APP_ERR_ARGUMENT;
    *n=0;take();int r=state(gen,sid);
    if(!r){
        *n=tx_bytes;
        if(!tx_bytes)r=APP_CHANNEL_AGAIN;
        else if(cap<tx_bytes)r=APP_CHANNEL_TOO_SMALL;
        else {memcpy(p,tx,tx_bytes);tx_bytes=0;events|=APP_CHANNEL_TX_READY;}
    }give();return r;
}
bool app_channel_take(uint32_t gen,app_channel_event_v23_t *e){
    take();uint32_t flags=events|(rx_bytes?APP_CHANNEL_RX_READY:0);
    bool ready=owner==gen && config.size && flags;
    if(ready){*e=(app_channel_event_v23_t){sizeof(*e),flags,session?session:event_session,rx_bytes?1:0,tx_bytes?0:1,0};events=0;}
    give();return ready;
}
