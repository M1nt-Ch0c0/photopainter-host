#pragma once
#include "wifi_profiles.h"
#include "esp_err.h"
/* Optional read-only boot source; NOT_FOUND alone permits NVS fallback. */
esp_err_t wifi_sd_load(wifi_profiles_t *profiles);
