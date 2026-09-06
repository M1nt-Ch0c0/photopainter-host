#include "photoframe_plugin.h"

#include "app_catalog.h"
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_elf.h"
#include "esp_log.h"

static const char *TAG = "photoframe_elf";
static const uint8_t *s_png;
static size_t s_png_size;
static volatile int s_reported_result = INT_MIN;
static bool s_ready;
static esp_elf_t s_elf;

static uint32_t s_version;
static int s_slot = -1;
static int s_app = -1;
extern esp_elf_symbol_table_t g_esp_photoframe_elfsyms[];

/* These three functions are the deliberately tiny payload-to-host ABI. The
 * generated symbol table references them by name, so keep them non-static. */
__attribute__((used, noinline)) const uint8_t *photoframe_host_png_data(void)
{
    return s_png;
}

__attribute__((used, noinline)) size_t photoframe_host_png_size(void)
{
    return s_png_size;
}

__attribute__((used, noinline)) void photoframe_host_report_result(int result)
{
    s_reported_result = result;
}

static void photoframe_plugin_deinit(void)
{
    if (!s_ready)
    {
        return;
    }
    s_ready = false;
    s_slot = s_app = -1;
    s_version = 0;
    esp_elf_deinit(&s_elf);
}

static esp_err_t load_slot(int slot)
{
    photoframe_plugin_deinit();
    uint8_t *payload = NULL;
    module_header_t header;
    esp_err_t err = module_slots_read(slot, &payload, &header);
    if (err != ESP_OK)
        return err;
    memset(&s_elf, 0, sizeof(s_elf));
    int result = esp_elf_init(&s_elf);
    if (result >= 0)
        result = esp_elf_relocate(&s_elf, payload);
    free(payload);
    if (result < 0)
    {
        esp_elf_deinit(&s_elf);
        return ESP_FAIL;
    }
    s_ready = true;
    s_slot = slot;
    s_app = app_catalog_running();
    s_version = header.version;
    ESP_LOGI(TAG, "independent payload ready: slot=%d version=%lu bytes=%lu", slot,
             (unsigned long)s_version, (unsigned long)header.length);
    return ESP_OK;
}

esp_err_t photoframe_plugin_init(void)
{
    esp_err_t err = module_slots_init();
    if (err != ESP_OK)
        return err;
    if (esp_elf_register_symbol(g_esp_photoframe_elfsyms) < 0)
        return ESP_FAIL;
    err = load_slot(module_slots_state().active);
    if (err != ESP_OK && module_slots_recover() == ESP_OK)
        err = load_slot(module_slots_state().active);
    return err;
}

static esp_err_t load_current(void)
{
    module_state_t s = module_slots_state();
    return load_slot(s.trial ? s.pending : s.active);
}

esp_err_t photoframe_plugin_select(const char *id, bool update)
{
    int app = app_catalog_find(id);
    esp_err_t err = app_catalog_begin(app, update);
    if (err != ESP_OK) return err;
    err = load_current();
    if (err != ESP_OK) {
        if (app_catalog_cancel() == ESP_OK) (void)load_current();
        /* If journal cancellation fails, keep execution disabled until reboot. */
    }
    return err;
}
esp_err_t photoframe_plugin_activate(void)
{ return photoframe_plugin_select("photoframe", true); }

esp_err_t photoframe_plugin_rollback_app(const char *id)
{
    int app = app_catalog_find(id);
    bool reload = app == app_catalog_running();
    esp_err_t err = app_catalog_rollback(app);
    if (err == ESP_OK && reload) {
        err = load_current();
        if (err != ESP_OK && module_slots_recover() == ESP_OK) (void)load_current();
    }
    return err;
}
esp_err_t photoframe_plugin_rollback(void)
{
    const app_catalog_t *c = app_catalog_get();
    int app = app_catalog_running();
    return c && app >= 0 ? photoframe_plugin_rollback_app(c->apps[app].id) : ESP_ERR_INVALID_STATE;
}
int photoframe_plugin_app(void) { return s_app; }
static bool in_trial(void)
{
    const app_catalog_t *c = app_catalog_get();
    return c && c->trial_app >= 0;
}

uint32_t photoframe_plugin_version(void) { return s_version; }
int photoframe_plugin_slot(void) { return s_slot; }

bool photoframe_plugin_is_ready(void) { return s_ready; }

int photoframe_plugin_render(const uint8_t *png, size_t size)
{
    if (!s_ready)
    {
        return PHOTOFRAME_RESULT_UNAVAILABLE;
    }
    if (png == NULL || size == 0)
    {
        return PHOTOFRAME_RESULT_ARGUMENT;
    }

    s_png = png;
    s_png_size = size;
    s_reported_result = INT_MIN;
    int request_result = esp_elf_request(&s_elf, 0, 0, NULL);
    s_png = NULL;
    s_png_size = 0;

    if (request_result < 0)
    {
        ESP_LOGE(TAG, "payload request failed: %d", request_result);
        photoframe_plugin_deinit();
        if (in_trial())
            (void)photoframe_plugin_rollback();
        return PHOTOFRAME_RESULT_UNAVAILABLE;
    }
    if (s_reported_result == INT_MIN)
    {
        ESP_LOGE(TAG, "payload returned without reporting a result");
        photoframe_plugin_deinit();
        if (in_trial())
            (void)photoframe_plugin_rollback();
        return PHOTOFRAME_RESULT_UNAVAILABLE;
    }
    int result = s_reported_result;
    if (result == PHOTOFRAME_RESULT_OK)
    {
        if (module_slots_confirm() != ESP_OK)
            ESP_LOGE(TAG, "trial confirmation failed; next reset will roll back");
    }
    else if (result <= PHOTOFRAME_RESULT_DISPLAY && in_trial())
    {
        (void)photoframe_plugin_rollback();
    }
    return result;
}
