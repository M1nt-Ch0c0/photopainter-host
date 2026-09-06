#include "esp_elf.h"
#include "photoframe_plugin.h"
esp_elf_symbol_table_t g_esp_photoframe_elfsyms[] = {{0}};
static int fail_load, result, live, peak, calls, report = 1;
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
    if (report) photoframe_host_report_result(result);
    return 0;
}
