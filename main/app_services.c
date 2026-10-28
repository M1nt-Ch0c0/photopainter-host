/* SPDX-License-Identifier: MIT */
#include "app_services.h"
#include "app_ble.h"
#include "app_coc.h"
#include "app_manager.h"
#include "photopainter_board.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CACHE_APP_MAX (512u*1024u)
#define CACHE_TOTAL_MAX (1024u*1024u)
typedef struct {char id[32];uint8_t *data;uint32_t bytes,version;uint64_t used;} cache_t;
static cache_t cache[5];
static char current[32];
static uint32_t generation;
static const app_context_v2_t *context;
static bool displayed,display_called,borrowed,connected;
static uint32_t timer_id,timer_interval;
static uint64_t timer_next;
static esp_timer_handle_t watchdog;
static void stalled(void *arg) {
    (void)arg;ESP_LOGE("app_watchdog","application transaction timed out; rebooting without unloading live code");
    esp_restart();
}
static esp_err_t arm(uint64_t ms) {
    if(!watchdog)return ESP_ERR_INVALID_STATE;
    esp_timer_stop(watchdog);
    return esp_timer_start_once(watchdog,ms*1000);
}
esp_err_t app_services_init(void) {
    if(watchdog)return ESP_OK;
    const esp_timer_create_args_t args={.callback=stalled,.name="app_watchdog"};
    return esp_timer_create(&args,&watchdog);
}
void app_services_open(const char *id,uint32_t gen) {
    strncpy(current,id,sizeof(current)-1);current[sizeof(current)-1]=0;
    generation=gen;timer_interval=0;context=NULL;
    app_ble_bind(gen);app_coc_bind(gen);
}
void app_services_close(void) {app_coc_cancel();app_ble_cancel();timer_interval=0;context=NULL;current[0]=0;}
esp_err_t app_services_begin(const app_context_v2_t *c) {
    if(context||!c||c->generation!=generation)return ESP_ERR_INVALID_STATE;
    displayed=display_called=borrowed=false;
    esp_err_t e=arm(5000);if(e==ESP_OK)context=c;
    return e;
}
esp_err_t app_services_legacy_begin(void){
    if(context||!app_manager_display_begin())return ESP_ERR_INVALID_STATE;
    esp_err_t result=arm(650000);if(result!=ESP_OK)app_manager_display_end();return result;
}
void app_services_end(void) {if(watchdog)esp_timer_stop(watchdog);context=NULL;borrowed=false;app_manager_display_end();}
bool app_services_display_completed(void) {return displayed;}
static bool active(void) {return context && current[0] && context->generation==generation;}
int32_t app_host_display_v2(const uint8_t *pixels,uint32_t bytes) {
    if(!active()||context->event==APP_EVENT_STOP||display_called)return APP_ERR_RUNTIME;
    if(!pixels||bytes!=APP_FRAME_BYTES)return APP_ERR_ARGUMENT;
    for(uint32_t i=0;i<bytes;i++)if(pixels[i]>APP_GREEN)return APP_ERR_COLOR;
    if(!app_manager_display_begin())return APP_ERR_RUNTIME;
    if(arm(650000)!=ESP_OK){app_manager_display_end();return APP_ERR_RUNTIME;}
    display_called=true;
    int r=photopainter_board_display(pixels,bytes);
    app_manager_display_end();
    displayed=r==APP_OK;
    /* Failure to re-arm must not leave a live application without a watchdog. */
    if(arm(5000)!=ESP_OK)esp_restart();
    return r;
}
uint64_t app_host_monotonic_ms_v2(void) {return active() ? esp_timer_get_time()/1000u : 0;}
uint64_t app_host_wall_time_v2(void) {
    if(!active())return 0;
    time_t t=time(NULL);return t>=1704067200 ? (uint64_t)t : 0;
}
static int find(const char *id){for(int i=0;i<5;i++)if(!strcmp(cache[i].id,id))return i;return -1;}
static void drop(int i){free(cache[i].data);memset(&cache[i],0,sizeof(cache[i]));}
void app_services_drop_cache(const char *id){int i=find(id);if(i>=0)drop(i);}
uint32_t app_services_cache_bytes(void){uint32_t n=0;for(int i=0;i<5;i++)n+=cache[i].bytes;return n;}
int32_t app_host_cache_put_v2(uint32_t version,const uint8_t *data,uint32_t bytes) {
    if(!active()||borrowed||(!data&&bytes))return APP_ERR_ARGUMENT;
    int own=find(current);
    if(!bytes||bytes>CACHE_APP_MAX) {
        if(own>=0)drop(own);
        return bytes ? APP_ERR_MEMORY : APP_OK;
    }
    while(app_services_cache_bytes()+bytes>CACHE_TOTAL_MAX) {
        int victim=-1;
        for(int i=0;i<5;i++)if(i!=own&&cache[i].data&&(victim<0||cache[i].used<cache[victim].used))victim=i;
        if(victim<0)return APP_ERR_MEMORY;
        drop(victim);
    }
    uint8_t *copy=malloc(bytes);if(!copy)return APP_ERR_MEMORY;
    memcpy(copy,data,bytes);
    if(own<0)for(int i=0;i<5;i++)if(!cache[i].id[0]){own=i;break;}
    if(own<0){free(copy);return APP_ERR_MEMORY;}
    drop(own);strcpy(cache[own].id,current);cache[own].data=copy;cache[own].bytes=bytes;
    cache[own].version=version;cache[own].used=esp_timer_get_time();return APP_OK;
}
const uint8_t *app_host_cache_get_v2(uint32_t version,uint32_t *bytes) {
    if(!bytes)return NULL;
    *bytes=0;
    if(!active())return NULL;
    int i=find(current);if(i<0)return NULL;
    if(cache[i].version!=version){if(!borrowed)drop(i);return NULL;}
    borrowed=true;*bytes=cache[i].bytes;cache[i].used=esp_timer_get_time();return cache[i].data;
}
int32_t app_host_timer_v2(uint32_t id,uint32_t interval) {
    if(!active()||(interval&&context->event==APP_EVENT_STOP)|| (interval&&interval<1000))return APP_ERR_ARGUMENT;
    timer_id=id;timer_interval=interval;timer_next=esp_timer_get_time()/1000u+interval;return APP_OK;
}
bool app_services_timer_due(uint32_t *id) {
    uint64_t now=esp_timer_get_time()/1000u;
    if(!timer_interval||now<timer_next)return false;
    *id=timer_id;timer_next=now+timer_interval;return true;
}
void app_services_network(bool value){connected=value;}
bool app_services_connected(void){return connected;}
int32_t app_host_ble_register_v2(const app_ble_service_v2_t *service) {
    if(!active() || context->event==APP_EVENT_STOP)return APP_ERR_RUNTIME;
    return app_ble_register(generation,service);
}
int32_t app_host_ble_set_v2(uint32_t index,const uint8_t *data,uint32_t size) {
    if(!active() || context->event==APP_EVENT_STOP)return APP_ERR_RUNTIME;
    return app_ble_set(generation,index,data,size);
}
int32_t app_host_ble_unregister_v2(void) {
    return active()?app_ble_unregister(generation):APP_ERR_RUNTIME;
}
static bool coc_active(void){return active() && context->abi_minor>=2;}
int32_t app_host_coc_open_v22(const app_coc_config_v22_t *c){
    return coc_active() && context->event!=APP_EVENT_STOP?app_coc_open(generation,c):APP_ERR_RUNTIME;
}
int32_t app_host_coc_info_v22(app_coc_info_v22_t *i){return coc_active()?app_coc_info(generation,i):APP_ERR_RUNTIME;}
int32_t app_host_coc_recv_v22(uint32_t s,void *p,uint32_t n,uint32_t *r){
    return coc_active() && context->event!=APP_EVENT_STOP?app_coc_recv(generation,s,p,n,r):APP_ERR_RUNTIME;
}
int32_t app_host_coc_send_v22(uint32_t s,const void *p,uint32_t n){
    return coc_active() && context->event!=APP_EVENT_STOP?app_coc_send(generation,s,p,n):APP_ERR_RUNTIME;
}
int32_t app_host_coc_close_v22(void){return coc_active()?app_coc_close(generation):APP_ERR_RUNTIME;}
