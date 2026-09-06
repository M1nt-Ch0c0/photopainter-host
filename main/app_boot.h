#pragma once
#include "esp_err.h"
/* Called only after catalog trial recovery. Persistent, bounded boot attempts. */
esp_err_t app_boot_prepare(void);
esp_err_t app_boot_fallback(void);
esp_err_t app_boot_complete(void);
