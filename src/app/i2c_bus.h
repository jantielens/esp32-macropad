#pragma once

// Shared mutex for Wire bus 0 (I2C) thread safety.
// Multiple peripherals (GT911 touch, ES8311 audio codec) may share
// Wire bus 0 and be accessed from different FreeRTOS tasks.

#include "board_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Create the Wire bus 0 mutex. Call once during setup() before any
// multi-task I2C usage begins.
void i2c_bus_init();

// Acquire exclusive access to Wire bus 0.
// Returns true if the lock was obtained within the timeout.
bool i2c_bus_lock(TickType_t timeout = pdMS_TO_TICKS(50));

// Release Wire bus 0 access.
void i2c_bus_unlock();
