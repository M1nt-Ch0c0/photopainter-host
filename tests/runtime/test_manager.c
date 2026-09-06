/* Execute the production manager against pthread-backed RTOS primitives. */
#include "app_manager.h"
#include "app_runtime.h"
#include "app_services.h"
#include "app_catalog.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
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
QueueHandle_t xQueueCreate(unsigned n,unsigned size){assert(n==8&&size*8<=1024);QueueHandle_t q=calloc(1,sizeof(*q));q->size=size;pthread_mutex_init(&q->mutex,NULL);return q;}
int xQueueSend(QueueHandle_t q,const void *data,uint32_t ms){(void)ms;pthread_mutex_lock(&q->mutex);if(q->count==8){pthread_mutex_unlock(&q->mutex);return 0;}memcpy(q->data+q->size*q->count++,data,q->size);pthread_mutex_unlock(&q->mutex);return 1;}
int xQueueReceive(QueueHandle_t q,void *data,uint32_t ms) {
    for(unsigned i=0;i<=ms;i++) {
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
static app_catalog_t catalog={.selected=-1,.trial_app=-1};
const app_catalog_t *app_catalog_get(void){return &catalog;}
module_state_t module_slots_state(void){return (module_state_t){-1,-1,-1,0};}
esp_err_t app_catalog_read(int a,int b,uint8_t **p,module_header_t *h){(void)a;(void)b;(void)p;(void)h;return -1;}
int app_catalog_find(const char *id){(void)id;return -1;}
esp_err_t app_catalog_stage(const char *id,const uint8_t *p,size_t n){(void)id;(void)p;(void)n;return 0;}
esp_err_t app_catalog_remove(int i){(void)i;return 0;}
bool app_manifest_read(const uint8_t *p,size_t n,app_manifest_v2_t *m){(void)p;(void)n;(void)m;return false;}
void app_services_drop_cache(const char *id){(void)id;}
uint32_t app_services_cache_bytes(void){return 0;}
bool app_services_timer_due(uint32_t *id){(void)id;return false;}
static bool connected;
void app_services_network(bool v){connected=v;}
static atomic_int events,inside,allow_finish,finished;
int app_runtime_event(uint32_t type,uint32_t timer){assert(type==APP_EVENT_NETWORK&&connected);(void)timer;events++;return 0;}
uint32_t app_runtime_abi(void){return 2;}
uint32_t app_runtime_generation(void){return 42;}
bool app_runtime_ready(void){return true;}
bool app_runtime_last_display(void){return false;}
esp_err_t photoframe_plugin_init(void){return 0;}
int photoframe_plugin_slot(void){return 0;}
int photoframe_plugin_app(void){return -1;}
uint32_t photoframe_plugin_version(void){return 1;}
bool photoframe_plugin_is_ready(void){return true;}
esp_err_t photoframe_plugin_select(const char *id,bool u){(void)id;(void)u;return 0;}
esp_err_t photoframe_plugin_rollback_app(const char *id){(void)id;return 0;}
int photoframe_plugin_render(const uint8_t *data,size_t size) {
    assert(size==16&&data[0]==0x53);inside=1;
    while(!allow_finish)usleep(1000);
    /* Data must remain alive after the simulated client disconnect. */
    assert(data[0]==0x53&&data[15]==0x53);return 0;
}
esp_err_t wifi_sd_load(const wifi_profiles_t *p,bool m,wifi_profiles_t *o){(void)p;(void)m;(void)o;return 0;}
esp_err_t wifi_sd_save(const wifi_profiles_t *p){(void)p;return 0;}
static void *client(void *arg) {
    (void)arg;uint8_t *p=malloc(16);memset(p,0x53,16);
    assert(app_manager_acquire());assert(app_manager_render(p,16)==0);
    free(p);app_manager_release();finished=1;return NULL;
}
int main(void) {
    assert(app_manager_start()==0);
    for(int i=0;app_manager_busy()&&i<1000;i++)usleep(1000);
    assert(!app_manager_busy());uint32_t e=app_manager_epoch();usleep(120000);
    assert(app_manager_epoch()==e); /* Idle polling must not invalidate every press. */
    pthread_t t;assert(!pthread_create(&t,NULL,client,NULL));
    for(int i=0;!inside&&i<1000;i++)usleep(1000);
    assert(inside&&app_manager_busy()&&!app_manager_acquire());
    char status[4096];app_manager_status(status,sizeof(status));assert(strstr(status,"display"));
    for(int i=0;i<20;i++)app_manager_network_changed(true);
    usleep(60000);assert(events==0&&!finished);
    allow_finish=1;pthread_join(t,NULL);assert(finished);
    for(int i=0;!events&&i<1000;i++)usleep(1000);
    assert(events==1);usleep(60000);assert(events==1&&!app_manager_busy());
    return 0;
}
