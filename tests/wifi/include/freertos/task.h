#pragma once
#include "FreeRTOS.h"
int xTaskCreate(void (*fn)(void *), const char *, int, void *, int, void *);
void vTaskDelay(uint32_t ticks);
