#pragma once

#include "board_config.h"

#if HAS_SENSOR_HX711

#include "brew_manager.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Brew Log — LittleFS storage for brew reports
// ============================================================================
// Files stored at /brews/NNNN.json (zero-padded 4-digit ID).
// Next ID tracked in NVS via Preferences (uint16_t).
// Auto-evicts oldest brew when count exceeds BREW_LOG_MAX_BREWS.

#define BREW_LOG_MAX_BREWS      200
#define BREW_LOG_DIR            "/brews"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize brew log (creates /brews/ dir if needed).
void brew_log_init();

// Save a completed brew. Computes peak_flow and avg_flow from the series.
// Returns the assigned brew ID, or 0 on failure.
// Caller should call brew_free_series() after this returns.
uint16_t brew_log_save(uint32_t elapsed_ms, float final_weight,
                       const BrewSample* series, uint16_t sample_count);

// Count brews on disk.
uint16_t brew_log_count();

// Get the most recent peak_flow value (from last saved brew). Returns 0 if none.
float brew_log_last_peak_flow();

// Import a raw brew JSON blob (from export). Assigns a new ID, writes to LittleFS.
// Returns the assigned brew ID, or 0 on failure.
uint16_t brew_log_import_raw(const char* json, size_t json_len);

#ifdef __cplusplus
}
#endif

#endif // HAS_SENSOR_HX711
