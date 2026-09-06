#pragma once
#include "FreeRTOS.h"
typedef struct queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,unsigned);
int xQueueSend(QueueHandle_t,const void *,uint32_t);
int xQueueReceive(QueueHandle_t,void *,uint32_t);
