#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(n,c) malloc(n)
static inline size_t heap_caps_get_free_size(int c){(void)c;return 8000000;}
static inline size_t heap_caps_get_minimum_free_size(int c){(void)c;return 7000000;}
