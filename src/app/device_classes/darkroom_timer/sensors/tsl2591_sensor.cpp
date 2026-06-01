#include "tsl2591_sensor.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "log_manager.h"

#include <Adafruit_TSL2591.h>
#include <Wire.h>

// When TSL2591_I2C_BUS == 1 the TSL2591 sits on a dedicated Wire1 bus
// and needs no shared-bus mutex.  When on bus 0 it shares with GT911/ES8311
// and must go through the i2c_bus lock.
//
// Bus hold time: with 3-read averaging at 300ms integration, a single
// tsl2591_read_lux() call holds the bus for ~900ms (up to ~2.4s on
// overflow fallback).  This is acceptable only on a dedicated bus (Wire1).
// On the shared bus 0 it would block touch polling for the full duration.
#if TSL2591_I2C_BUS == 1
  #define SENSOR_WIRE  Wire1
#else
  #include "i2c_bus.h"
  #define SENSOR_WIRE  Wire
#endif

#define TAG "TSL2591"

static Adafruit_TSL2591 g_tsl = Adafruit_TSL2591(2591);
static bool g_initialized = false;
static bool g_available   = false;

// Gain/timing presets ordered from highest sensitivity to lowest.
// The driver starts at the highest and falls back on overflow.
struct GainPreset {
    tsl2591Gain_t gain;
    tsl2591IntegrationTime_t timing;
    const char* label;
};

static constexpr GainPreset GAIN_PRESETS[] = {
    { TSL2591_GAIN_HIGH, TSL2591_INTEGRATIONTIME_300MS, "high/300ms" },
    { TSL2591_GAIN_MED,  TSL2591_INTEGRATIONTIME_300MS, "med/300ms"  },
    { TSL2591_GAIN_LOW,  TSL2591_INTEGRATIONTIME_100MS, "low/100ms"  },
};
static constexpr size_t GAIN_PRESET_COUNT = sizeof(GAIN_PRESETS) / sizeof(GAIN_PRESETS[0]);
static size_t g_current_preset = 0;  // index into GAIN_PRESETS

// Number of reads to average for noise reduction
static constexpr int READ_COUNT = 3;

// ── Bus helpers (no-op when on a dedicated bus) ─────────────────────
#if TSL2591_I2C_BUS == 1
static inline bool sensor_bus_lock(TickType_t)  { return true; }
static inline void sensor_bus_unlock()          {}
#else
static inline bool sensor_bus_lock(TickType_t t) { return i2c_bus_lock(t); }
static inline void sensor_bus_unlock()           { i2c_bus_unlock(); }
#endif

static void apply_gain_preset(size_t idx) {
    g_tsl.setGain(GAIN_PRESETS[idx].gain);
    g_tsl.setTiming(GAIN_PRESETS[idx].timing);
    g_current_preset = idx;
}

bool tsl2591_init() {
    if (g_initialized) return g_available;
    g_initialized = true;

#if TSL2591_I2C_BUS == 1
    // Start the dedicated sensor bus with explicit pins.
    SENSOR_WIRE.begin(TSL2591_I2C_SDA, TSL2591_I2C_SCL);
    SENSOR_WIRE.setClock(TSL2591_I2C_FREQUENCY);
    LOGI(TAG, "Wire1 started on SDA=%d SCL=%d", TSL2591_I2C_SDA, TSL2591_I2C_SCL);
#endif

    if (!sensor_bus_lock(pdMS_TO_TICKS(200))) {
        LOGW(TAG, "I2C bus lock timeout during init");
        return false;
    }

    g_available = g_tsl.begin(&SENSOR_WIRE, TSL2591_ADDR);
    if (g_available) {
        apply_gain_preset(0);
        LOGI(TAG, "Sensor ready at 0x%02X (%s, auto-fallback)",
             TSL2591_ADDR, GAIN_PRESETS[0].label);
    } else {
        LOGW(TAG, "Sensor not found at 0x%02X", TSL2591_ADDR);
    }

    sensor_bus_unlock();
    return g_available;
}

// Single raw read at current gain/timing.  Returns lux or -1 on overflow.
static float read_single_lux() {
    uint32_t lum  = g_tsl.getFullLuminosity();
    uint16_t ir   = lum >> 16;
    uint16_t full = lum & 0xFFFF;
    float lux = g_tsl.calculateLux(full, ir);
    LOGD(TAG, "raw: lux=%.3f full=%u ir=%u", lux, full, ir);
    return lux;  // negative means overflow
}

float tsl2591_read_lux() {
    if (!g_available) return -1.0f;

    if (!sensor_bus_lock(pdMS_TO_TICKS(500))) {
        LOGW(TAG, "I2C bus lock timeout during read");
        return -1.0f;
    }

    float result = -1.0f;

    // Try current preset first; on overflow, fall back to lower sensitivity
    for (size_t preset = g_current_preset; preset < GAIN_PRESET_COUNT; preset++) {
        if (preset != g_current_preset) {
            apply_gain_preset(preset);
            LOGI(TAG, "Overflow — fallback to %s", GAIN_PRESETS[preset].label);
            // Need a fresh integration cycle after changing gain
            read_single_lux();  // discard first read
        }

        // Take multiple reads and average for noise reduction
        float sum = 0.0f;
        int valid = 0;
        for (int i = 0; i < READ_COUNT; i++) {
            float lux = read_single_lux();
            if (lux >= 0.0f) {
                sum += lux;
                valid++;
            }
        }

        if (valid > 0) {
            result = sum / (float)valid;
            break;
        }
        // All reads overflowed at this preset — try next lower sensitivity
    }

    // If we fell back, try to return to highest sensitivity for next read
    if (g_current_preset != 0) {
        apply_gain_preset(0);
    }

    sensor_bus_unlock();

    if (result < 0.0f) {
        LOGW(TAG, "Overflow at all gain presets");
        return -1.0f;
    }

    LOGD(TAG, "Lux=%.3f (avg of %d reads)", result, READ_COUNT);
    return result;
}

bool tsl2591_is_connected() {
    if (!g_initialized) return false;

    if (!sensor_bus_lock(pdMS_TO_TICKS(100))) return false;

    SENSOR_WIRE.beginTransmission(TSL2591_ADDR);
    bool ok = (SENSOR_WIRE.endTransmission() == 0);

    sensor_bus_unlock();
    return ok;
}

#else // !IS_DARKROOM_TIMER

bool  tsl2591_init()         { return false; }
float tsl2591_read_lux()     { return -1.0f; }
bool  tsl2591_is_connected() { return false; }

#endif // IS_DARKROOM_TIMER
