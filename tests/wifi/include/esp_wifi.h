#pragma once
#include "esp_err.h"
#define WIFI_INIT_CONFIG_DEFAULT() {0}
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 1
#define WIFI_AUTH_WPA2_PSK 2
#define WIFI_AUTH_OPEN 0
#define WIFI_PS_NONE 0
typedef struct { int dummy; } wifi_init_config_t;
typedef struct { struct { char ssid[32], password[64]; struct { int authmode; } threshold; struct { int capable; } pmf_cfg; } sta; } wifi_config_t;
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int, const wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_set_ps(int);
