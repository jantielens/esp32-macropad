#pragma once

#include "board_config.h"

#if IS_DARKROOM_TIMER

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Print Session Log — Storage-facade persistence for darkroom prints
// ============================================================================
// Files stored at /prints/YYMMDD-NNN.json (date-based ID) on the Storage
// facade (flash or SD).
// Date and sequence tracked in NVS (namespace "print_log").
// Auto-evicts oldest print when count exceeds DARKROOM_PRINT_LOG_MAX.
// Deferred I/O: tick functions snapshot data, loop() writes to disk.
//
// Binding scheme "print":
//   [print:id]       — current print ID ("---" while printing, actual ID after save)
//   [print:last_id]  — most recently saved print ID, or "---"
//   [print:count]    — total prints stored on device

// Field-proven 500-print FIFO limit from legacy branch.
#define DARKROOM_PRINT_LOG_MAX  500
#define PRINT_LOG_DIR           "/prints"

// Maximum segments in a test strip (matches STRIP_MAX_SEGMENTS)
#define PRINT_LOG_MAX_SEGMENTS  12

// ============================================================================
// Pending data structs — filled by tick functions (LVGL task)
// ============================================================================

struct PrintLogSegment {
    float cumulative_s;
    float incremental_s;
    float offset_stops;
};

struct PrintLogExposureData {
    float set_time_s;
    float effective_time_s;
    float dry_down_pct;
    // Metering context (negative = invalid/not set)
    float lref;
    float zone5_time;
    float l_bright;
    float l_dark;
    float sbr;
    float grade;
    const char* grade_label;  // points to static string in meter module
    float mag_factor;
};

struct PrintLogStripData {
    float base_time_s;
    const char* step_label;   // points to static string in STEP_TABLE
    float step_stops;
    int   segment_count;
    PrintLogSegment segments[PRINT_LOG_MAX_SEGMENTS];
};

// Initialize print log: create /prints/ dir, open NVS, register binding scheme.
void print_log_init();

// Deferred I/O — call from main loop(). Writes pending prints to disk.
void print_log_loop();

// Snapshot exposure data for deferred save. Call from LVGL task on expiry.
void print_log_pend_exposure(const PrintLogExposureData& data);

// Snapshot test strip data for deferred save. Call from LVGL task on sequence complete.
void print_log_pend_strip(const PrintLogStripData& data);

// Clear the current print ID (call when an exposure/strip starts).
// [print:id] will show "---" until the print is saved.
void print_log_clear_id();

// Dispatch a print log action (toggle_star, set_star).
void print_log_dispatch(const char* command, const char* value);

// Get the current print count (from NVS cache).
uint16_t print_log_get_count();

#endif // IS_DARKROOM_TIMER
