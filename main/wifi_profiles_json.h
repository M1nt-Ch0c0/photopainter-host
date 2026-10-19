#pragma once
#include "wifi_profiles.h"
#include "esp_err.h"
#include <stddef.h>
esp_err_t wifi_profiles_parse(const char *text, size_t size, wifi_profiles_t *out);
esp_err_t wifi_profiles_read_file(const char *path, wifi_profiles_t *out);
esp_err_t wifi_profiles_save_file(const char *path, const wifi_profiles_t *profiles);
/* Existing JSON/bak is authoritative; only absence permits one-time migration. */
esp_err_t wifi_profiles_migrate(const char *mount, const wifi_profiles_t *fallback,
                               wifi_profiles_t *out);
/* Caller frees the encoded JSON; contains credentials. */
esp_err_t wifi_profiles_encode(const wifi_profiles_t *profiles, char **out);
