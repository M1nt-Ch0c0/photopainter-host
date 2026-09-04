#include "photoframe_plugin.h"

#include <limits.h>
#include <stdbool.h>

#include "esp_elf.h"
#include "esp_log.h"

static const char *TAG = "photoframe_elf";
static const uint8_t *s_png;
static size_t s_png_size;
static volatile int s_reported_result = INT_MIN;
static bool s_ready;
static esp_elf_t s_elf;

extern const uint8_t photoframe_elf_start[]
    asm("_binary_photoframe_bin_start");
extern const uint8_t photoframe_elf_end[]
    asm("_binary_photoframe_bin_end");
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
    if (!s_ready) {
        return;
    }
    s_ready = false;
    (void)esp_elf_unregister_symbol(g_esp_photoframe_elfsyms);
    esp_elf_deinit(&s_elf);
}

esp_err_t photoframe_plugin_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    uintptr_t embedded_start = (uintptr_t)&photoframe_elf_start[0];
    uintptr_t embedded_end = (uintptr_t)&photoframe_elf_end[0];
    if (embedded_end <= embedded_start) {
        return ESP_ERR_INVALID_SIZE;
    }

    int result = esp_elf_init(&s_elf);
    if (result < 0) {
        ESP_LOGE(TAG, "esp_elf_init failed: %d", result);
        return ESP_FAIL;
    }
    result = esp_elf_register_symbol(g_esp_photoframe_elfsyms);
    if (result < 0) {
        ESP_LOGE(TAG, "symbol registration failed: %d", result);
        esp_elf_deinit(&s_elf);
        return ESP_FAIL;
    }
    result = esp_elf_relocate(&s_elf, (const uint8_t *)embedded_start);
    if (result < 0) {
        ESP_LOGE(TAG, "payload relocation failed: %d", result);
        esp_elf_unregister_symbol(g_esp_photoframe_elfsyms);
        esp_elf_deinit(&s_elf);
        return ESP_FAIL;
    }
    s_ready = true;
    ESP_LOGI(TAG, "photoframe payload ready (%u bytes)",
             (unsigned)(embedded_end - embedded_start));
    return ESP_OK;
}

bool photoframe_plugin_is_ready(void)
{
    return s_ready;
}

int photoframe_plugin_render(const uint8_t *png, size_t size)
{
    if (!s_ready) {
        return PHOTOFRAME_RESULT_UNAVAILABLE;
    }
    if (png == NULL || size == 0) {
        return PHOTOFRAME_RESULT_ARGUMENT;
    }

    s_png = png;
    s_png_size = size;
    s_reported_result = INT_MIN;
    int request_result = esp_elf_request(&s_elf, 0, 0, NULL);
    s_png = NULL;
    s_png_size = 0;

    if (request_result < 0) {
        ESP_LOGE(TAG, "payload request failed: %d", request_result);
        photoframe_plugin_deinit();
        return PHOTOFRAME_RESULT_UNAVAILABLE;
    }
    if (s_reported_result == INT_MIN) {
        ESP_LOGE(TAG, "payload returned without reporting a result");
        photoframe_plugin_deinit();
        return PHOTOFRAME_RESULT_UNAVAILABLE;
    }
    return s_reported_result;
}
