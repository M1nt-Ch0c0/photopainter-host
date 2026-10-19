#pragma once

#include "esp_err.h"
#include "wifi_profiles.h"

#define PHOTOFRAME_WIFI_SSID_MAX 32
#define PHOTOFRAME_WIFI_PASSWORD_MAX 64
#define PHOTOFRAME_PUSH_TOKEN_MAX 128

typedef struct {
    wifi_profiles_t wifi;
    char push_token[PHOTOFRAME_PUSH_TOKEN_MAX + 1];
    esp_err_t token_status;
} photopainter_host_config_t;

esp_err_t photopainter_host_config_load(photopainter_host_config_t *config);
