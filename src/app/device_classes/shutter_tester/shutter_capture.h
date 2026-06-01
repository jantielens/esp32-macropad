#pragma once

#include "board_config.h"
#include "shutter_defaults.h"

#if IS_SHUTTER_TESTER

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Shutter Capture — Runtime-capability-driven capture abstraction
// ============================================================================
// Public seam between the shutter tester feature layer (measurement, binding,
// waveform widget) and the concrete capture backend (P4 local ADC).
//
// Feature-layer code must include this header, not shutter_adc.h.
// All shared shutter-domain arrays are sized to SHUTTER_SENSOR_MAX.
// Runtime behavior is driven by the active preset's sensor_count.

// Fixed upper bound for all shared-domain arrays.
// Reserve capacity for future 9-sensor matrix presets.
// Also defined in board_config.h — the ifndef guard allows either inclusion order.
#ifndef SHUTTER_SENSOR_MAX
#define SHUTTER_SENSOR_MAX 9
#endif

// ============================================================================
// Per-sensor position (x, y) in millimetres relative to mount centre
// ============================================================================

struct ShutterSensorPosition {
    float x_mm;
    float y_mm;
};

// ============================================================================
// GPIO-to-logical-slot mapping for ADC scan-list remapping
// ============================================================================
// Each entry is the GPIO pin that provides data for that logical slot.
// The ADC layer reorders its scan list so DMA output matches slot order,
// decoupling physical wiring from the logical slot numbering used by
// measurement formulas and position arrays.

struct ShutterSensorSlotMapping {
    int gpio_pins[SHUTTER_SENSOR_MAX];  // GPIO pin for each logical slot (-1 = unused)
    uint8_t count;                       // Number of mapped slots
};

// ============================================================================
// Topology and preset identifier types
// ============================================================================

enum class ShutterTopologyType : uint8_t {
    SingleSensor,   // 1 sensor — timing and center-point verification only
    ThreeLine,      // 3 sensors — horizontal line array
    FourSensor,     // 4 sensors — corner or L-shape layout
    Matrix3x3,      // 9 sensors — 3×3 matrix
};

enum class ShutterPresetId : uint8_t {
    DirectSingle   = 0,   // 1 local ADC sensor (active)
    Direct3Line    = 1,   // 3 local ADC sensors (active)
    Offload3Line   = 2,   // reserved: requires offload backend
    Offload9Matrix = 3,   // reserved: requires offload backend
    Direct4Corner  = 4,   // 4 local ADC sensors — corner layout (active)
    Direct4LShapeH = 5,   // reserved: 4 sensors — horizontal L-shape
    Direct4LShapeV = 6,   // reserved: 4 sensors — vertical L-shape
};

// ============================================================================
// Slot and preset descriptors (read-only, lives in flash)
// ============================================================================

struct ShutterSensorSlot {
    uint8_t     logical_index;    // 0-based sensor index
    int         local_gpio;       // GPIO pin number (-1 if remote)
    int8_t      remote_channel;   // Remote channel index (-1 if local)
    uint8_t     row;              // Matrix row (0 for line topologies)
    uint8_t     col;              // Matrix column (0-2 for line)
    const char* label;            // Short label e.g. "S1", "S2"
};

struct ShutterSensorPreset {
    ShutterPresetId          id;
    const char*              preset_id_str;    // stable internal ID e.g. "direct_3_line"
    const char*              display_name;     // human-readable e.g. "Direct - 3-Line"
    ShutterTopologyType      topology;
    uint8_t                  sensor_count;
    bool                     local_capture;    // true = P4 local ADC backend
    uint32_t                 expected_sample_rate_hz_per_sensor;
    const ShutterSensorSlot* slots;
    uint8_t                  slot_count;
    bool                     active_now;       // false = reserved, no backend yet
    const ShutterSensorPosition* positions;      // per-sensor (x,y) in mm from mount centre
    uint8_t                      position_count; // == sensor_count
};

// ============================================================================
// Public contract structs
// ============================================================================

// Active capture capabilities — returned by shutter_capture_get_caps().
struct ShutterCaptureCaps {
    uint8_t             sensor_count;
    uint32_t            sample_rate_hz_per_sensor;
    ShutterTopologyType topology;
    ShutterPresetId     preset_id;
    const char*         preset_id_str;
    const char*         preset_name;      // human-readable display name
    const char*         backend_name;
    bool                waveform_available;
    bool                local_capture;
};

// Trigger configuration — passed to shutter_capture_set_trigger_config().
// All fields are per-sensor where indexed; global fields apply to all sensors.
struct ShutterTriggerConfig {
    uint16_t thresholds[SHUTTER_SENSOR_MAX];  // Per-sensor ADC trigger threshold
    uint32_t pre_trigger_samples;
    uint32_t post_trigger_samples;
    uint32_t post_capture_samples;
};

// Per-sensor waveform view — part of ShutterCaptureFrame.
struct ShutterWaveformView {
    const uint16_t* samples;              // PSRAM-backed sample buffer (read-only)
    uint32_t        count;                // Number of valid samples
    uint32_t        trigger_index;        // Index of first threshold crossing
    float           sample_rate_hz;       // Backend-reported per-sensor rate
    uint8_t         logical_sensor_index; // 0-based sensor index
};

// Capture frame — returned by shutter_capture_get_latest().
// Exactly sensor_count entries of waveforms[] are valid; slots above are zeroed.
struct ShutterCaptureFrame {
    uint32_t            capture_id;                       // Monotonic; 0 = no capture yet
    uint32_t            timestamp_ms;
    uint8_t             sensor_count;
    ShutterTopologyType topology;
    ShutterPresetId     preset_id;
    uint16_t            thresholds[SHUTTER_SENSOR_MAX];   // Thresholds active during capture
    ShutterWaveformView waveforms[SHUTTER_SENSOR_MAX];
    bool                valid;
};

// ============================================================================
// Public API
// ============================================================================

// Initialize the capture layer for the given preset ID string.
// If preset_id_str is null, empty, invalid, or reserved, falls back to
// "direct_3_line" (when board has 3 valid pins) then "direct_single".
// Call once from setup() before shutter_measure_init().
//
// This is a lightweight configure step: it validates the preset, sets up
// channel mappings, and creates the worker task in a parked state. The ADC
// engine (and its ~28 KB of DMA-internal RAM) is NOT started here — the
// first call to shutter_capture_acquire() brings it up.
void shutter_capture_init(const char* preset_id_str);

// ============================================================================
// Reference-counted lifecycle
// ============================================================================
// The shutter ADC pins a non-trivial amount of internal-DMA RAM while running
// (~28 KB across the conversion frame, ring buffer, and read buffer). On
// boards that share that pool with networking (e.g. ESP32-P4 + ESP-Hosted
// SDIO), keeping the ADC always-on fragments the pool enough to starve
// AsyncTCP / LWIP. These calls let each consumer (session, alignment,
// recalibrate, on-screen widgets) hold the engine only while it actually
// needs live samples.
//
// All calls are thread-safe and may be made from any FreeRTOS task.
//
// tag must be a string literal or stable string with a short identifier
// (e.g. "session", "align", "pad_screen"); it is used only for diagnostic
// logging of acquire/release imbalance.

// Acquire the engine. On the first acquire (refcount 0 -> 1), allocates DMA
// resources and starts sampling. Returns true on success. On allocation
// failure the refcount is NOT incremented and the caller must not release.
bool shutter_capture_acquire(const char* tag);

// Release the engine. On the last release (refcount 1 -> 0), frees DMA
// resources. Logs a warning if called more times than acquire() succeeded
// for the same tag.
void shutter_capture_release(const char* tag);

// True iff DMA resources are currently allocated (any consumer holds the
// engine). This is distinct from is_calibrating(): the engine may be
// running but still in its initial calibration window.
bool shutter_capture_is_running();

// Returns true if the capture backend initialized successfully.
bool shutter_capture_is_available();

// Fill out with the active preset's capabilities.
// Always safe to call; returns zeroed struct if not initialized.
void shutter_capture_get_caps(ShutterCaptureCaps* out);

// Get the active preset's per-sensor positions.
// Copies up to max_count positions into out. Returns the number copied (0 if not initialized).
uint8_t shutter_capture_get_positions(ShutterSensorPosition* out, uint8_t max_count);

// Update trigger thresholds and capture window sizes.
// Applied on the next arm() cycle.
void shutter_capture_set_trigger_config(const ShutterTriggerConfig* cfg);

// Arm the capture backend for the next shutter actuation.
// Idempotent for the local ADC backend (always-listening).
void shutter_capture_arm();

// Drive the capture backend state machine from the app loop.
// No-op for the local ADC backend (driven by its own FreeRTOS task).
void shutter_capture_poll();

// Get the most recent capture frame (read-only snapshot, safe from LVGL task).
// Returns false if no capture is available yet.
bool shutter_capture_get_latest(ShutterCaptureFrame* out);

// ============================================================================
// Alignment Mode
// ============================================================================

// Per-sensor alignment reading, published at ~20 Hz during alignment mode.
struct ShutterAlignmentReading {
    uint16_t    raw[SHUTTER_SENSOR_MAX];       // Decimated average ADC per sensor
    uint8_t     pct[SHUTTER_SENSOR_MAX];       // Percentage 0-100 (0=dark, 100=saturated)
    uint16_t    spread_pct;                    // max_pct - min_pct across active sensors
    const char* status;                        // 3-tier: "ready", "usable", "not-ready"
    const char* hint;                          // Short human-readable hint (empty when ready)
    uint8_t     sensor_count;                  // Number of active sensors
    uint32_t    timestamp_ms;                  // millis() when snapshot was taken
    bool        valid;                         // true if data is available
};

// Enter alignment mode (live sensor readout for positioning).
// Ignored if calibrating, mid-capture, or already in alignment.
void shutter_capture_start_alignment();

// Exit alignment mode back to idle.
// Ignored if not in alignment.
void shutter_capture_stop_alignment();

// Returns true if alignment mode is currently active.
bool shutter_capture_is_alignment_active();

// Re-run the dark-baseline calibration used by alignment and sensor health.
void shutter_capture_recalibrate();

// Returns true if the backend is currently calibrating dark baselines.
bool shutter_capture_is_calibrating();

// Copy the latest alignment reading. Returns false if not active or no data yet.
bool shutter_capture_get_alignment(ShutterAlignmentReading* out);

// Preset registry access — for unit tests and UI enumeration.
// count_out receives the number of entries in the returned array.
const ShutterSensorPreset* shutter_capture_get_preset_table(uint8_t* count_out);

#endif // IS_SHUTTER_TESTER
