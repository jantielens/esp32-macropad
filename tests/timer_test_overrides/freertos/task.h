#pragma once

#include <freertos/FreeRTOS.h>

typedef void* TaskHandle_t;
typedef unsigned int StackType_t;
typedef void (*TaskFunction_t)(void*);
typedef unsigned int UBaseType_t;
typedef int BaseType_t;
