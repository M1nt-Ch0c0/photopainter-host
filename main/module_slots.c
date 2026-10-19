#include "app_catalog.h"
#include "app_manifest.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sha/sha_core.h"
#include <stdlib.h>
#include <string.h>

static const esp_partition_t *legacy[2], *arena;
static nvs_handle_t nvs;
static app_catalog_t catalog;
static bool initialized;
static const module_state_t empty = {-1, -1, -1, 0};

bool app_id_valid(const char *id)
{
    if (!id) return false;
    size_t n = strnlen(id, APP_ID_BYTES);
    if (!n || n >= APP_ID_BYTES) return false;
    for (size_t i = 0; i < n; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-' || id[i] == '_')) return false;
    return true;
}
static esp_err_t save(app_catalog_t next)
{
    esp_err_t e = nvs_set_blob(nvs, "catalog", &next, sizeof(next));
    if (e == ESP_OK) e = nvs_commit(nvs);
    if (e == ESP_OK) catalog = next;
    return e;
}
const app_catalog_t *app_catalog_get(void) { return initialized ? &catalog : NULL; }
int app_catalog_find(const char *id)
{
    if (!initialized || !app_id_valid(id)) return -1;
    for (int i = 0; i < APP_LIMIT; ++i)
        if (!strcmp(catalog.apps[i].id, id)) return i;
    return -1;
}
int app_catalog_running(void) { return catalog.trial_app >= 0 ? catalog.trial_app : catalog.selected; }
module_state_t module_slots_state(void)
{
    int i = app_catalog_running();
    return initialized && i >= 0 ? catalog.apps[i].slots : empty;
}
static bool entry_exists(int i) { return initialized && i >= 0 && i < APP_LIMIT && catalog.apps[i].id[0]; }
static bool valid_state(module_state_t s)
{
    return s.active >= -1 && s.active < 2 && s.previous >= -1 && s.previous < 2 &&
           s.pending >= -1 && s.pending < 2 && (s.trial == 0 || s.trial == 1) &&
           (s.previous < 0 || s.previous != s.active) &&
           (s.pending < 0 || s.pending != s.active) && (!s.trial || s.pending >= 0) &&
           (s.active >= 0 || s.previous < 0);
}
static bool valid_catalog(void)
{
    if (catalog.version != APP_CATALOG_VERSION || catalog.selected < -1 || catalog.selected >= APP_LIMIT ||
        catalog.trial_app < -1 || catalog.trial_app >= APP_LIMIT) return false;
    for (int i = 0; i < APP_LIMIT; ++i) {
        const app_entry_t *a = &catalog.apps[i];
        if (!a->id[0]) {
            if (catalog.selected == i || catalog.trial_app == i) return false;
            continue;
        }
        if (!app_id_valid(a->id) || !valid_state(a->slots) ||
            (a->slots.trial && catalog.trial_app != i)) return false;
        for (int j = 0; j < i; ++j)
            if (!strcmp(a->id, catalog.apps[j].id)) return false;
    }
    if (catalog.selected >= 0 && catalog.apps[catalog.selected].slots.active < 0) return false;
    if (catalog.trial_app >= 0) {
        module_state_t s = catalog.apps[catalog.trial_app].slots;
        if (!s.trial && s.active < 0) return false;
    }
    return true;
}
esp_err_t module_slots_init(void)
{
    initialized = false;
    legacy[0] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "elf_a");
    legacy[1] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x41, "elf_b");
    arena = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x42, "app_slots");
    if (!legacy[0] || !legacy[1] || !arena || legacy[0]->size != MODULE_SLOT_BYTES ||
        legacy[1]->size != MODULE_SLOT_BYTES || arena->size != (APP_LIMIT - 1) * 2 * MODULE_SLOT_BYTES)
        return ESP_ERR_NOT_FOUND;
    esp_err_t e = nvs_flash_init_partition("elf_state");
    if (e != ESP_OK) return e;
    e = nvs_open_from_partition("elf_state", "module", NVS_READWRITE, &nvs);
    if (e != ESP_OK) return e;
    size_t size = sizeof(catalog);
    e = nvs_get_blob(nvs, "catalog", &catalog, &size);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        memset(&catalog, 0, sizeof(catalog));
        catalog.version = APP_CATALOG_VERSION;
        catalog.selected = 0;
        catalog.trial_app = -1;
        strcpy(catalog.apps[0].id, "photoframe");
        module_state_t old = {0, -1, -1, 0};
        size = sizeof(old);
        e = nvs_get_blob(nvs, "state", &old, &size);
        if (e != ESP_ERR_NVS_NOT_FOUND && (e != ESP_OK || size != sizeof(old) || !valid_state(old) || old.active < 0)) return ESP_ERR_INVALID_STATE;
        if (old.trial) { old.trial = 0; old.pending = -1; }
        catalog.apps[0].slots = old;
        for (int i = 1; i < APP_LIMIT; ++i) catalog.apps[i].slots = empty;
        e = save(catalog);
    } else if (e == ESP_OK && (size != sizeof(catalog) || !valid_catalog())) {
        return ESP_ERR_INVALID_STATE;
    }
    if (e != ESP_OK) return e;
    initialized = true;
    if (catalog.trial_app >= 0) {
        e = app_catalog_cancel();
        if (e != ESP_OK) initialized = false;
    }
    return e;
}
static const esp_partition_t *location(int app, int slot, size_t *offset)
{
    if (!entry_exists(app) || slot < 0 || slot > 1) return NULL;
    *offset = app ? ((app - 1) * 2 + slot) * MODULE_SLOT_BYTES : 0;
    return app ? arena : legacy[slot];
}
static bool valid_header(const module_header_t *h)
{
    return h->magic == MODULE_MAGIC && (h->format == 1 || h->format == 2) && (h->abi == 1 || (h->format == 2 && h->abi == 2)) &&
           h->length >= 52 && h->length <= MODULE_MAX_BYTES;
}
static bool valid_identity(const module_header_t *h, const char id[APP_ID_BYTES], const char *expected)
{
    return h->format == 1 ? !strcmp(expected, "photoframe") : app_id_valid(id) && !strcmp(id, expected);
}
static bool valid_data(const module_header_t *h, const uint8_t *p, const char *id)
{
    app_manifest_v2_t manifest;
    if (h->abi == 2 && (!app_manifest_read(p,h->length,&manifest) || strcmp(id,manifest.id))) return false;
    uint8_t hash[32];
    if (!module_elf_valid(p, h->length)) return false;
    esp_sha(SHA2_256, p, h->length, hash);
    return !memcmp(hash, h->sha256, 32);
}
esp_err_t app_catalog_read(int app, int slot, uint8_t **data, module_header_t *h)
{
    if (!data || !h) return ESP_ERR_INVALID_ARG;
    *data = NULL;
    size_t offset;
    const esp_partition_t *p = location(app, slot, &offset);
    if (!p) return ESP_ERR_INVALID_ARG;
    esp_err_t e = esp_partition_read(p, offset, h, sizeof(*h));
    if (e != ESP_OK) return e;
    if (!valid_header(h)) return ESP_ERR_INVALID_VERSION;
    char id[APP_ID_BYTES];
    e = esp_partition_read(p, offset + sizeof(*h), id, sizeof(id));
    if (e != ESP_OK) return e;
    if (!valid_identity(h, id, catalog.apps[app].id)) return ESP_ERR_INVALID_ARG;
    uint8_t *payload = heap_caps_malloc(h->length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!payload) return ESP_ERR_NO_MEM;
    e = esp_partition_read(p, offset + MODULE_HEADER_BYTES, payload, h->length);
    if (e == ESP_OK && !valid_data(h, payload, catalog.apps[app].id)) e = ESP_ERR_INVALID_CRC;
    if (e != ESP_OK) free(payload); else *data = payload;
    return e;
}
esp_err_t module_slots_read(int slot, uint8_t **data, module_header_t *h)
{ return app_catalog_read(app_catalog_running(), slot, data, h); }
esp_err_t app_catalog_stage(const char *id, const uint8_t *package, size_t size)
{
    if (!initialized || catalog.trial_app >= 0) return ESP_ERR_INVALID_STATE;
    if (!app_id_valid(id) || !package) return ESP_ERR_INVALID_ARG;
    if (size < MODULE_HEADER_BYTES + 52 || size > MODULE_SLOT_BYTES) return ESP_ERR_INVALID_SIZE;
    module_header_t h;
    memcpy(&h, package, sizeof(h));
    if (!valid_header(&h) || size != MODULE_HEADER_BYTES + h.length ||
        !valid_identity(&h, (const char *)package + sizeof(h), id) || !valid_data(&h, package + MODULE_HEADER_BYTES, id)) return ESP_ERR_INVALID_ARG;
    int app = app_catalog_find(id);
    app_catalog_t next = catalog;
    if (app < 0) {
        for (int i = 0; i < APP_LIMIT; ++i) if (!next.apps[i].id[0]) { app = i; break; }
        if (app < 0) return ESP_ERR_NO_MEM;
        strcpy(next.apps[app].id, id);
        next.apps[app].slots = empty;
    }
    module_state_t *s = &next.apps[app].slots;
    if (s->pending >= 0) return ESP_ERR_INVALID_STATE;
    int target = s->active < 0 ? 0 : 1 - s->active;
    s->previous = -1;
    /* Reserve directory bank / invalidate fallback atomically before flash writes. */
    esp_err_t e = save(next);
    if (e != ESP_OK) return e;
    size_t offset;
    const esp_partition_t *p = location(app, target, &offset);
    e = esp_partition_erase_range(p, offset, MODULE_SLOT_BYTES);
    if (e == ESP_OK) e = esp_partition_write(p, offset + MODULE_HEADER_BYTES, package + MODULE_HEADER_BYTES, h.length);
    uint8_t *verify = heap_caps_malloc(h.length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!verify) return ESP_ERR_NO_MEM;
    if (e == ESP_OK) e = esp_partition_read(p, offset + MODULE_HEADER_BYTES, verify, h.length);
    if (e == ESP_OK && !valid_data(&h, verify, id)) e = ESP_ERR_INVALID_CRC;
    free(verify);
    /* Header (including identity) is the last flash write; only journal exposes it. */
    if (e == ESP_OK) e = esp_partition_write(p, offset, package, sizeof(h) + APP_ID_BYTES);
    if (e != ESP_OK) return e;
    s->pending = target;
    return save(next);
}
esp_err_t module_slots_stage(const uint8_t *p, size_t n)
{ return app_catalog_stage("photoframe", p, n); }
esp_err_t app_catalog_begin(int app, bool update)
{
    if (!entry_exists(app) || catalog.trial_app >= 0) return ESP_ERR_INVALID_STATE;
    module_state_t s = catalog.apps[app].slots;
    if (update ? s.pending < 0 : s.active < 0) return ESP_ERR_INVALID_STATE;
    if (!update && app == catalog.selected) return ESP_OK;
    app_catalog_t next = catalog;
    next.trial_app = app;
    next.apps[app].slots.trial = update ? 1 : 0;
    return save(next);
}
esp_err_t module_slots_begin_trial(void) { return app_catalog_begin(app_catalog_running(), true); }
esp_err_t module_slots_confirm(void)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    int app = catalog.trial_app;
    if (app < 0) return ESP_OK;
    app_catalog_t next = catalog;
    module_state_t *s = &next.apps[app].slots;
    if (s->trial) *s = (module_state_t){s->pending, s->active, -1, 0};
    next.selected = app;
    next.trial_app = -1;
    return save(next);
}
esp_err_t app_catalog_cancel(void)
{
    if (!initialized || catalog.trial_app < 0) return ESP_ERR_INVALID_STATE;
    app_catalog_t next = catalog;
    module_state_t *s = &next.apps[next.trial_app].slots;
    if (s->trial) { s->pending = -1; s->trial = 0; }
    next.trial_app = -1;
    return save(next);
}
esp_err_t app_catalog_rollback(int app)
{
    if (!entry_exists(app)) return ESP_ERR_INVALID_ARG;
    if (catalog.trial_app >= 0) return catalog.trial_app == app ? app_catalog_cancel() : ESP_ERR_INVALID_STATE;
    app_catalog_t next = catalog;
    module_state_t *s = &next.apps[app].slots;
    if (s->pending >= 0) { s->pending = -1; s->trial = 0; }
    else if (s->previous >= 0) { int old = s->active; s->active = s->previous; s->previous = old; }
    else return ESP_ERR_INVALID_STATE;
    return save(next);
}
esp_err_t module_slots_rollback(void) { return app_catalog_rollback(app_catalog_running()); }
esp_err_t module_slots_recover(void)
{
    if (!initialized || catalog.trial_app >= 0 || catalog.selected < 0) return ESP_ERR_INVALID_STATE;
    app_catalog_t next = catalog;
    module_state_t *s = &next.apps[next.selected].slots;
    if (s->previous < 0) return ESP_ERR_NOT_FOUND;
    *s = (module_state_t){s->previous, -1, -1, 0};
    return save(next);
}
esp_err_t app_catalog_remove(int app)
{
    if (!entry_exists(app) || catalog.trial_app >= 0 || catalog.selected == app) return ESP_ERR_INVALID_STATE;
    app_catalog_t next = catalog;
    memset(&next.apps[app], 0, sizeof(next.apps[app]));
    next.apps[app].slots = empty;
    /* Freed bytes are never loaded and are erased when next allocated. */
    return save(next);
}
