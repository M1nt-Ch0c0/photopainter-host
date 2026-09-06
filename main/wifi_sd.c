#include "wifi_sd.h"
#include "wifi_profiles_json.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

esp_err_t wifi_sd_load(wifi_profiles_t *profiles)
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
    sdmmc_card_t *card = NULL;
    esp_err_t e = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &card);
    if (e != ESP_OK) {
        /* No card-detect pin. A command timeout is the absent-card path.
         * Other media/filesystem failures are not treated as an absent list. */
        if (e == ESP_ERR_TIMEOUT) return ESP_ERR_NOT_FOUND;
        return e;
    }
    e = wifi_profiles_read_file("/sdcard/config/wifi.json", profiles);
    esp_err_t unmount = esp_vfs_fat_sdcard_unmount("/sdcard", card);
    if (unmount != ESP_OK) return unmount;
    if (e == ESP_OK) ESP_LOGI("wifi", "loaded %lu profiles from SD", (unsigned long)profiles->count);
    return e;
}
