#include "tsl2591_sensor.h"
#include "../board_config.h"

#if IS_DARKROOM_TIMER

#include "../i2c_bus.h"
#include "../log_manager.h"

#include <Adafruit_TSL2591.h>
#include <Wire.h>

#define TAG "TSL2591"

static Adafruit_TSL2591 g_tsl = Adafruit_TSL2591(2591);
static bool g_initialized = false;
static bool g_available   = false;

bool tsl2591_init() {
    if (g_initialized) return g_available;
    g_initialized = true;

    // Acquire I2C bus — Wire is already started by touch/audio init.
    if (!i2c_bus_lock(pdMS_TO_TICKS(200))) {
        LOGW(TAG, "I2C bus lock timeout during init");
        return false;
    }

    g_available = g_tsl.begin(&Wire, TSL2591_ADDR);
    if (g_available) {
        g_tsl.setGain(TSL2591_GAIN_MED);
        g_tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
        LOGI(TAG, "Sensor ready at 0x%02X (medium gain, 100ms)", TSL2591_ADDR);
    } else {
        LOGW(TAG, "Sensor not found at 0x%02X", TSL2591_ADDR);
    }

    i2c_bus_unlock();
    return g_available;
}

float tsl2591_read_lux() {
    if (!g_available) return -1.0f;

    if (!i2c_bus_lock(pdMS_TO_TICKS(200))) {
        LOGW(TAG, "I2C bus lock timeout during read");
        return -1.0f;
    }

    uint32_t lum = g_tsl.getFullLuminosity();
    uint16_t ir  = lum >> 16;
    uint16_t full = lum & 0xFFFF;
    float lux = g_tsl.calculateLux(full, ir);

    i2c_bus_unlock();

    if (lux < 0) {
        LOGW(TAG, "Overflow or invalid reading");
        return -1.0f;
    }

    LOGD(TAG, "Lux=%.1f (full=%u, ir=%u)", lux, full, ir);
    return lux;
}

bool tsl2591_is_connected() {
    if (!g_initialized) return false;

    if (!i2c_bus_lock(pdMS_TO_TICKS(100))) return false;

    // Try reading the device ID register
    Wire.beginTransmission(TSL2591_ADDR);
    bool ok = (Wire.endTransmission() == 0);

    i2c_bus_unlock();
    return ok;
}

#else // !IS_DARKROOM_TIMER

bool  tsl2591_init()         { return false; }
float tsl2591_read_lux()     { return -1.0f; }
bool  tsl2591_is_connected() { return false; }

#endif // IS_DARKROOM_TIMER
