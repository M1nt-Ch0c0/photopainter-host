#pragma once
#include <stdio.h>
/* Compile format arguments too, retaining useful -Werror checks. */
#define ESP_LOGI(tag, fmt, ...) do { if (0) printf("%s " fmt, tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
