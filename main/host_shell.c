/* Read-only shell commands. Application state comes only from the manager's
 * published STATUS snapshot (the same source as op 3), so no catalog, Flash or
 * application call happens on the BLE worker and no admission is needed. */
#include "host_shell.h"
#include "app_manager.h"
#include "app_manifest.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>

#define STATUS_BYTES 4096u
typedef uint8_t (*command_fn)(int, char **, const uint8_t *, pp_shell_out *);
typedef struct {const char *name, *help; command_fn run;} command_t;

static char *status_text(void) {
    char *text = heap_caps_malloc(STATUS_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!text) text = malloc(STATUS_BYTES);
    if (text) {text[0] = 0; app_manager_status(text, STATUS_BYTES);}
    return text;
}
static cJSON *status_json(pp_shell_out *o) {
    char *text = status_text();
    cJSON *json = text ? cJSON_Parse(text) : NULL;
    free(text);
    if (!json) pp_shell_printf(o, "status unavailable\n");
    return json;
}
static double number(const cJSON *o, const char *name, double fallback) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, name);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}
static bool flag(const cJSON *o, const char *name) {
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, name));
}
static const char *string(const cJSON *o, const char *name) {
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, name));
    return s ? s : "";
}
static const cJSON *app_at(const cJSON *status, int bank) {
    const cJSON *a;
    cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(status, "apps"))
        if ((int)number(a, "bank", -1) == bank) return a;
    return NULL;
}
static long slot_version(const cJSON *app, int slot) {
    const cJSON *v = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(app, "versions"), slot);
    return slot >= 0 && cJSON_IsNumber(v) ? (long)v->valuedouble : -1;
}

static uint8_t run_help(int, char **, const uint8_t *, pp_shell_out *);
static uint8_t run_uname(int argc, char **argv, const uint8_t *info, pp_shell_out *o) {
    (void)argc; (void)argv; (void)info;
    const esp_app_desc_t *d = esp_app_get_description();
    esp_chip_info_t chip; esp_chip_info(&chip);
    pp_shell_printf(o, "host     %s %s\n", d->project_name, d->version);
    pp_shell_printf(o, "built    %s %s\n", d->date, d->time);
    pp_shell_printf(o, "abi      host exports 1, payload up to %u.%u\n", APP_ABI_MAJOR, APP_ABI_MINOR);
    pp_shell_printf(o, "esp-idf  %s\n", esp_get_idf_version());
    pp_shell_printf(o, "chip     %s rev v%d.%d, %d cores\n", CONFIG_IDF_TARGET,
                    chip.revision / 100, chip.revision % 100, chip.cores);
    return PP_SHELL_EXIT_OK;
}
static uint8_t run_uptime(int argc, char **argv, const uint8_t *info, pp_shell_out *o) {
    (void)argc; (void)argv; (void)info;
    uint64_t ms = (uint64_t)esp_timer_get_time() / 1000u, s = ms / 1000u;
    pp_shell_printf(o, "up %llud %02llu:%02llu:%02llu (%llu ms), reset reason %d\n",
                    (unsigned long long)(s / 86400u), (unsigned long long)(s / 3600u % 24u),
                    (unsigned long long)(s / 60u % 60u), (unsigned long long)(s % 60u),
                    (unsigned long long)ms, (int)esp_reset_reason());
    return PP_SHELL_EXIT_OK;
}
static uint8_t run_free(int argc, char **argv, const uint8_t *info, pp_shell_out *o) {
    (void)argc; (void)argv; (void)info;
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    pp_shell_printf(o, "%-9s %10s %10s %10s %10s\n", "", "total", "free", "min-free", "largest");
    pp_shell_printf(o, "%-9s %10u %10u %10u %10u\n", "internal",
                    (unsigned)heap_caps_get_total_size(internal), (unsigned)heap_caps_get_free_size(internal),
                    (unsigned)heap_caps_get_minimum_free_size(internal),
                    (unsigned)heap_caps_get_largest_free_block(internal));
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
        pp_shell_printf(o, "%-9s %10u %10u %10u %10u\n", "psram",
                        (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    return PP_SHELL_EXIT_OK;
}
static uint8_t run_apps(int argc, char **argv, const uint8_t *info, pp_shell_out *o) {
    (void)argc; (void)argv; (void)info;
    cJSON *s = status_json(o);
    if (!s) return PP_SHELL_EXIT_ERROR;
    int loaded = (int)number(s, "loaded_app", -1), selected = (int)number(s, "selected", -1);
    int trial = (int)number(s, "trial_app", -1);
    unsigned count = 0; const cJSON *a;
    pp_shell_printf(o, "BANK ID                               VERSION SLOT ABI STATE\n");
    cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(s, "apps")) {
        int bank = (int)number(a, "bank", -1), slot = (int)number(a, "active", -1);
        long version = slot_version(a, slot);
        char v[12] = "-", sl[12] = "-";
        if (version >= 0) snprintf(v, sizeof(v), "%ld", version);
        if (slot >= 0) snprintf(sl, sizeof(sl), "%d", slot);
        pp_shell_printf(o, "%-4d %-32s %7s %4s %3d%s%s%s%s\n", bank, string(a, "id"), v, sl,
                        (int)number(a, "abi", 0), bank == loaded ? " foreground" : "",
                        bank == selected ? " selected" : "",
                        bank == trial || number(a, "trial", -1) >= 0 ? " trial" : "",
                        number(a, "pending", -1) >= 0 ? " pending" : "");
        count++;
    }
    pp_shell_printf(o, "%u of %d banks used\n", count, (int)number(s, "capacity", 0));
    cJSON_Delete(s); return PP_SHELL_EXIT_OK;
}
static uint8_t run_ps(int argc, char **argv, const uint8_t *info, pp_shell_out *o) {
    (void)argc; (void)argv; (void)info;
    cJSON *s = status_json(o);
    if (!s) return PP_SHELL_EXIT_ERROR;
    int loaded = (int)number(s, "loaded_app", -1);
    const cJSON *a = loaded >= 0 ? app_at(s, loaded) : NULL;
    if (a) {
        const char *name = string(a, "name");
        pp_shell_printf(o, "app        %s (bank %d)%s%s\n", string(a, "id"), loaded,
                        name[0] && strcmp(name, string(a, "id")) ? ", " : "",
                        name[0] && strcmp(name, string(a, "id")) ? name : "");
        pp_shell_printf(o, "slot       %d, version %ld\n", (int)number(s, "loaded", -1), (long)number(s, "version", 0));
    } else pp_shell_printf(o, "app        none\n");
    pp_shell_printf(o, "abi        %d.%d\n", (int)number(s, "payload_abi", 0), (int)number(s, "payload_abi_minor", 0));
    pp_shell_printf(o, "phase      %s\n", string(s, "phase"));
    pp_shell_printf(o, "ready      %s, runtime %s, confirmed %s\n", flag(s, "ready") ? "yes" : "no",
                    flag(s, "runtime_ready") ? "yes" : "no", flag(s, "confirmed") ? "yes" : "no");
    pp_shell_printf(o, "generation %lu\n", (unsigned long)number(s, "generation", 0));
    pp_shell_printf(o, "last       %s -> %s, result %d\n", string(s, "last_switch_source"),
                    string(s, "last_switch_target"), (int)number(s, "last_result", 0));
    pp_shell_printf(o, "busy       %s\n", app_manager_busy() ? "yes" : "no");
    cJSON_Delete(s); return PP_SHELL_EXIT_OK;
}
static uint8_t run_status(int argc, char **argv, const uint8_t *info, pp_shell_out *o) {
    (void)argc; (void)argv; (void)info;
    char *text = status_text();
    if (!text) {pp_shell_printf(o, "out of memory\n"); return PP_SHELL_EXIT_ERROR;}
    pp_shell_write(o, text, strnlen(text, STATUS_BYTES)); pp_shell_write(o, "\n", 1);
    free(text); return PP_SHELL_EXIT_OK;
}
static uint8_t run_wifi(int argc, char **argv, const uint8_t *info, pp_shell_out *o) {
    (void)argc; (void)argv;
    /* Discovery-record fields only: never SSID plaintext or credentials. */
    static const char *const states[] = {"offline", "connecting", "connected", "config-invalid"};
    pp_shell_printf(o, "state %s\n", info[1] < 4 ? states[info[1]] : "unknown");
    if (info[1] == 2) pp_shell_printf(o, "ipv4  %u.%u.%u.%u\n", info[8], info[9], info[10], info[11]);
    return PP_SHELL_EXIT_OK;
}
static const command_t commands[] = {
    {"help", "list commands", run_help},
    {"uname", "host firmware, ABI, ESP-IDF and chip", run_uname},
    {"version", "same as uname", run_uname},
    {"uptime", "time since boot and reset reason", run_uptime},
    {"free", "internal and PSRAM heap", run_free},
    {"apps", "installed applications and slots", run_apps},
    {"ps", "foreground application state", run_ps},
    {"current", "same as ps", run_ps},
    {"status", "STATUS (op 3) JSON", run_status},
    {"wifi", "station state and IPv4", run_wifi},
};
static uint8_t run_help(int argc, char **argv, const uint8_t *info, pp_shell_out *o) {
    (void)argc; (void)argv; (void)info;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
        pp_shell_printf(o, "%-8s %s\n", commands[i].name, commands[i].help);
    return PP_SHELL_EXIT_OK;
}
uint8_t host_shell_exec(int argc, char **argv, const uint8_t info[20], pp_shell_out *o) {
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strcmp(argv[0], commands[i].name)) continue;
        if (argc != 1) {pp_shell_printf(o, "usage: %s\n", commands[i].name); return PP_SHELL_EXIT_USAGE;}
        return commands[i].run(argc, argv, info, o);
    }
    pp_shell_printf(o, "%s: command not found\n", argv[0]);
    return PP_SHELL_EXIT_UNKNOWN;
}
