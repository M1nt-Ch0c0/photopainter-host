#pragma once
#include "wifi_profiles.h"
#include "esp_err.h"
/* Optional boot source; missing JSON may be migrated safely from NVS/wifi.txt. */
esp_err_t wifi_sd_load(const wifi_profiles_t *fallback, bool allow_migration, wifi_profiles_t *profiles);
