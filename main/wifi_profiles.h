#pragma once
#include <stdbool.h>
#include <stdint.h>
#define WIFI_PROFILE_LIMIT 10
/* Fixed layout also used by the provisioning tool's NVS blob. */
typedef struct { char ssid[33]; char password[65]; } wifi_profile_t;
typedef struct { uint32_t version, count; wifi_profile_t items[WIFI_PROFILE_LIMIT]; } wifi_profiles_t;
bool wifi_profile_valid(const wifi_profile_t *profile);
bool wifi_profiles_valid(const wifi_profiles_t *profiles);

_Static_assert(sizeof(wifi_profiles_t) == 988, "NVS Wi-Fi profile layout");
