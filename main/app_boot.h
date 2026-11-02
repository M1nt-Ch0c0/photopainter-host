#pragma once
#include "esp_err.h"
#include <stdbool.h>
/* USB maintenance is an explicit operator retry; other resets retain bounded recovery.
 * Called only after catalog trial recovery. */
esp_err_t app_boot_prepare(bool usb_maintenance_reset);
esp_err_t app_boot_fallback(void);
esp_err_t app_boot_complete(void);

/* Public, non-secret reason for the current boot guard decision. */
const char *app_boot_diagnostic(void);
