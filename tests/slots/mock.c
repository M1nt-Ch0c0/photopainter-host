#include "esp_partition.h"
#include "app_catalog.h"
#include "nvs.h"
#include <openssl/sha.h>
#include <string.h>
static unsigned char flash[3][8 * MODULE_SLOT_BYTES];
static app_catalog_t persisted, staged;
static unsigned char boot[56],boot_staged[56];
static int boot_exists,boot_dirty,catalog_dirty;
static int exists, fail_write, fail_commit, legacy_exists;
static module_state_t legacy_state;
static esp_partition_t parts[] = {{MODULE_SLOT_BYTES, 0}, {MODULE_SLOT_BYTES, 1}, {8 * MODULE_SLOT_BYTES, 2}};
void mock_reset(void)
{
    memset(flash, 255, sizeof(flash));
    exists = legacy_exists = boot_exists = boot_dirty = catalog_dirty = 0;
    fail_write = 0;
    fail_commit = 0;
}
void mock_fail_write(int n) { fail_write = n; }
void mock_fail_commit(int n) { fail_commit = n; }
const esp_partition_t *esp_partition_find_first(int a, int b, const char *c)
{
    (void)a;
    (void)c;
    return b == 0x40 ? parts : b == 0x41 ? parts + 1 : parts + 2;
}
esp_err_t esp_partition_read(const esp_partition_t *p, size_t o, void *d, size_t n)
{
    if (o + n > p->size)
        return ESP_FAIL;
    memcpy(d, flash[p->index] + o, n);
    return 0;
}
esp_err_t esp_partition_write(const esp_partition_t *p, size_t o, const void *d,
                              size_t n)
{
    if (fail_write > 0 && --fail_write == 0)
        return ESP_FAIL;
    if (o + n > p->size)
        return ESP_FAIL;
    memcpy(flash[p->index] + o, d, n);
    return 0;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t o, size_t n)
{
    if (o + n > p->size)
        return ESP_FAIL;
    memset(flash[p->index] + o, 255, n);
    return 0;
}
esp_err_t nvs_flash_init_partition(const char *p)
{
    (void)p;
    return 0;
}
esp_err_t nvs_open_from_partition(const char *p, const char *s, int m, nvs_handle_t *h)
{
    (void)p;
    (void)s;
    (void)m;
    *h = !strcmp(s,"runtime")?2:1;
    return 0;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *d, size_t *n)
{
    (void)h;
    (void)k;
    if(h==2&&!strcmp(k,"runtime_boot")) {
        if(!boot_exists)return ESP_ERR_NVS_NOT_FOUND;
        if(*n<sizeof(boot))return ESP_ERR_INVALID_SIZE;
        memcpy(d,boot,sizeof(boot));*n=sizeof(boot);return 0;
    }
    if (!strcmp(k, "state") && legacy_exists) {
        if (*n < sizeof(legacy_state)) return ESP_ERR_INVALID_SIZE;
        memcpy(d, &legacy_state, sizeof(legacy_state)); *n = sizeof(legacy_state); return 0;
    }
    if (!exists || strcmp(k, "catalog"))
        return ESP_ERR_NVS_NOT_FOUND;
    if (*n < sizeof(persisted)) return ESP_ERR_INVALID_SIZE;
    memcpy(d, &persisted, sizeof(persisted));
    *n = sizeof(persisted);
    return 0;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *d, size_t n)
{
    (void)h;
    (void)k;
    if(h==2&&!strcmp(k,"runtime_boot")) {
        if(n!=sizeof(boot))return ESP_FAIL;
        memcpy(boot_staged,d,n);boot_dirty=1;return 0;
    }
    catalog_dirty=1;
    if (n != sizeof(staged))
        return ESP_FAIL;
    memcpy(&staged, d, n);
    return 0;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (fail_commit > 0 && --fail_commit == 0)
        return ESP_FAIL;
    if(h==2) {
        if(boot_dirty){memcpy(boot,boot_staged,sizeof(boot));boot_exists=1;boot_dirty=0;}
    } else if(catalog_dirty){persisted=staged;exists=1;catalog_dirty=0;}
    return 0;
}
void esp_sha(int t, const unsigned char *p, size_t n, unsigned char *d)
{
    (void)t;
    SHA256(p, n, d);
}
void mock_seed(const void *p, size_t n) { memcpy(flash[0], p, n); }

void mock_legacy(int active, int previous, int pending, int trial) {
    exists = 0; legacy_exists = 1; legacy_state = (module_state_t){active, previous, pending, trial};
}
void mock_corrupt_catalog(void) { persisted.selected = APP_LIMIT; }
void mock_corrupt_payload(int app, int slot) {
    int bank = app ? 2 : slot;
    size_t offset = app ? ((app - 1) * 2 + slot) * MODULE_SLOT_BYTES : 0;
    flash[bank][offset + MODULE_HEADER_BYTES + 10] ^= 1;
}
