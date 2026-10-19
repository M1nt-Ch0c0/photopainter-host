#include "esp_elf.h"
#include "photoframe_plugin.h"
#include "app_runtime.h"
esp_elf_symbol_table_t g_esp_photoframe_elfsyms[] = {{0}};
static int fail_load, result, live, peak, calls, report = 1;
static int v2_mode,display=1,fail_event,trace[128],trace_count;
void mock_v2(int mode,int shown,int fail) {v2_mode=mode;display=shown;fail_event=fail;trace_count=0;}
int mock_trace(int i){return i<trace_count?trace[i]:-1;}
extern void photoframe_host_report_result(int result);
void mock_plugin_reset(void) { fail_load = result = live = peak = calls = 0; report = 1; }
void mock_plugin_fail_load(void) { fail_load = 1; }
void mock_plugin_result(int r, int reports) { result = r; report = reports; }
int mock_plugin_live(void) { return live; }
int mock_plugin_peak(void) { return peak; }
int mock_plugin_calls(void) { return calls; }
int esp_elf_init(esp_elf_t *elf) { elf->live = 1; if (++live > peak) peak = live; return 0; }
int esp_elf_relocate(esp_elf_t *elf, const unsigned char *data) {
    (void)elf; (void)data;
    if (fail_load) { fail_load = 0; return -1; }
    return 0;
}
void esp_elf_deinit(esp_elf_t *elf) { if (elf->live) { --live; elf->live = 0; } }
int esp_elf_register_symbol(esp_elf_symbol_table_t *t) { (void)t; return 0; }
int esp_elf_request(esp_elf_t *elf, int opt, int argc, char **argv) {
    (void)elf; (void)opt; (void)argc; (void)argv; ++calls;
    const app_context_v2_t *c=app_host_context_v2();
    if(c) {
        if(trace_count<128)trace[trace_count++]=c->event;
        app_result_v2_t r={sizeof(r),c->event_id,c->generation,
            c->event==(unsigned)fail_event?APP_ERR_RUNTIME:APP_OK,
            c->event==APP_EVENT_START?APP_READY:0};
        if(v2_mode==3)r.generation++;
        if(v2_mode==4)r.flags=0;
        if(v2_mode!=1)app_host_complete_v2(&r);
        if(v2_mode==2)app_host_complete_v2(&r);
    } else if (report) photoframe_host_report_result(result);
    return 0;
}

#include "app_services.h"
esp_err_t app_services_init(void){return ESP_OK;}
void app_services_open(const char *id,uint32_t gen){(void)id;(void)gen;}
void app_services_close(void){}
esp_err_t app_services_begin(const app_context_v2_t *c){(void)c;return ESP_OK;}
void app_services_end(void){}
bool app_services_display_completed(void){return display;}
bool app_services_connected(void){return false;}

esp_err_t app_services_legacy_begin(void){return ESP_OK;}
