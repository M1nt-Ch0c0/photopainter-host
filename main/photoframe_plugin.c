/* ABI 1 PNG adapter plus ABI 2 cooperative event runtime. */
#include "app_runtime.h"
#include "app_services.h"
#include "app_boot.h"
#include "app_catalog.h"
#include "esp_elf.h"
#include "esp_log.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static esp_elf_t elf;
static bool loaded,runtime_ready,event_active,result_received,result_bad,last_display;
static int slot=-1,app=-1,reported;
static uint32_t version,abi,generation,event_id;
static app_manifest_v2_t manifest;
static app_context_v2_t context;
static const uint8_t *legacy_png;
static size_t legacy_size;
extern esp_elf_symbol_table_t g_esp_photoframe_elfsyms[];
const uint8_t *photoframe_host_png_data(void){return legacy_png;}
size_t photoframe_host_png_size(void){return legacy_size;}
void photoframe_host_report_result(int value){reported=value;}
const app_context_v2_t *app_host_context_v2(void){return event_active ? &context : NULL;}
void app_host_complete_v2(const app_result_v2_t *r) {
    if(!event_active)return;
    if(result_received || !r || r->size!=sizeof(*r) || r->event_id!=context.event_id ||
        r->generation!=context.generation || (r->flags&~APP_READY) ||
        (r->flags && context.event!=APP_EVENT_START) || r->status>0 || r->status<APP_ERR_UNSUPPORTED) {
        result_bad=true;return;
    }
    result_received=true;reported=r->status;
    if(context.event==APP_EVENT_START)runtime_ready=reported==APP_OK && (r->flags&APP_READY);
}
static int dispatch(uint32_t type,uint32_t timer,const uint8_t *data,size_t size) {
    if(!loaded||abi!=2||event_active)return APP_ERR_RUNTIME;
    context=(app_context_v2_t){sizeof(context),2,0,type,++event_id,generation,
        type==APP_EVENT_INPUT?APP_INPUT_PNG:0,(uint32_t)size,data,timer,app_services_connected()};
    reported=APP_ERR_RUNTIME;result_received=result_bad=false;last_display=false;
    if(app_services_begin(&context)!=ESP_OK)return APP_ERR_RUNTIME;
    event_active=true;
    int e=esp_elf_request(&elf,0,0,NULL);
    event_active=false;last_display=app_services_display_completed();app_services_end();
    memset(&context,0,sizeof(context));
    if(e<0||!result_received||result_bad)return APP_ERR_RUNTIME;
    return reported;
}
static bool in_trial(void){const app_catalog_t *c=app_catalog_get();return c&&c->trial_app>=0;}
static void unload(void) {
    if(loaded)esp_elf_deinit(&elf);
    loaded=runtime_ready=false;slot=app=-1;abi=version=0;legacy_png=NULL;legacy_size=0;
    app_services_close();
}
esp_err_t app_runtime_shutdown(void) {
    if(event_active)return ESP_ERR_INVALID_STATE;
    int e=loaded&&abi==2 ? dispatch(APP_EVENT_STOP,0,NULL,0) : APP_OK;
    /* Calls have returned; ABI 2 imports cannot create background tasks. */
    unload();return e==APP_OK ? ESP_OK : ESP_FAIL;
}
static esp_err_t load_current(void) {
    module_state_t state=module_slots_state();int target=state.trial ? state.pending : state.active;
    if(app_runtime_shutdown()!=ESP_OK)return ESP_FAIL;
    uint8_t *payload=NULL;module_header_t header;
    esp_err_t e=module_slots_read(target,&payload,&header);if(e!=ESP_OK)return e;
    if(header.abi==2&&!app_manifest_read(payload,header.length,&manifest)){free(payload);return ESP_ERR_INVALID_ARG;}
    memset(&elf,0,sizeof(elf));int r=esp_elf_init(&elf);
    if(r>=0)r=esp_elf_relocate(&elf,payload);
    free(payload);
    if(r<0){esp_elf_deinit(&elf);return ESP_FAIL;}
    loaded=true;slot=target;app=app_catalog_running();version=header.version;abi=header.abi;
    ++generation;if(!generation)++generation;
    app_services_open(app_catalog_get()->apps[app].id,generation);
    if(abi==2) {
        r=dispatch(APP_EVENT_START,0,NULL,0);
        if(r!=APP_OK||!runtime_ready||((manifest.flags&APP_REQUIRES_DISPLAY)&&!last_display)) {
            (void)app_runtime_shutdown();return ESP_FAIL;
        }
        if(in_trial()&&module_slots_confirm()!=ESP_OK)return ESP_FAIL;
    }
    ESP_LOGI("app_runtime","ready bank=%d slot=%d ABI=%lu version=%lu",app,slot,(unsigned long)abi,(unsigned long)version);
    return ESP_OK;
}
esp_err_t photoframe_plugin_init(void) {
    /* Initial firmware startup; tests may call repeatedly with a fresh mock Flash. */
    if(loaded)(void)app_runtime_shutdown();
    esp_err_t e=module_slots_init();if(e!=ESP_OK)return e;
    if(app_services_init()!=ESP_OK||esp_elf_register_symbol(g_esp_photoframe_elfsyms)<0)return ESP_FAIL;
    e=app_boot_prepare();if(e!=ESP_OK)return e;
    e=load_current();
    if(e!=ESP_OK&&app_boot_fallback()==ESP_OK)e=load_current();
    if(e==ESP_OK)e=app_boot_complete();
    if(e!=ESP_OK&&loaded)(void)app_runtime_shutdown();
    return e;
}
esp_err_t photoframe_plugin_select(const char *id,bool update) {
    int target=app_catalog_find(id);
    if(!update&&!in_trial()&&loaded&&target==app&&runtime_ready)return ESP_OK;
    esp_err_t e=app_catalog_begin(target,update);if(e!=ESP_OK)return e;
    e=load_current();
    if(e!=ESP_OK&&in_trial()) {
        (void)app_runtime_shutdown();
        if(app_catalog_cancel()==ESP_OK)(void)load_current();
    }
    if(e==ESP_OK)e=app_boot_complete();
    return e;
}
esp_err_t photoframe_plugin_activate(void){return photoframe_plugin_select("photoframe",true);}
esp_err_t photoframe_plugin_rollback_app(const char *id) {
    int target=app_catalog_find(id);bool reload=target==app_catalog_running();
    /* A journal failure must leave the currently loaded application untouched. */
    esp_err_t e=app_catalog_rollback(target);
    if(e==ESP_OK&&reload)e=load_current();
    return e;
}
esp_err_t photoframe_plugin_rollback(void){int a=app_catalog_running();const app_catalog_t *c=app_catalog_get();return c&&a>=0?photoframe_plugin_rollback_app(c->apps[a].id):ESP_ERR_INVALID_STATE;}
int photoframe_plugin_app(void){return app;}
int photoframe_plugin_slot(void){return slot;}
uint32_t photoframe_plugin_version(void){return version;}
bool photoframe_plugin_is_ready(void){return loaded;}
uint32_t app_runtime_abi(void){return abi;}
uint32_t app_runtime_generation(void){return generation;}
bool app_runtime_ready(void){return runtime_ready;}
bool app_runtime_last_display(void){return last_display;}
bool app_runtime_accepts_png(void){return loaded&&(abi==1||(manifest.inputs&APP_INPUT_PNG));}
int photoframe_plugin_render(const uint8_t *png,size_t size) {
    if(!loaded)return APP_ERR_RUNTIME;
    if(!png||!size)return APP_ERR_ARGUMENT;
    if(!app_runtime_accepts_png())return APP_ERR_UNSUPPORTED;
    int result;
    if(abi==2) {
        result=dispatch(APP_EVENT_INPUT,0,png,size);
        if(result==APP_OK&&!last_display)result=APP_ERR_RUNTIME;
    } else {
        legacy_png=png;legacy_size=size;reported=INT_MIN;
        if(app_services_legacy_begin()!=ESP_OK){legacy_png=NULL;legacy_size=0;return APP_ERR_RUNTIME;}
        int e=esp_elf_request(&elf,0,0,NULL);app_services_end();legacy_png=NULL;legacy_size=0;
        result=e<0||reported==INT_MIN ? APP_ERR_RUNTIME : reported;
        last_display=result==APP_OK;
    }
    if(result==APP_OK) {
        runtime_ready=true;
        if(module_slots_confirm()!=ESP_OK)ESP_LOGE("app_runtime","confirmation commit failed");
    } else if(result<=APP_ERR_DISPLAY&&result!=APP_ERR_UNSUPPORTED) {
        if(in_trial())(void)photoframe_plugin_rollback();
        else if(result==APP_ERR_RUNTIME)(void)app_runtime_shutdown();
    }
    return result;
}
int app_runtime_event(uint32_t type,uint32_t timer) {
    if(abi!=2||!runtime_ready)return APP_ERR_UNSUPPORTED;
    int e=dispatch(type,timer,NULL,0);
    if(e==APP_ERR_RUNTIME)(void)app_runtime_shutdown();
    return e;
}
