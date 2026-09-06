#include "esp_partition.h"
#include "module_slots.h"
#include "nvs.h"
#include <openssl/sha.h>
#include <string.h>
static unsigned char flash[2][MODULE_SLOT_BYTES];
static module_state_t persisted, staged;
static int exists, fail_write, fail_commit;
static esp_partition_t parts[] = {{MODULE_SLOT_BYTES, 0}, {MODULE_SLOT_BYTES, 1}};
void mock_reset(void)
{
    memset(flash, 255, sizeof(flash));
    exists = 0;
    fail_write = 0;
    fail_commit = 0;
}
void mock_fail_write(int n) { fail_write = n; }
void mock_fail_commit(int n) { fail_commit = n; }
const esp_partition_t *esp_partition_find_first(int a, int b, const char *c)
{
    (void)a;
    (void)c;
    return b == 0x40 ? parts : parts + 1;
}
esp_err_t esp_partition_read(const esp_partition_t *p, size_t o, void *d, size_t n)
{
    if (o + n > MODULE_SLOT_BYTES)
        return ESP_FAIL;
    memcpy(d, flash[p->index] + o, n);
    return 0;
}
esp_err_t esp_partition_write(const esp_partition_t *p, size_t o, const void *d,
                              size_t n)
{
    if (fail_write > 0 && --fail_write == 0)
        return ESP_FAIL;
    if (o + n > MODULE_SLOT_BYTES)
        return ESP_FAIL;
    memcpy(flash[p->index] + o, d, n);
    return 0;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t o, size_t n)
{
    if (o + n > MODULE_SLOT_BYTES)
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
    *h = 1;
    return 0;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *d, size_t *n)
{
    (void)h;
    (void)k;
    if (!exists)
        return ESP_ERR_NVS_NOT_FOUND;
    memcpy(d, &persisted, sizeof(persisted));
    *n = sizeof(persisted);
    return 0;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *d, size_t n)
{
    (void)h;
    (void)k;
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
    persisted = staged;
    exists = 1;
    return 0;
}
void esp_sha(int t, const unsigned char *p, size_t n, unsigned char *d)
{
    (void)t;
    SHA256(p, n, d);
}
