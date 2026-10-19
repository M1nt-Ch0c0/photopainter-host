#pragma once
#include "photoframe_plugin.h"
#include "app_manifest.h"
uint32_t app_runtime_abi(void);
uint32_t app_runtime_generation(void);
bool app_runtime_ready(void);
bool app_runtime_accepts_png(void);
bool app_runtime_last_display(void);
int app_runtime_event(uint32_t event,uint32_t timer_id);
/* Shutdown without loading another instance, including after failed STOP. */
esp_err_t app_runtime_shutdown(void);
