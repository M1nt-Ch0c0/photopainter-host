#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

enum
{
    PHOTOFRAME_RESULT_OK = 0,
    PHOTOFRAME_RESULT_ARGUMENT = -1,
    PHOTOFRAME_RESULT_PNG = -2,
    PHOTOFRAME_RESULT_DIMENSIONS = -3,
    PHOTOFRAME_RESULT_COLOR = -4,
    PHOTOFRAME_RESULT_MEMORY = -5,
    PHOTOFRAME_RESULT_DISPLAY = -6,
    PHOTOFRAME_RESULT_BUSY_TIMEOUT = -7,
    PHOTOFRAME_RESULT_UNAVAILABLE = -8,
};

esp_err_t photoframe_plugin_init(void);
bool photoframe_plugin_is_ready(void);
int photoframe_plugin_render(const uint8_t *png, size_t size);

esp_err_t photoframe_plugin_activate(void);
esp_err_t photoframe_plugin_rollback(void);
uint32_t photoframe_plugin_version(void);
int photoframe_plugin_slot(void);

esp_err_t photoframe_plugin_select(const char *id, bool update);
esp_err_t photoframe_plugin_rollback_app(const char *id);
int photoframe_plugin_app(void);
