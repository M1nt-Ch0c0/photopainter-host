#include "app_boot.h"
#include "app_catalog.h"
#include "nvs.h"
#include <stdint.h>
#include <string.h>
/* Phase 1: active entered; 2: fallback intent before catalog commit;
 * 3: fallback entered. A reset in phase 3 never executes that image again.
 * The checksum detects damaged records; this is not an authentication scheme. */
typedef struct {
    uint32_t magic,phase;
    int32_t bank,active,previous;
    char id[32];
    uint32_t checksum;
} record_t;
static record_t record;
static nvs_handle_t handle;
static bool opened;
static uint32_t checksum(const record_t *r) {
    const uint8_t *p=(const uint8_t *)r;uint32_t v=2166136261u;
    for(size_t i=0;i<sizeof(*r)-4;i++)v=(v^p[i])*16777619u;
    return v;
}
static esp_err_t write_record(void) {
    record.checksum=checksum(&record);
    esp_err_t e=nvs_set_blob(handle,"runtime_boot",&record,sizeof(record));
    return e==ESP_OK?nvs_commit(handle):e;
}
static bool identity(void) {
    const app_catalog_t *c=app_catalog_get();
    return c&&record.bank==c->selected&&record.bank>=0&&record.bank<APP_LIMIT&&
        !memcmp(record.id,c->apps[record.bank].id,sizeof(record.id))&&c->trial_app<0;
}
esp_err_t app_boot_complete(void) {
    if(!opened)return ESP_ERR_INVALID_STATE;
    memset(&record,0,sizeof(record));record.magic=0x32425452;
    return write_record();
}
esp_err_t app_boot_fallback(void) {
    if(!opened||!identity()||(record.phase!=1&&record.phase!=2)||record.previous<0)
        return ESP_ERR_INVALID_STATE;
    const app_catalog_t *c=app_catalog_get();module_state_t s=c->apps[record.bank].slots;
    bool before=s.active==record.active&&s.previous==record.previous;
    bool after=s.active==record.previous&&s.previous==-1&&s.pending==-1;
    if(!before&&!after)return ESP_ERR_INVALID_STATE;
    record.phase=2;
    esp_err_t e=write_record();if(e!=ESP_OK)return e;
    if(before){e=module_slots_recover();if(e!=ESP_OK)return e;}
    record.phase=3;
    return write_record();
}
esp_err_t app_boot_prepare(void) {
    opened=false;
    const app_catalog_t *c=app_catalog_get();
    if(!c||c->trial_app>=0||c->selected<0)return ESP_ERR_INVALID_STATE;
    esp_err_t e=nvs_open_from_partition("elf_state","runtime",NVS_READWRITE,&handle);
    if(e!=ESP_OK)return e;
    opened=true;size_t size=sizeof(record);
    e=nvs_get_blob(handle,"runtime_boot",&record,&size);
    if(e!=ESP_ERR_NVS_NOT_FOUND && (e!=ESP_OK||size!=sizeof(record)||
        record.magic!=0x32425452||record.checksum!=checksum(&record)||record.phase>3))
        return ESP_ERR_INVALID_STATE;
    if(e==ESP_OK&&record.phase) {
        if(!identity()||record.active<0||record.active>1||record.previous < -1||
            record.previous>1||record.previous==record.active||record.phase==3)
            return ESP_ERR_INVALID_STATE;
        return app_boot_fallback();
    }
    const app_entry_t *a=&c->apps[c->selected];
    record=(record_t){.magic=0x32425452,.phase=1,.bank=c->selected,
        .active=a->slots.active,.previous=a->slots.previous};
    memcpy(record.id,a->id,sizeof(record.id));
    return write_record();
}
