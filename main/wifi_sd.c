#include "wifi_sd.h"
#include "wifi_profiles_json.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"
#include <errno.h>
#include <sys/stat.h>

static esp_err_t mount_card(sdmmc_card_t **card)
{
    /* Official PhotoPainter board pins, upstream a5e8f757ba0c, sdcard_bsp.h.
     * SDMMC is separate from the display SPI bus; no PMIC rail writes. */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = GPIO_NUM_39; slot.cmd = GPIO_NUM_41;
    slot.d0 = GPIO_NUM_40; slot.d1 = GPIO_NUM_1;
    slot.d2 = GPIO_NUM_2; slot.d3 = GPIO_NUM_38;
    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false, .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t e = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, card);
    if (e != ESP_OK) {
        /* No card-detect pin. A command timeout is the absent-card path.
         * Other media/filesystem failures are not treated as an absent list. */
        if (e == ESP_ERR_TIMEOUT) return ESP_ERR_NOT_FOUND;
        return e;
    }
    return ESP_OK;
}

esp_err_t wifi_sd_load(const wifi_profiles_t *fallback, bool allow_migration, wifi_profiles_t *profiles)
{
    sdmmc_card_t *card = NULL;
    esp_err_t e = mount_card(&card);
    if (e != ESP_OK) return e;
    e = allow_migration ? wifi_profiles_migrate("/sdcard", fallback, profiles)
                        : wifi_profiles_read_file("/sdcard/config/wifi.json", profiles);
    esp_err_t unmount = esp_vfs_fat_sdcard_unmount("/sdcard", card);
    if (unmount != ESP_OK) return unmount;
    if (e == ESP_OK) ESP_LOGI("wifi", "loaded %lu profiles from SD", (unsigned long)profiles->count);
    return e;
}

/* HTTP callers hold the shared operation lock. Boot finishes SD access before
 * starting HTTP. A corrupt primary/backup is never replaced by this API. */
esp_err_t wifi_sd_save(const wifi_profiles_t *profiles)
{
    if (!wifi_profiles_valid(profiles)) return ESP_ERR_INVALID_ARG;
    sdmmc_card_t *card = NULL;
    esp_err_t e = mount_card(&card);
    if (e != ESP_OK) return e;
    wifi_profiles_t existing;
    e = wifi_profiles_read_file("/sdcard/config/wifi.json", &existing);
    if (e == ESP_OK || e == ESP_ERR_NOT_FOUND) {
        if (mkdir("/sdcard/config", 0700) && errno != EEXIST) e = ESP_FAIL;
        else e = wifi_profiles_save_file("/sdcard/config/wifi.json", profiles);
    } else if (e == ESP_ERR_INVALID_ARG) e = ESP_ERR_INVALID_STATE;
    esp_err_t unmount = esp_vfs_fat_sdcard_unmount("/sdcard", card);
    return unmount == ESP_OK ? e : unmount;
}
