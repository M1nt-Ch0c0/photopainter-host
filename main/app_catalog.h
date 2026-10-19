#pragma once
#include "module_slots.h"
#define APP_LIMIT 5
#define APP_ID_BYTES 32
#define APP_CATALOG_VERSION 1
/* Physical bank index is array index, each permanently contains two 1 MiB slots. */
typedef struct { char id[APP_ID_BYTES]; module_state_t slots; } app_entry_t;
typedef struct {
    uint32_t version;
    int32_t selected, trial_app;
    app_entry_t apps[APP_LIMIT];
} app_catalog_t;
bool app_id_valid(const char *id);
const app_catalog_t *app_catalog_get(void);
int app_catalog_find(const char *id);
int app_catalog_running(void);
esp_err_t app_catalog_stage(const char *id, const uint8_t *package, size_t size);
esp_err_t app_catalog_read(int app, int slot, uint8_t **data, module_header_t *header);
esp_err_t app_catalog_begin(int app, bool update);
esp_err_t app_catalog_cancel(void);
esp_err_t app_catalog_rollback(int app);
esp_err_t app_catalog_remove(int app);
