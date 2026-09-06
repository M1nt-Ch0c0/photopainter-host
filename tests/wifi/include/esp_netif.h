#pragma once
#include "esp_err.h"
#define IPSTR "%d"
#define IP2STR(x) (*(x))
typedef struct { struct { int ip; } ip_info; } ip_event_got_ip_t;
esp_err_t esp_netif_init(void);
void *esp_netif_create_default_wifi_sta(void);
