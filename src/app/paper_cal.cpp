#include "paper_cal.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "binding_template.h"
#include "log_manager.h"
#include "relay_controller.h"
#include "sensors/tsl2591_sensor.h"
#include "shared_mem.h"

#include <Arduino.h>
#include <string.h>
#include <stdio.h>

#define TAG "PaperCal"

// ============================================================================
// Constants
// ============================================================================

static constexpr uint32_t LAMP_SETTLE_MS = 500;  // wait after relay ON before reading

// ============================================================================
// State
// ============================================================================

enum CalState : uint8_t {
    CAL_IDLE    = 0,
    CAL_READING = 1,
    CAL_DONE    = 2,
};

// Sub-states for the reading sequence (driven by paper_cal_loop)
enum ReadPhase : uint8_t {
    READ_NONE       = 0,
    READ_LAMP_ON    = 1,   // relay ON requested, waiting for settle
    READ_SENSOR     = 2,   // settle elapsed, take reading
};

static struct {
    CalState   state;
    float      lref;          // last lux reading (-1 = none)
    bool       has_reading;   // true after a successful read
    // Loop-side sequencing (main task context)
    ReadPhase  read_phase;
    uint32_t   lamp_on_ms;    // millis() when lamp turned on
} s = {
    .state       = CAL_IDLE,
    .lref        = -1.0f,
    .has_reading = false,
    .read_phase  = READ_NONE,
    .lamp_on_ms  = 0,
};

// ============================================================================
// Binding resolver
// ============================================================================

static bool cal_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:no_key");
        return false;
    }

    if (strcmp(params, "state") == 0) {
        const char* st;
        switch (s.state) {
            case CAL_READING: st = "reading"; break;
            case CAL_DONE:    st = "done";    break;
            default:          st = "idle";    break;
        }
        snprintf(out, out_len, "%s", st);
        return true;
    }

    if (strcmp(params, "lref") == 0) {
        if (!s.has_reading) {
            snprintf(out, out_len, "---");
            return false;
        }
        snprintf(out, out_len, "%.1f", s.lref);
        return true;
    }

    snprintf(out, out_len, "ERR:bad_key");
    return false;
}

static void cal_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Commands (called from LVGL task via action_dispatch)
// ============================================================================

static void cmd_start() {
    if (s.state == CAL_READING) return;  // already in progress

    s.state = CAL_READING;
    s.read_phase = READ_LAMP_ON;
    // Relay ON is handled in paper_cal_loop() — deferred to main task
    LOGI(TAG, "Start reading");
}

static void cmd_cancel() {
    if (s.state == CAL_IDLE) return;

    s.state = CAL_IDLE;
    s.lref = -1.0f;
    s.has_reading = false;
    s.read_phase = READ_NONE;
    relay_request(false);
    LOGI(TAG, "Cancelled");
}

// ============================================================================
// Public API
// ============================================================================

void paper_cal_dispatch(const char* command) {
    if (!command || !command[0]) {
        LOGW(TAG, "Empty command");
        return;
    }

    if (strcmp(command, "start") == 0)  { cmd_start(); return; }
    if (strcmp(command, "cancel") == 0) { cmd_cancel(); return; }

    LOGW(TAG, "Unknown command: '%s'", command);
}

void paper_cal_tick() {
    // No time-based expiry to check — all sequencing is in paper_cal_loop().
}

void paper_cal_loop() {
    if (s.state != CAL_READING) return;

    switch (s.read_phase) {
    case READ_LAMP_ON:
        // Turn relay on and start settle timer
        relay_request(true);
        relay_loop();  // force-flush so HTTP fires now
        s.lamp_on_ms = millis();
        s.read_phase = READ_SENSOR;
        LOGI(TAG, "Lamp ON, settling %ums", LAMP_SETTLE_MS);
        break;

    case READ_SENSOR:
        // Wait for lamp settle
        if (millis() - s.lamp_on_ms < LAMP_SETTLE_MS) return;

        {
            float lux = tsl2591_read_lux();

            // Turn lamp off immediately after reading
            relay_request(false);
            relay_loop();  // force-flush

            if (lux < 0) {
                LOGW(TAG, "Sensor read failed");
                s.state = CAL_IDLE;
                s.read_phase = READ_NONE;
                s.has_reading = false;
                return;
            }

            s.lref = lux;
            s.has_reading = true;
            s.state = CAL_DONE;
            s.read_phase = READ_NONE;

            // Write to shared memory for Phase 2 (meter) auto-population
            shared_mem_set("lref", lux);

            LOGI(TAG, "Lref = %.1f lux", lux);
        }
        break;

    default:
        break;
    }
}

void paper_cal_init() {
    if (!binding_template_register("cal", cal_resolve, cal_collect)) {
        LOGE(TAG, "Failed to register cal binding scheme");
    } else {
        LOGI(TAG, "Paper cal binding scheme registered");
    }
}

#else // !IS_DARKROOM_TIMER

void paper_cal_init() {}
void paper_cal_dispatch(const char*) {}
void paper_cal_tick() {}
void paper_cal_loop() {}

#endif // IS_DARKROOM_TIMER
