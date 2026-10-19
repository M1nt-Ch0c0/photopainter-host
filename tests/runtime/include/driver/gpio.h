#pragma once
#include <stdint.h>
#define GPIO_NUM_4 4
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
int gpio_config(const gpio_config_t *);
int gpio_get_level(int);
