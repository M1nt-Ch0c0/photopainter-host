#pragma once
#include <stddef.h>
#define SHA2_256 1
void esp_sha(int, const unsigned char *, size_t, unsigned char *);
