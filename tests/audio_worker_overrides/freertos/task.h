#pragma once
#include "FreeRTOS.h"
#include "../../freertos/task.h"
void vTaskDelay(TickType_t ticks);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);