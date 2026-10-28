/* Execute the production manager against pthread-backed RTOS primitives. */
#include "app_manager.h"
#include "app_runtime.h"
#include "app_services.h"
#include "app_catalog.h"
#include "app_ble.h"
#include "app_coc.h"
#include "app_pairing.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
struct semaphore {pthread_mutex_t mutex;pthread_cond_t cond;int count;};
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    SemaphoreHandle_t s=calloc(1,sizeof(*s));pthread_mutex_init(&s->mutex,NULL);pthread_cond_init(&s->cond,NULL);return s;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void){SemaphoreHandle_t s=xSemaphoreCreateBinary();s->count=1;return s;}
int xSemaphoreTake(SemaphoreHandle_t s,uint32_t ms) {
    pthread_mutex_lock(&s->mutex);
    while(!s->count) {
        if(!ms){pthread_mutex_unlock(&s->mutex);return 0;}
        pthread_cond_wait(&s->cond,&s->mutex);
    }
    s->count--;pthread_mutex_unlock(&s->mutex);return 1;
}
int xSemaphoreGive(SemaphoreHandle_t s){pthread_mutex_lock(&s->mutex);s->count=1;pthread_cond_signal(&s->cond);pthread_mutex_unlock(&s->mutex);return 1;}
void vSemaphoreDelete(SemaphoreHandle_t s){pthread_mutex_destroy(&s->mutex);pthread_cond_destroy(&s->cond);free(s);}
struct queue {pthread_mutex_t mutex;unsigned size,count;unsigned char data[1024];};
static QueueHandle_t created_queue;static atomic_bool pause_job_delivery;
QueueHandle_t xQueueCreate(unsigned n,unsigned size){assert(n==8&&size*8<=1024);QueueHandle_t q=calloc(1,sizeof(*q));q->size=size;pthread_mutex_init(&q->mutex,NULL);created_queue=q;return q;}
int xQueueSend(QueueHandle_t q,const void *data,uint32_t ms){(void)ms;pthread_mutex_lock(&q->mutex);if(q->count==8){pthread_mutex_unlock(&q->mutex);return 0;}memcpy(q->data+q->size*q->count++,data,q->size);pthread_mutex_unlock(&q->mutex);return 1;}
int xQueueReceive(QueueHandle_t q,void *data,uint32_t ms) {
    for(unsigned i=0;i<=ms;i++) {
        if(atomic_load(&pause_job_delivery)){if(i<ms)usleep(1000);continue;}
        pthread_mutex_lock(&q->mutex);
        if(q->count){memcpy(data,q->data,q->size);q->count--;memmove(q->data,q->data+q->size,q->count*q->size);pthread_mutex_unlock(&q->mutex);return 1;}
        pthread_mutex_unlock(&q->mutex);if(i<ms)usleep(1000);
    }return 0;
}
struct launch {void (*fn)(void *);void *arg;};
static void *launch(void *p){struct launch l=*(struct launch *)p;free(p);l.fn(l.arg);return NULL;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out) {
    (void)name;(void)stack;(void)priority;struct launch *l=malloc(sizeof(*l));*l=(struct launch){fn,arg};
    pthread_t task;assert(!pthread_create(&task,NULL,launch,l));if(out)*out=task;pthread_detach(task);return 1;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void){return pthread_self();}
void vTaskDelay(uint32_t ms){usleep(ms*1000);}
int gpio_config(const gpio_config_t *c){assert(c->pin_bit_mask==(1u<<4)&&c->pull_up_en==1&&c->pull_down_en==0);return 0;}
int gpio_get_level(int pin){assert(pin==4);return 1;}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int esp_reset_reason(void){return 15;}
static app_catalog_t catalog={.selected=-1,.trial_app=-1};
static atomic_int selects,stages,removes,rollbacks;
const app_catalog_t *app_catalog_get(void){return &catalog;}
module_state_t module_slots_state(void){return (module_state_t){-1,-1,-1,0};}
esp_err_t app_catalog_read(int a,int b,uint8_t **p,module_header_t *h){(void)a;(void)b;(void)p;(void)h;return -1;}
int app_catalog_find(const char *id){(void)id;return -1;}
esp_err_t app_catalog_stage(const char *id,const uint8_t *p,size_t n){(void)id;(void)p;(void)n;stages++;return 0;}
esp_err_t app_catalog_remove(int i){(void)i;removes++;return 0;}
bool app_manifest_read(const uint8_t *p,size_t n,app_manifest_v2_t *m){(void)p;(void)n;(void)m;return false;}
void app_services_drop_cache(const char *id){(void)id;}
uint32_t app_services_cache_bytes(void){return 0;}
bool app_services_timer_due(uint32_t *id){(void)id;return false;}
static bool connected;
void app_services_network(bool v){connected=v;}
static atomic_int events,inside,allow_finish,finished;
static atomic_bool pause_after_display,physical_finished,allow_return;
static atomic_int ble_events;
static atomic_bool runtime_ready;
static atomic_bool pairing_blocked,pairing_view_pending;
static atomic_int pairing_draws;
static atomic_uint pairing_authority=1,reset_ack;
static atomic_int reset_count,revoke_count;
uint32_t app_pairing_authorization_epoch(void){return atomic_load(&pairing_authority);}
unsigned app_pairing_state(uint32_t *serial){if(serial)*serial=777;return pairing_blocked?PP_PAIR_AWAIT_BUTTON:PP_PAIR_BOUND;}
bool app_pairing_blocks_app(void){return pairing_blocked;}
bool app_pairing_take_view(pp_pair_view *out){if(!atomic_exchange(&pairing_view_pending,false))return false;*out=(pp_pair_view){.serial=777,.kind=PP_PAIR_VIEW_PROMPT};return true;}
int app_pairing_draw(const pp_pair_view *v){assert(v->serial==777&&app_manager_busy());pairing_draws++;return 0;}
void app_pairing_view_complete(uint32_t serial,bool ok){assert(serial==777&&ok&&pairing_draws==1);pairing_blocked=true;}
esp_err_t app_pairing_button(bool reset,uint32_t serial){(void)serial;assert(app_manager_busy());if(reset){assert(reset_ack==app_manager_pairing_reset_pending());pairing_authority++;reset_count++;}return 0;}
void photopainter_ble_revoke(void){revoke_count++;}
bool photopainter_ble_reset_ready(uint32_t serial){return atomic_load(&reset_ack)==serial;}
esp_err_t app_services_legacy_begin(void){assert(app_manager_busy());return app_manager_display_begin()?0:ESP_ERR_INVALID_STATE;}
void app_services_end(void){assert(app_manager_busy());app_manager_display_end();}
atomic_size_t mock_free_psram=8000000,mock_free_internal=32768;
void photopainter_ble_app_changed(void){}
int app_runtime_ble(const app_ble_event_v2_t *event){assert(event->type==APP_BLE_CONNECTED || event->type==APP_BLE_WRITTEN);ble_events++;return APP_OK;}
int app_runtime_event(uint32_t type,uint32_t timer){assert(type==APP_EVENT_NETWORK&&connected);(void)timer;events++;return 0;}
uint32_t app_runtime_abi(void){return 2;}
uint32_t app_runtime_abi_minor(void){return 2;}
uint32_t app_runtime_generation(void){return 42;}
bool app_runtime_ready(void){return runtime_ready;}
bool app_runtime_last_display(void){return false;}
esp_err_t photoframe_plugin_init(void){return ESP_ERR_INVALID_STATE;}
int app_coc_info(uint32_t gen,app_coc_info_v22_t *info){(void)gen;(void)info;return APP_ERR_RUNTIME;}
bool app_coc_take(uint32_t gen,app_coc_event_v22_t *event){(void)gen;(void)event;return false;}
bool app_coc_registration(uint32_t *gen,uint32_t *rev,uint32_t *max){(void)gen;(void)rev;(void)max;return false;}
void app_coc_disconnected(uint32_t revision,uint32_t session,int error){(void)revision;(void)session;(void)error;}
int app_runtime_coc(const app_coc_event_v22_t *event){(void)event;assert(false);return APP_ERR_RUNTIME;}
int photoframe_plugin_slot(void){return 0;}
static int loaded_app=-1;
int photoframe_plugin_app(void){return loaded_app;}
uint32_t photoframe_plugin_version(void){return 1;}
bool photoframe_plugin_is_ready(void){return runtime_ready;}
esp_err_t photoframe_plugin_select(const char *id,bool u){(void)id;(void)u;selects++;runtime_ready=true;return 0;}
esp_err_t photoframe_plugin_rollback_app(const char *id){(void)id;rollbacks++;return 0;}
int photoframe_plugin_render(const uint8_t *data,size_t size) {
    assert(size==16&&data[0]==0x53);assert(app_manager_display_begin());inside=1;
    while(!allow_finish)usleep(1000);
    /* Data must remain alive after the simulated client disconnect. */
    assert(data[0]==0x53&&data[15]==0x53);app_manager_display_end();
    if(pause_after_display){physical_finished=true;while(!allow_return)usleep(1000);assert(data[0]==0x53&&data[15]==0x53);}
    return 0;
}
esp_err_t wifi_sd_load(const wifi_profiles_t *p,bool m,wifi_profiles_t *o){(void)p;(void)m;(void)o;return 0;}
esp_err_t wifi_sd_save(const wifi_profiles_t *p){(void)p;return 0;}
static void *client(void *arg) {
    (void)arg;uint8_t *p=malloc(16);memset(p,0x53,16);
    bool held=false;for(unsigned i=0;!held&&i<1000;i++){held=app_manager_acquire();if(!held)usleep(1000);}
    assert(held);assert(app_manager_render(p,16)==0);
    free(p);app_manager_release();finished=1;return NULL;
}
static atomic_bool cancelled_job;
static void acquire_idle(void){for(unsigned i=0;i<1000;i++){if(app_manager_acquire())return;usleep(1000);}assert(false);}
static void *queued_old_owner(void *unused){
    (void)unused;unsigned authority=app_pairing_authorization_epoch();
    acquire_idle();
    assert(app_manager_select_authorized("photoframe",false,"ble",authority)==ESP_ERR_INVALID_STATE);
    app_manager_release();
    /* The transport owner ACK comes only after the synchronous borrowed job
     * returns and its admission is released. */
    reset_ack=app_manager_pairing_reset_pending();cancelled_job=true;return NULL;
}
static void wait_reset(unsigned count){for(unsigned i=0;reset_count<(int)count&&i<1000;i++)usleep(1000);assert(reset_count==(int)count);for(unsigned i=0;app_manager_pairing_reset_pending()&&i<1000;i++)usleep(1000);assert(!app_manager_pairing_reset_pending());}
int main(void) {
    assert(app_manager_start()==0);
    for(int i=0;app_manager_busy()&&i<1000;i++)usleep(1000);
    assert(!app_manager_busy());uint32_t e=app_manager_epoch();usleep(120000);
    assert(app_manager_epoch()==e); /* Idle polling must not invalidate every press. */
    app_manager_network_changed(true);usleep(60000);
    assert(events==0 && connected && !app_manager_busy());
    char initial[4096];app_manager_status(initial,sizeof(initial));
    cJSON *fault=cJSON_Parse(initial);assert(fault);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(fault,"phase")->valuestring,"fault"));
    assert(cJSON_GetObjectItemCaseSensitive(fault,"last_result")->valuedouble==ESP_ERR_INVALID_STATE);
    cJSON_Delete(fault);
    assert(app_manager_acquire());assert(app_manager_select("photoframe",false)==0);app_manager_release();
    assert(app_manager_acquire());
    strcpy(catalog.apps[0].id,"photoframe");loaded_app=0;
    assert(app_manager_target_is_running("photoframe"));
    assert(!app_manager_target_is_running("color-test") && !app_manager_target_is_running(NULL));
    loaded_app=APP_LIMIT;assert(!app_manager_target_is_running("photoframe"));
    loaded_app=-1;memset(catalog.apps[0].id,0,sizeof(catalog.apps[0].id));app_manager_release();
    pthread_t t;assert(!pthread_create(&t,NULL,client,NULL));
    for(int i=0;!inside&&i<1000;i++)usleep(1000);
    assert(inside&&app_manager_busy()&&!app_manager_acquire());
    assert(app_manager_display_busy());
    char status[4096];app_manager_status(status,sizeof(status));assert(strstr(status,"display"));
    mock_free_psram=7500000;mock_free_internal=24576;
    app_manager_status(status,sizeof(status));
    assert(strstr(status,"\"free_psram\":7500000") && strstr(status,"\"free_internal_ram\":24576"));
    cJSON *diagnostics=cJSON_Parse(status);assert(diagnostics);
    assert(cJSON_GetObjectItemCaseSensitive(diagnostics,"reset_reason")->valuedouble==15);
    double uptime=cJSON_GetObjectItemCaseSensitive(diagnostics,"uptime_ms")->valuedouble;
    cJSON_Delete(diagnostics);usleep(5000);app_manager_status(status,sizeof(status));
    diagnostics=cJSON_Parse(status);assert(diagnostics);
    assert(cJSON_GetObjectItemCaseSensitive(diagnostics,"uptime_ms")->valuedouble>uptime);
    cJSON_Delete(diagnostics);
    for(int i=0;i<20;i++)app_manager_network_changed(true);
    usleep(60000);assert(events==0&&!finished);
    allow_finish=1;pthread_join(t,NULL);assert(finished);
    for(int i=0;!events&&i<1000;i++)usleep(1000);
    assert(events==1);usleep(60000);assert(events==1&&!app_manager_busy());
    app_ble_bind(42);
    app_ble_service_v2_t service={.size=sizeof(service),.count=1,.uuid={0x91},
        .characteristics={{.uuid={0x92},.properties=APP_BLE_READ|APP_BLE_WRITE,.max_bytes=128}}};
    assert(app_ble_register(42,&service)==APP_OK);app_ble_connection(true);
    uint32_t rev;assert(app_ble_schema(NULL,NULL,&rev));
    uint8_t output[128];size_t size=0;
    assert(app_manager_acquire());
    assert(app_ble_request(true,42,rev,0,(const uint8_t *)"echo",4,output,&size)==0);
    usleep(60000);int before=ble_events; /* Queued BLE events cannot bypass admission. */
    usleep(60000);assert(ble_events==before);
    assert(app_manager_ble_flush(42)==0 && ble_events==2 && app_manager_busy());
    assert(app_manager_ble_flush(41)==ESP_ERR_INVALID_STATE);
    app_manager_release();
    for(int i=0;ble_events<2&&i<1000;i++)usleep(1000);
    assert(ble_events==2);app_ble_cancel();
    pairing_view_pending=true;
    for(int i=0;!pairing_draws&&i<1000;i++)usleep(1000);
    assert(pairing_draws==1&&pairing_blocked);
    app_manager_network_changed(true);usleep(80000);assert(events==1);
    pairing_blocked=false;
    for(int i=0;events<2&&i<1000;i++)usleep(1000);
    assert(events==2&&pairing_draws==1);
    /* A BEGIN-like upload holds admission but no physical refresh: the local
     * reset request must be accepted and block further remote admission. */
    acquire_idle();assert(!app_manager_display_busy());
    assert(app_manager_request_pairing_reset());unsigned serial=app_manager_pairing_reset_pending();assert(serial&&revoke_count>0);
    usleep(50000);assert(reset_count==0&&!app_manager_acquire());
    app_manager_release();reset_ack=serial-1;usleep(60000);assert(reset_count==0);
    reset_ack=serial;wait_reset(1);
    /* Reproduce the old local-frame TOCTOU: its precheck was before reset, but
     * it obtains admission only after reset has finished. No side effect runs. */
    unsigned before_select=selects,before_stage=stages,before_remove=removes,before_rollback=rollbacks;
    acquire_idle();
    uint8_t dummy[16]={0x53};
    assert(app_manager_select_authorized("photoframe",false,"ble",1)==ESP_ERR_INVALID_STATE);
    assert(app_manager_stage_authorized("photoframe",dummy,sizeof(dummy),1)==ESP_ERR_INVALID_STATE);
    assert(app_manager_remove_authorized("photoframe",1)==ESP_ERR_INVALID_STATE);
    assert(app_manager_rollback_authorized("photoframe","ble",1)==ESP_ERR_INVALID_STATE);
    assert(app_manager_render_authorized(dummy,sizeof(dummy),1)==ESP_ERR_INVALID_STATE);
    uint8_t reply[128];size_t reply_size=0;
    assert(app_manager_ble_request_authorized(true,dummy,sizeof(dummy),reply,&reply_size,1)==PP_BLE_STATE);
    app_manager_release();assert(selects==before_select&&stages==before_stage&&removes==before_remove&&rollbacks==before_rollback);
    /* A queued old-owner job is cancelled before execution, lets the worker
     * release its admission, and only then permits the exact reset ACK. */
    pause_job_delivery=true;assert(!pthread_create(&t,NULL,queued_old_owner,NULL));
    for(unsigned i=0;i<1000;i++){pthread_mutex_lock(&created_queue->mutex);unsigned queued=created_queue->count;pthread_mutex_unlock(&created_queue->mutex);if(queued)break;usleep(1000);}
    pthread_mutex_lock(&created_queue->mutex);assert(created_queue->count==1);pthread_mutex_unlock(&created_queue->mutex);
    assert(app_manager_request_pairing_reset());usleep(60000);assert(reset_count==1&&!cancelled_job);
    pause_job_delivery=false;pthread_join(t,NULL);assert(cancelled_job);wait_reset(2);assert(selects==before_select);
    /* An owner cannot evade an 8-second physical request by refreshing back
     * to back. Existing display/borrowed pixels finish; every later draw is
     * denied from the instant reset REQUEST is latched. */
    inside=finished=allow_finish=0;pause_after_display=true;physical_finished=false;allow_return=false;
    assert(!pthread_create(&t,NULL,client,NULL));
    for(unsigned i=0;!inside&&i<1000;i++)usleep(1000);
    assert(inside&&app_manager_display_busy());
    assert(app_manager_request_pairing_reset());serial=app_manager_pairing_reset_pending();
    assert(serial&&!app_manager_request_pairing_reset()&&!app_manager_acquire());
    reset_ack=serial; /* CoC can leave the GATT owner idle while manager draws. */
    usleep(60000);assert(reset_count==2&&!finished&&!physical_finished&&app_manager_display_busy());
    allow_finish=1;
    for(unsigned i=0;!physical_finished&&i<1000;i++)usleep(1000);
    assert(physical_finished&&!app_manager_display_busy()&&!finished&&reset_count==2);
    assert(!app_manager_display_begin()); /* Pending gate, not display busy. */
    allow_return=true;pthread_join(t,NULL);assert(finished);wait_reset(3);
    assert(!app_manager_display_busy()&&!app_manager_pairing_reset_pending());
    assert(app_manager_display_begin());app_manager_display_end();
    return 0;
}
