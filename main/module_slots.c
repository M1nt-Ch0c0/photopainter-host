#include "module_slots.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sha/sha_core.h"
#include <stdlib.h>
#include <string.h>

static const esp_partition_t *slots[2];
static nvs_handle_t nvs;
static module_state_t state;
static bool initialized;
static const char *TAG = "module_slots";
static esp_err_t save(module_state_t next)
{
    esp_err_t e = nvs_set_blob(nvs, "state", &next, sizeof(next));
    if (e == ESP_OK)
        e = nvs_commit(nvs);
    if (e == ESP_OK)
        state = next;
    return e;
}
module_state_t module_slots_state(void) { return state; }
static bool valid_header(const module_header_t *h)
{
    return h->magic == MODULE_MAGIC && h->format == 1 && h->abi == MODULE_ABI &&
           h->length >= 52 && h->length <= MODULE_MAX_BYTES;
}
static bool valid_data(const module_header_t *h, const uint8_t *p)
{
    uint8_t hash[32];
    if (!module_elf_valid(p, h->length))
        return false;
    esp_sha(SHA2_256, p, h->length, hash);
    return memcmp(hash, h->sha256, 32) == 0;
}
static esp_err_t initialize(void)
{
    slots[0] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "elf_a");
    slots[1] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x41, "elf_b");
    if (!slots[0] || !slots[1] || slots[0]->size != MODULE_SLOT_BYTES ||
        slots[1]->size != MODULE_SLOT_BYTES)
        return ESP_ERR_NOT_FOUND;
    esp_err_t e = nvs_flash_init_partition("elf_state");
    if (e != ESP_OK)
        return e; /* Never erase a damaged journal automatically. */
    e = nvs_open_from_partition("elf_state", "module", NVS_READWRITE, &nvs);
    if (e != ESP_OK)
        return e;
    size_t size = sizeof(state);
    e = nvs_get_blob(nvs, "state", &state, &size);
    if (e == ESP_ERR_NVS_NOT_FOUND)
        return save((module_state_t){0, -1, -1, 0});
    if (e != ESP_OK || size != sizeof(state) || state.active < 0 || state.active > 1 ||
        state.previous < -1 || state.previous > 1 || state.pending < -1 ||
        state.pending > 1 || state.previous == state.active ||
        state.pending == state.active || (state.trial != 0 && state.trial != 1))
        return ESP_ERR_INVALID_STATE;
    if (state.trial)
    {
        ESP_LOGW(TAG, "unfinished trial after reset; reverting to confirmed slot %d",
                 (int)state.active);
        module_state_t next = state;
        next.pending = -1;
        next.trial = 0;
        return save(next);
    }
    return ESP_OK;
}
esp_err_t module_slots_init(void)
{
    esp_err_t err = initialize();
    initialized = err == ESP_OK;
    return err;
}
esp_err_t module_slots_read(int slot, uint8_t **data, module_header_t *h)
{
    *data = NULL;
    if (slot < 0 || slot > 1 || !slots[slot])
        return ESP_ERR_INVALID_ARG;
    esp_err_t e = esp_partition_read(slots[slot], 0, h, sizeof(*h));
    if (e != ESP_OK)
        return e;
    if (!valid_header(h))
        return ESP_ERR_INVALID_VERSION;
    uint8_t *p = heap_caps_malloc(h->length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)
        return ESP_ERR_NO_MEM;
    e = esp_partition_read(slots[slot], MODULE_HEADER_BYTES, p, h->length);
    if (e == ESP_OK && !valid_data(h, p))
        e = ESP_ERR_INVALID_CRC;
    if (e != ESP_OK)
    {
        free(p);
        return e;
    }
    *data = p;
    return ESP_OK;
}
esp_err_t module_slots_stage(const uint8_t *package, size_t size)
{
    if (!initialized)
        return ESP_ERR_INVALID_STATE;
    if (state.pending >= 0 || state.trial)
        return ESP_ERR_INVALID_STATE;
    if (size < MODULE_HEADER_BYTES + 52 || size > MODULE_SLOT_BYTES)
        return ESP_ERR_INVALID_SIZE;
    module_header_t h;
    memcpy(&h, package, sizeof(h));
    if (!valid_header(&h) || size != MODULE_HEADER_BYTES + h.length ||
        !valid_data(&h, package + MODULE_HEADER_BYTES))
        return ESP_ERR_INVALID_ARG;
    int target = 1 - state.active;
    /* Invalidate the old fallback before overwriting it. Active is never touched. */
    module_state_t next = state;
    next.previous = -1;
    esp_err_t e = save(next);
    if (e != ESP_OK)
        return e;
    e = esp_partition_erase_range(slots[target], 0, MODULE_SLOT_BYTES);
    if (e == ESP_OK)
        e = esp_partition_write(slots[target], MODULE_HEADER_BYTES,
                                package + MODULE_HEADER_BYTES, h.length);
    /* Read back before committing the header. Power loss leaves an invalid slot. */
    uint8_t *verify = heap_caps_malloc(h.length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!verify)
        return ESP_ERR_NO_MEM;
    if (e == ESP_OK)
        e = esp_partition_read(slots[target], MODULE_HEADER_BYTES, verify, h.length);
    if (e == ESP_OK && !valid_data(&h, verify))
        e = ESP_ERR_INVALID_CRC;
    free(verify);
    if (e == ESP_OK)
        e = esp_partition_write(slots[target], 0, &h, sizeof(h));
    if (e != ESP_OK)
        return e;
    next.pending = target;
    next.trial = 0;
    ESP_LOGI(TAG, "staged version %lu in slot %d", (unsigned long)h.version, target);
    return save(next);
}
esp_err_t module_slots_begin_trial(void)
{
    if (!initialized)
        return ESP_ERR_INVALID_STATE;
    if (state.pending < 0 || state.trial)
        return ESP_ERR_INVALID_STATE;
    module_state_t next = state;
    next.trial = 1;
    return save(next);
}
esp_err_t module_slots_confirm(void)
{
    if (!initialized)
        return ESP_ERR_INVALID_STATE;
    if (!state.trial)
        return ESP_OK;
    module_state_t next = {state.pending, state.active, -1, 0};
    esp_err_t e = save(next);
    if (e == ESP_OK)
        ESP_LOGI(TAG, "confirmed slot %d after physical refresh", (int)state.active);
    return e;
}
esp_err_t module_slots_rollback(void)
{
    if (!initialized)
        return ESP_ERR_INVALID_STATE;
    module_state_t next = state;
    if (state.pending >= 0)
    {
        next.pending = -1;
        next.trial = 0;
    }
    else if (state.previous >= 0)
    {
        next.active = state.previous;
        next.previous = state.active;
    }
    else
        return ESP_ERR_INVALID_STATE;
    return save(next);
}
esp_err_t module_slots_recover(void)
{
    if (!initialized)
        return ESP_ERR_INVALID_STATE;
    if (state.previous < 0)
        return ESP_ERR_NOT_FOUND;
    module_state_t next = {state.previous, -1, -1, 0};
    return save(next);
}
