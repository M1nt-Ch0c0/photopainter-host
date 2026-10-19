#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool initialized,raw,stable,armed,eligible,gesture;
    uint32_t ignored;
    uint64_t changed,pressed;
    uint32_t epoch;
} app_button_t;
/* Pure debounce policy: pressed=true is active-low GPIO sampled by the platform. */
bool app_button_sample(app_button_t *s,bool pressed,bool busy,uint32_t epoch,uint64_t ms);
