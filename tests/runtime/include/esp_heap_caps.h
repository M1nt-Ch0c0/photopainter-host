#pragma once
#include <stdlib.h>
#include <stdatomic.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define heap_caps_malloc(n,c) malloc(n)
extern atomic_size_t mock_free_psram,mock_free_internal;
static inline size_t heap_caps_get_free_size(int c){return c&MALLOC_CAP_INTERNAL?atomic_load(&mock_free_internal):atomic_load(&mock_free_psram);}
static inline size_t heap_caps_get_minimum_free_size(int c){return c&MALLOC_CAP_INTERNAL?16384:7000000;}
