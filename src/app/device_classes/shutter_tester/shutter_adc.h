#pragma once

#include "board_config.h"
#include "shutter_defaults.h"

#if IS_SHUTTER_TESTER

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Shutter ADC — P4-local DMA capture backend (internal, not for feature code)
// ============================================================================
// This is the concrete ESP32-P4 ADC backend consumed by shutter_capture.cpp.
// Feature-layer code (shutter_measure, shutter_binding, waveform_widget) must
// NOT include this header directly — include shutter_capture.h instead.
//
// Runs a dedicated FreeRTOS task that monitors the ADC2 continuous-mode DMA
// stream for light pulses. When a pulse is detected on any channel, captures
// the full waveform window (pre-trigger + event + post-trigger).

// Maximum samples per channel in the capture buffer.
// At ~666 kSPS, 8192 samples ≈ 12.3 ms — covers up to ~1/60s with margin.
// Slow speeds (1/30s and below) use adaptive window sizing.
#define SHUTTER_CAPTURE_MAX_SAMPLES  32768

// Pre-trigger samples to keep (ring buffer lookback before the pulse).
// Override in board_overrides.h for more baseline context on slow speeds.
#ifndef SHUTTER_PRE_TRIGGER_SAMPLES
#define SHUTTER_PRE_TRIGGER_SAMPLES  512
#endif

// Post-trigger silence samples before declaring pulse complete.
#ifndef SHUTTER_POST_TRIGGER_SAMPLES
#define SHUTTER_POST_TRIGGER_SAMPLES 256
#endif

// Post-capture extension: additional samples recorded after pulse ends.
// Provides visual baseline context on the right side of the waveform.
#ifndef SHUTTER_POST_CAPTURE_SAMPLES
#define SHUTTER_POST_CAPTURE_SAMPLES 0
#endif

// Number of consecutive samples below threshold required to trigger capture.
// Prevents single-sample ADC noise spikes from causing false triggers.
#define SHUTTER_TRIGGER_DEBOUNCE     4

// ADC resolution (12-bit on ESP32-P4 ADC2).
#define SHUTTER_ADC_RESOLUTION_BITS  12
#define SHUTTER_ADC_MAX_VALUE        ((1 << SHUTTER_ADC_RESOLUTION_BITS) - 1)

// Default trigger threshold (ADC units). Pulse starts when value drops below.
// BPW34 in reverse-bias with pull-up: dark ≈ 3350, light << 3000.
// Set close to idle to capture full pulse edges.
#define SHUTTER_DEFAULT_THRESHOLD    3200

// Trigger margin below the calibrated dark baseline (ADC units).
// After boot/recalibration, threshold is set to: min(baselines) - margin.
// Must be large enough to avoid noise-triggered captures (~25× typical RMS)
// but small enough to catch full pulse edges from real shutter actuations.
// Override in board_overrides.h if sensor characteristics differ.
#ifndef SHUTTER_TRIGGER_MARGIN
#define SHUTTER_TRIGGER_MARGIN       150
#endif

// Target sample rate per channel in Hz. ESP-IDF adc_continuous caps at ~83 kHz
// aggregate; with 3 channels that gives ~27.7 kSPS per channel. Four-channel
// presets are capped to the aggregate limit and run at ~20.8 kSPS per channel.
// At 27.7 kSPS: 1/1000s ≈ 28 samples, 1/125s ≈ 222, 1s ≈ 27 700.
#define SHUTTER_SAMPLE_RATE_HZ       27700

// Maximum aggregate ADC continuous sampling rate. Keep this aligned with the
// validated 3-channel configuration; requesting 4× SHUTTER_SAMPLE_RATE_HZ
// exceeds the ESP32-P4 driver limit and disables the shutter tester.
#ifndef SHUTTER_ADC_TOTAL_SAMPLE_RATE_HZ
#define SHUTTER_ADC_TOTAL_SAMPLE_RATE_HZ (SHUTTER_SAMPLE_RATE_HZ * 3)
#endif

// Per-channel captured waveform (backend type; not exposed to feature code).
struct ShutterWaveform {
    uint16_t* samples;           // PSRAM-allocated sample buffer
    uint32_t  count;             // Number of valid samples
    uint32_t  trigger_index;     // Index where threshold was first crossed
    float     sample_rate_hz;    // Actual sample rate (may differ from target)
};

// Captured event: waveforms for all active sensors from a single shutter actuation.
// Array is sized to SHUTTER_SENSOR_MAX; only slots 0..sensor_count-1 are populated.
struct ShutterCapture {
    ShutterWaveform sensors[SHUTTER_SENSOR_MAX];
    uint32_t        capture_id;     // Monotonic counter; incremented on each new capture
    uint32_t        timestamp_ms;   // millis() when capture completed
    uint8_t         sensor_count;   // Number of active sensors in this capture
    bool            valid;          // true if capture contains usable data
};

// Forward declaration (defined in shutter_capture.h).
struct ShutterSensorSlotMapping;

// Set the GPIO-to-logical-slot mapping before initializing the ADC.
// Ensures DMA output data arrives in logical-slot order rather than GPIO order.
// Must be called before shutter_adc_init(). Pass nullptr for default identity mapping.
void shutter_adc_set_slot_mapping(const ShutterSensorSlotMapping* mapping);

// One-time module configuration: validate sensor count, allocate PSRAM
// ring/capture buffers, create the worker task in its parked (stopped) state.
// Does NOT allocate DMA-internal RAM and does NOT start sampling — call
// shutter_adc_start() to bring the engine up. Called once from
// shutter_capture_init(); do not call directly from feature code.
void shutter_adc_init(uint8_t active_sensor_count);

// Allocate DMA-internal resources (~28 KB) and start continuous sampling.
// Idempotent: returns true immediately if already running. On failure the
// engine remains in the stopped state and no allocations leak. Safe to call
// from any task; serialized internally.
bool shutter_adc_start();

// Stop continuous sampling and free DMA-internal resources. Defers teardown
// until any in-flight capture (CAPTURE_ACTIVE / CAPTURE_COOLDOWN) finalizes
// so the front capture buffer is never torn down mid-trigger. Idempotent.
// Blocks until the worker task has parked.
void shutter_adc_stop();

// True iff DMA resources are currently allocated. Different from
// shutter_adc_is_calibrating(): "running" means resources exist; the engine
// may still be in CALIBRATING mode and not yet ready to detect triggers.
bool shutter_adc_is_running();

// Get the most recent capture (read-only snapshot, safe from LVGL task).
// Returns false if no capture is available yet.
bool shutter_adc_get_capture(ShutterCapture* out);

// Get the current trigger threshold (ADC units).
uint16_t shutter_adc_get_threshold();

// Update the trigger threshold. Called by shutter_capture_set_trigger_config().
void shutter_adc_set_threshold(uint16_t threshold);

// Returns true if the ADC driver initialized successfully.
bool shutter_adc_is_available();

// Get the per-sensor sample rate in Hz, measured by the boot self-calibration.
// Returns the configured fallback (SHUTTER_SAMPLE_RATE_HZ) until calibration
// completes (~SHUTTER_CALIB_WINDOW_MS after init). Always > 0.
float shutter_adc_get_sample_rate_hz();

// Forward declaration for alignment reading struct (defined in shutter_capture.h).
struct ShutterAlignmentReading;

// Request transition to alignment mode. Ignored if calibrating or mid-capture.
void shutter_adc_start_alignment();

// Request transition back to idle from alignment mode.
void shutter_adc_stop_alignment();

// Returns true if currently in alignment mode.
bool shutter_adc_is_alignment_active();

// Request a fresh dark-baseline calibration. Ignored while a capture is in progress.
void shutter_adc_recalibrate();

// Returns true if currently running the dark-baseline calibration window.
bool shutter_adc_is_calibrating();

// Copy the latest alignment reading. Returns false if not in alignment or no data.
bool shutter_adc_get_alignment(ShutterAlignmentReading* out);

#endif // IS_SHUTTER_TESTER
