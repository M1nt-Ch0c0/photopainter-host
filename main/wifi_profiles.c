#include "wifi_profiles.h"
#include <string.h>
bool wifi_profile_valid(const wifi_profile_t *p)
{
    if (!p) return false;
    size_t ssid = strnlen(p->ssid, sizeof(p->ssid));
    size_t pass = strnlen(p->password, sizeof(p->password));
    if (!ssid || ssid > 32 || pass > 64 || (pass && pass < 8)) return false;
    for (size_t i = 0; i < ssid; ++i)
        if ((unsigned char)p->ssid[i] < 32 || (unsigned char)p->ssid[i] == 127) return false;
    for (size_t i = 0; i < pass; ++i) {
        unsigned char c = p->password[i];
        if (pass == 64) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        } else if (c < 32 || c > 126) return false;
    }
    return true;
}
bool wifi_profiles_valid(const wifi_profiles_t *p)
{
    if (!p || p->version != 1 || p->count > WIFI_PROFILE_LIMIT) return false;
    for (uint32_t i = 0; i < p->count; ++i) {
        if (!wifi_profile_valid(&p->items[i])) return false;
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(p->items[i].ssid, p->items[j].ssid)) return false;
    }
    return true;
}
