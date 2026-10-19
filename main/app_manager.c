/* All directory writes and application calls execute on this one worker. */
#include "app_manager.h"
#include "app_runtime.h"
#include "app_services.h"
#include "app_catalog.h"
#include "app_button.h"
#include "wifi_sd.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {RENDER,SELECT,ROLLBACK,STAGE,REMOVE,WIFI_READ,WIFI_SAVE,NEXT};
typedef struct {
    int op;uint32_t generation;const char *id;const void *data;size_t size;bool update;
    int *result;SemaphoreHandle_t done;
} job_t;
static QueueHandle_t jobs;
static SemaphoreHandle_t admission,snapshot_lock;
static TaskHandle_t worker;
static atomic_bool busy=true,network_dirty=false,network_value=false;
static atomic_uint epoch,ignored_keys;
static char snapshot[4096]="{\"ready\":false,\"runtime_ready\":false,\"phase\":\"starting\"}";
static const char *phase="starting",*source="boot";
static int last_result;
static char last_target[32];
bool app_manager_busy(void){return atomic_load(&busy);}
uint32_t app_manager_epoch(void){return atomic_load(&epoch);}
bool app_manager_acquire(void) {
    if(!admission||xSemaphoreTake(admission,0)!=pdTRUE)return false;
    atomic_store(&busy,true);atomic_fetch_add(&epoch,1);return true;
}
void app_manager_release(void){atomic_store(&busy,false);xSemaphoreGive(admission);}
void app_manager_network_changed(bool value){atomic_store(&network_value,value);atomic_store(&network_dirty,true);}
static void update_snapshot(void) {
    const app_catalog_t *c=app_catalog_get();module_state_t state=module_slots_state();
    cJSON *o=cJSON_CreateObject();if(!o)return;
    cJSON_AddNumberToObject(o,"abi",1); /* legacy envelope, not current payload ABI */
    cJSON_AddNumberToObject(o,"payload_abi",app_runtime_abi());
    cJSON_AddNumberToObject(o,"active",state.active);cJSON_AddNumberToObject(o,"previous",state.previous);
    cJSON_AddNumberToObject(o,"pending",state.pending);cJSON_AddNumberToObject(o,"trial",state.trial);
    cJSON_AddNumberToObject(o,"loaded",photoframe_plugin_slot());cJSON_AddNumberToObject(o,"version",photoframe_plugin_version());
    cJSON_AddBoolToObject(o,"ready",photoframe_plugin_is_ready());cJSON_AddBoolToObject(o,"runtime_ready",app_runtime_ready());
    cJSON_AddBoolToObject(o,"confirmed",c&&c->trial_app<0&&app_runtime_ready());
    cJSON_AddNumberToObject(o,"selected",c?c->selected:-1);cJSON_AddNumberToObject(o,"trial_app",c?c->trial_app:-1);
    cJSON_AddNumberToObject(o,"loaded_app",photoframe_plugin_app());cJSON_AddNumberToObject(o,"capacity",APP_LIMIT);
    cJSON_AddNumberToObject(o,"generation",app_runtime_generation());cJSON_AddStringToObject(o,"phase",phase);
    cJSON_AddStringToObject(o,"last_switch_source",source);cJSON_AddStringToObject(o,"last_switch_target",last_target);
    cJSON_AddNumberToObject(o,"last_result",last_result);cJSON_AddNumberToObject(o,"ignored_buttons",atomic_load(&ignored_keys));
    cJSON_AddBoolToObject(o,"display_completed",app_runtime_last_display());
    cJSON_AddNumberToObject(o,"cache_bytes",app_services_cache_bytes());
    cJSON_AddNumberToObject(o,"free_psram",heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(o,"minimum_free_psram",heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    cJSON *apps=cJSON_AddArrayToObject(o,"apps");
    for(int i=0;c&&i<APP_LIMIT;i++) {
        const app_entry_t *a=&c->apps[i];if(!a->id[0])continue;
        cJSON *v=cJSON_CreateObject();cJSON_AddItemToArray(apps,v);
        cJSON_AddStringToObject(v,"id",a->id);cJSON_AddNumberToObject(v,"bank",i);cJSON_AddNumberToObject(v,"slot_bytes",MODULE_SLOT_BYTES);
        cJSON_AddNumberToObject(v,"active",a->slots.active);cJSON_AddNumberToObject(v,"previous",a->slots.previous);
        cJSON_AddNumberToObject(v,"pending",a->slots.pending);cJSON_AddNumberToObject(v,"trial",a->slots.trial);
        cJSON *versions=cJSON_AddArrayToObject(v,"versions");uint32_t active_abi=0,inputs=0;char name[64]="";
        for(int s=0;s<2;s++) {
            uint8_t *data=NULL;module_header_t h;
            if(app_catalog_read(i,s,&data,&h)==ESP_OK) {
                cJSON_AddItemToArray(versions,cJSON_CreateNumber(h.version));
                if(s==a->slots.active) {
                    active_abi=h.abi;inputs=h.abi==1?APP_INPUT_PNG:0;
                    app_manifest_v2_t m;if(h.abi==2&&app_manifest_read(data,h.length,&m)){inputs=m.inputs;strcpy(name,m.name);}
                }
            } else cJSON_AddItemToArray(versions,cJSON_CreateNull());
            free(data);
        }
        cJSON_AddNumberToObject(v,"abi",active_abi);cJSON_AddNumberToObject(v,"inputs",inputs);
        cJSON_AddStringToObject(v,"name",name[0]?name:a->id);
        cJSON_AddBoolToObject(v,"legacy_push_only",active_abi==1);
        cJSON_AddBoolToObject(v,"button_eligible",active_abi==2&&a->slots.active>=0);
    }
    char *text=cJSON_PrintUnformatted(o);cJSON_Delete(o);
    if(text) {
        if(strlen(text)<sizeof(snapshot)) {
            xSemaphoreTake(snapshot_lock,portMAX_DELAY);strcpy(snapshot,text);xSemaphoreGive(snapshot_lock);
        }
        free(text);
    }
}
void app_manager_status(char *out,size_t size) {
    if(!out||!size)return;
    if(snapshot_lock)xSemaphoreTake(snapshot_lock,portMAX_DELAY);
    snprintf(out,size,"%s",snapshot);
    if(snapshot_lock)xSemaphoreGive(snapshot_lock);
}
static int next_app(void) {
    const app_catalog_t *c=app_catalog_get();if(!c||c->trial_app>=0)return ESP_ERR_INVALID_STATE;
    int current=photoframe_plugin_app();
    for(int n=1;n<=APP_LIMIT;n++) {
        int i=(current+n+APP_LIMIT)%APP_LIMIT;if(i==current||!c->apps[i].id[0]||c->apps[i].slots.active<0)continue;
        uint8_t *data=NULL;module_header_t h;
        esp_err_t e=app_catalog_read(i,c->apps[i].slots.active,&data,&h);free(data);
        if(e!=ESP_OK||h.abi!=2)continue;
        strcpy(last_target,c->apps[i].id);
        return photoframe_plugin_select(last_target,false);
    }
    return ESP_ERR_NOT_FOUND;
}
static int execute(const job_t *j) {
    switch(j->op) {
    case RENDER:return j->generation==app_runtime_generation()?photoframe_plugin_render(j->data,j->size):APP_ERR_RUNTIME;
    case SELECT:source="http";snprintf(last_target,sizeof(last_target),"%s",j->id);return photoframe_plugin_select(j->id,j->update);
    case ROLLBACK:source="http";snprintf(last_target,sizeof(last_target),"%s",j->id);return photoframe_plugin_rollback_app(j->id);
    case STAGE:return app_catalog_stage(j->id,j->data,j->size);
    case REMOVE:{int e=app_catalog_remove(app_catalog_find(j->id));if(e==ESP_OK)app_services_drop_cache(j->id);return e;}
    case WIFI_READ:return wifi_sd_load(NULL,false,(wifi_profiles_t *)j->data);
    case WIFI_SAVE:return wifi_sd_save(j->data);
    case NEXT:source="button";return next_app();
    default:return ESP_ERR_INVALID_ARG;
    }
}
static void run(void *unused) {
    (void)unused;
    last_result=photoframe_plugin_init();phase=last_result==ESP_OK?"idle":"fault";update_snapshot();app_manager_release();
    for(;;) {
        job_t j;
        if(xQueueReceive(jobs,&j,pdMS_TO_TICKS(20))==pdTRUE) {
            phase=j.op==RENDER?"display":j.op==SELECT||j.op==NEXT||j.op==ROLLBACK?"switching":"management";
            update_snapshot();int result=execute(&j);last_result=result;phase=photoframe_plugin_is_ready()?"idle":"fault";update_snapshot();
            if(j.done){*j.result=result;xSemaphoreGive(j.done);}else app_manager_release();
        } else if(xSemaphoreTake(admission,0)==pdTRUE) {
            uint32_t timer=0;
            bool network=atomic_exchange(&network_dirty,false);
            bool tick=!network&&app_services_timer_due(&timer);
            if(!network&&!tick){xSemaphoreGive(admission);continue;}
            atomic_store(&busy,true);atomic_fetch_add(&epoch,1);
            bool event=false;
            if(network) {
                app_services_network(atomic_load(&network_value));phase="network";
                last_result=app_runtime_event(APP_EVENT_NETWORK,0);event=true;
                tick=app_services_timer_due(&timer);
            }
            if(tick) {phase="timer";last_result=app_runtime_event(APP_EVENT_TIMER,timer);event=true;}
            if(event){phase=photoframe_plugin_is_ready()?"idle":"fault";update_snapshot();}
            app_manager_release();
        }
    }
}
static int invoke(job_t j) {
    if(!jobs||xTaskGetCurrentTaskHandle()==worker)return ESP_ERR_INVALID_STATE;
    SemaphoreHandle_t done=xSemaphoreCreateBinary();if(!done)return ESP_ERR_NO_MEM;
    int result=ESP_FAIL;j.done=done;j.result=&result;
    if(xQueueSend(jobs,&j,0)!=pdTRUE){vSemaphoreDelete(done);return ESP_ERR_INVALID_STATE;}
    /* HTTP handler retains request and borrowed buffers until worker completes.
     * Socket disconnect does not cancel this wait or free a live frame. */
    xSemaphoreTake(done,portMAX_DELAY);vSemaphoreDelete(done);return result;
}
int app_manager_render(const uint8_t *p,size_t n){return invoke((job_t){.op=RENDER,.generation=app_runtime_generation(),.data=p,.size=n});}
esp_err_t app_manager_select(const char *id,bool update){return invoke((job_t){.op=SELECT,.id=id,.update=update});}
esp_err_t app_manager_rollback(const char *id){return invoke((job_t){.op=ROLLBACK,.id=id});}
esp_err_t app_manager_stage(const char *id,const uint8_t *p,size_t n){return invoke((job_t){.op=STAGE,.id=id,.data=p,.size=n});}
esp_err_t app_manager_remove(const char *id){return invoke((job_t){.op=REMOVE,.id=id});}
esp_err_t app_manager_wifi_read(wifi_profiles_t *out){return invoke((job_t){.op=WIFI_READ,.data=out});}
esp_err_t app_manager_wifi_save(const wifi_profiles_t *in){return invoke((job_t){.op=WIFI_SAVE,.data=in});}
static void button_task(void *unused) {
    (void)unused;app_button_t policy={0};
    for(;;) {
        /* GP4_Active=0, pull-up enabled, official button_bsp.c a5e8f757ba0c. */
        bool was_busy=app_manager_busy();uint32_t e=app_manager_epoch();
        uint32_t before=policy.ignored;
        if(app_button_sample(&policy,gpio_get_level(GPIO_NUM_4)==0,was_busy,e,esp_timer_get_time()/1000u)) {
            if(app_manager_acquire()) {
                job_t j={.op=NEXT};
                if(xQueueSend(jobs,&j,0)!=pdTRUE){atomic_fetch_add(&ignored_keys,1);app_manager_release();}
            } else atomic_fetch_add(&ignored_keys,1);
        }
        atomic_fetch_add(&ignored_keys,policy.ignored-before);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
esp_err_t app_manager_start(void) {
    if(jobs)return ESP_ERR_INVALID_STATE;
    admission=xSemaphoreCreateBinary();snapshot_lock=xSemaphoreCreateMutex();jobs=xQueueCreate(8,sizeof(job_t));
    if(!admission||!snapshot_lock||!jobs)return ESP_ERR_NO_MEM;
    const gpio_config_t config={.pin_bit_mask=1ULL<<GPIO_NUM_4,.mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    esp_err_t e=gpio_config(&config);if(e!=ESP_OK)return e;
    if(xTaskCreate(run,"app_manager",32768,NULL,5,&worker)!=pdPASS)return ESP_ERR_NO_MEM;
    if(xTaskCreate(button_task,"app_button",3072,NULL,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
}
