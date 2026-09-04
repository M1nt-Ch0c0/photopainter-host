#pragma once

#include "esp_err.h"

#define PHOTOFRAME_WIFI_SSID_MAX 32
#define PHOTOFRAME_WIFI_PASSWORD_MAX 64
#define PHOTOFRAME_PUSH_TOKEN_MAX 128

typedef struct {
    char wifi_ssid[PHOTOFRAME_WIFI_SSID_MAX + 1];
    char wifi_password[PHOTOFRAME_WIFI_PASSWORD_MAX + 1];
    char push_token[PHOTOFRAME_PUSH_TOKEN_MAX + 1];
} photopainter_host_config_t;

esp_err_t photopainter_host_config_load(photopainter_host_config_t *config);
