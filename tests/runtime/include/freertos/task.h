#pragma once
#include "FreeRTOS.h"
#include <pthread.h>
typedef pthread_t TaskHandle_t;
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelay(uint32_t);
