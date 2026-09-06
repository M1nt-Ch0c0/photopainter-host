#include "app_services.h"
#include "photopainter_board.h"
#include "esp_timer.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static int64_t now;static uint64_t deadline;static int displays,restarts,result;
static void (*watchdog)(void *);
int64_t esp_timer_get_time(void){return now;}
esp_err_t esp_timer_create(const esp_timer_create_args_t *a,esp_timer_handle_t *t){watchdog=a->callback;*t=(void *)1;return 0;}
esp_err_t esp_timer_stop(esp_timer_handle_t t){(void)t;deadline=0;return 0;}
esp_err_t esp_timer_start_once(esp_timer_handle_t t,uint64_t us){(void)t;deadline=us;return 0;}
void esp_restart(void){restarts++;}
int photoframe_e6_render(const uint8_t *wire,size_t n) {
    assert(deadline==650000000);assert(n==192000);displays++;
    assert(wire[n-1]==0x53&&wire[0]==0x62);return result;
}
static app_context_v2_t c;
static void open_app(const char *id) {
    app_services_end();app_services_close();app_services_open(id,7);
    c=(app_context_v2_t){.size=sizeof(c),.generation=7,.event=APP_EVENT_START};
    assert(app_services_begin(&c)==0);
}
int main(void) {
    assert(app_services_init()==0);open_app("one");assert(deadline==5000000);
    uint8_t *frame=calloc(384000,1);assert(frame);
    frame[0]=3;frame[1]=4;frame[383998]=2;frame[383999]=5;
    uint8_t *wire=malloc(192000);memset(wire,0xa5,192000);
    frame[383997]=6;
    assert(photopainter_board_pack(frame,384000,wire,192000)==APP_ERR_COLOR);
    assert(wire[0]==0xa5&&wire[191999]==0xa5);
    assert(app_host_display_v2(frame,384000)==APP_ERR_COLOR&&displays==0);
    frame[383997]=0;
    assert(app_host_display_v2(frame,383999)==APP_ERR_ARGUMENT&&displays==0);
    assert(app_host_display_v2(frame,384000)==0&&displays==1);
    assert(app_services_display_completed()&&deadline==5000000);
    assert(app_host_display_v2(frame,384000)==APP_ERR_RUNTIME&&displays==1);
    app_services_end();c.event=APP_EVENT_STOP;assert(app_services_begin(&c)==0);
    assert(app_host_display_v2(frame,384000)==APP_ERR_RUNTIME);
    assert(app_host_timer_v2(1,1000)==APP_ERR_ARGUMENT);
    open_app("one");assert(app_host_cache_put_v2(1,frame,384000)==0);
    uint32_t n=0;const uint8_t *borrowed=app_host_cache_get_v2(1,&n);
    assert(borrowed&&n==384000&&borrowed[1]==4);
    assert(!app_host_cache_get_v2(2,&n)&&borrowed[1]==4);
    assert(app_host_cache_put_v2(1,frame,384000)!=0&&borrowed[1]==4);
    open_app("two");assert(!app_host_cache_get_v2(1,&n));now++;
    assert(app_host_cache_put_v2(1,frame,384000)==0);
    open_app("three");now++;assert(app_host_cache_put_v2(1,frame,384000)==0);
    assert(app_services_cache_bytes()==768000);
    open_app("one");assert(!app_host_cache_get_v2(1,&n));
    open_app("two");assert(!app_host_cache_get_v2(2,&n));
    assert(app_services_cache_bytes()==384000);
    assert(app_host_cache_put_v2(1,frame,512*1024+1)==APP_ERR_MEMORY);
    assert(app_host_timer_v2(19,999)==APP_ERR_ARGUMENT);
    assert(app_host_timer_v2(19,1000)==0);uint32_t id;
    now+=999000;assert(!app_services_timer_due(&id));
    now+=5000000;assert(app_services_timer_due(&id)&&id==19);
    assert(!app_services_timer_due(&id));
    app_services_close();now+=9000000;assert(!app_services_timer_due(&id));
    assert(app_host_display_v2(frame,384000)==APP_ERR_RUNTIME);
    open_app("three");result=APP_ERR_TIMEOUT;
    assert(app_host_display_v2(frame,384000)==APP_ERR_TIMEOUT);
    assert(!app_services_display_completed());watchdog(NULL);assert(restarts==1);
    app_services_end();assert(deadline==0);
    assert(app_services_legacy_begin()==0&&deadline==650000000);app_services_end();
    app_services_drop_cache("three");assert(app_services_cache_bytes()==0);
    free(frame);free(wire);return 0;
}
