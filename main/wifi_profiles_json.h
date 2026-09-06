#pragma once
#include "wifi_profiles.h"
#include "esp_err.h"
#include <stddef.h>
esp_err_t wifi_profiles_parse(const char *text, size_t size, wifi_profiles_t *out);
esp_err_t wifi_profiles_read_file(const char *path, wifi_profiles_t *out);
