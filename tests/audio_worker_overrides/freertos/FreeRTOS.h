#pragma once
#include "../../freertos/FreeRTOS.h"
#include "../../freertos/portmacro.h"
#include <freertos/semphr.h>
using UBaseType_t = unsigned;
using TaskFunction_t = void (*)(void*);