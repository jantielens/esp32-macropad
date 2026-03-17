#include "i2c_bus.h"
#include "log_manager.h"

static SemaphoreHandle_t s_wire_mutex = NULL;

void i2c_bus_init() {
    s_wire_mutex = xSemaphoreCreateMutex();
}

bool i2c_bus_lock(TickType_t timeout) {
    if (!s_wire_mutex) return true; // not yet initialized — single-task phase
    return xSemaphoreTake(s_wire_mutex, timeout) == pdTRUE;
}

void i2c_bus_unlock() {
    if (!s_wire_mutex) return;
    xSemaphoreGive(s_wire_mutex);
}
