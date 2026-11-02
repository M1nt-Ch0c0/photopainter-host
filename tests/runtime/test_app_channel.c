/* SPDX-License-Identifier: MIT */
#include "app_channel.h"
#include <assert.h>
#include <string.h>
int main(void){
    app_channel_config_v23_t c={.size=sizeof(c),.service={1},.max_message_bytes=4096,.max_reply_bytes=64};
    app_channel_event_v23_t e;app_channel_info_v23_t info={.size=sizeof(info)};
    uint32_t gen,rev,sid,n;uint8_t in[4096],out[4096];memset(in,0x42,sizeof(in));
    assert(app_channel_register(7,&c)==APP_ERR_RUNTIME);
    app_channel_bind(7);c.max_message_bytes=4097;assert(app_channel_register(7,&c)==APP_ERR_ARGUMENT);c.max_message_bytes=4096;
    assert(app_channel_register(7,&c)==0 && app_channel_register(7,&c)==APP_CHANNEL_BUSY);
    assert(app_channel_schema(NULL,&gen,&rev)&&gen==7);
    assert(app_channel_connect(7,rev,100,&sid)==APP_CHANNEL_STALE);
    app_channel_peer_authorized(100,true);assert(app_channel_connect(7,rev,100,&sid)==0 && sid);
    uint32_t old=sid,again;assert(app_channel_connect(7,rev,100,&again)==0&&again==sid);
    assert(app_channel_connect(7,rev,101,&again)==APP_CHANNEL_STALE);
    assert(app_channel_take(7,&e)&&e.type==APP_CHANNEL_CONNECTED&&e.session==sid);
    assert(app_channel_receive(7,rev+1,sid,in,4096)==APP_CHANNEL_STALE);
    assert(app_channel_receive(7,rev,sid,in,4096)==0);in[0]=0;
    assert(app_channel_receive(7,rev,sid,in,1)==APP_CHANNEL_AGAIN);
    assert(app_channel_recv(7,sid,out,4095,&n)==APP_CHANNEL_TOO_SMALL&&n==4096);
    assert(app_channel_take(7,&e)&&(e.type&APP_CHANNEL_RX_READY));
    assert(app_channel_recv(7,sid,out,sizeof(out),&n)==0&&n==4096&&out[0]==0x42);
    assert(app_channel_send(7,sid,in,65)==APP_ERR_ARGUMENT);
    assert(app_channel_send(7,sid,in,64)==0);in[1]=0;
    assert(app_channel_send(7,sid,in,1)==APP_CHANNEL_AGAIN);
    assert(app_channel_receive(7,rev,sid,in,1)==APP_CHANNEL_AGAIN);
    assert(app_channel_transmit(7,sid,out,63,&n)==APP_CHANNEL_TOO_SMALL&&n==64);
    assert(app_channel_transmit(7,sid,out,sizeof(out),&n)==0&&n==64&&out[1]==0x42);
    assert(app_channel_take(7,&e)&&(e.type&APP_CHANNEL_TX_READY));
    app_channel_disconnect(101);assert(app_channel_info(7,&info)==0&&info.session==sid);
    app_channel_disconnect(100);assert(app_channel_take(7,&e)&&e.type==APP_CHANNEL_DISCONNECTED&&e.session==sid);
    assert(app_channel_recv(7,sid,out,sizeof(out),&n)==APP_CHANNEL_CLOSED);
    app_channel_peer_authorized(101,true);assert(app_channel_connect(7,rev,101,&sid)==0&&sid!=old);
    assert(app_channel_send(7,old,in,1)==APP_CHANNEL_STALE);
    assert(app_channel_receive(7,rev,sid,in,1)==0);
    app_channel_bind(8);assert(!app_channel_schema(NULL,NULL,NULL));
    assert(app_channel_recv(7,sid,out,sizeof(out),&n)==APP_CHANNEL_STALE);
    assert(app_channel_register(8,&c)==0);uint32_t next_rev;assert(app_channel_schema(NULL,NULL,&next_rev)&&next_rev!=rev);
    assert(app_channel_connect(8,rev,101,&sid)==APP_CHANNEL_STALE);
    assert(app_channel_connect(8,next_rev,101,&sid)==0);
    assert(app_channel_recv(8,sid,out,sizeof(out),&n)==APP_CHANNEL_AGAIN);
    app_channel_peer_authorized(101,false);
    assert(app_channel_connect(8,next_rev,101,&again)==APP_CHANNEL_STALE);
    assert(app_channel_recv(8,sid,out,sizeof(out),&n)==APP_CHANNEL_CLOSED);
    app_channel_peer_authorized(102,true);assert(app_channel_connect(8,next_rev,102,&again)==0&&again!=sid);
    assert(app_channel_close(7)==APP_ERR_RUNTIME);assert(app_channel_close(8)==0);
    app_channel_cancel();return 0;
}
