#pragma once

#include "board_config.h"

#if HAS_IMAGE_FETCH

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Image Fetch — slot-based background image fetcher
// ============================================================================
// Single FreeRTOS task that round-robins through active slots, fetching
// images by HTTP(S) and decoding them to RGB565 via image_decoder.
// Double-buffered PSRAM frames with atomic pointer swap.
//
// Lifecycle:
//   image_fetch_init()              — create the background task (call once)
//   image_fetch_request(...)        — register a slot (returns slot_id)
//   image_fetch_cancel(slot_id)     — free slot and its buffers
//   image_fetch_pause/resume_slot() — per-slot gate (hidden/visible page)
//   image_fetch_pause/resume()      — pause/resume all slots (screen saver)
//   image_fetch_has_new_frame()     — poll from LVGL update loop
//   image_fetch_get_frame()         — returns front buffer pointer + dimensions

typedef int8_t image_slot_t;
#define IMAGE_SLOT_INVALID (-1)
#define IMAGE_SLOT_MAX     64

// ---- PSRAM memory budget per slot ----
// Each active slot holds 3 copies of the tile's RGB565 frame:
//   front_buf + back_buf  (fetch task double-buffer)  = 2 × W×H×2
//   owned_pixels          (LVGL-side stable copy)     = 1 × W×H×2
//
// Transient peak during decode (one slot at a time):
//   download buffer  ≈ size of compressed image (e.g. ~50 KB JPEG)
//   RGB888/RGBA8888 intermediate = src_W × src_H × 3 (or ×4 for PNG)
//
// Example for 4 camera tiles at 240×400 on 8 MB PSRAM:
//   Steady-state: 4 × 240×400×2×3 = 2.3 MB
//   Peak decode:  +640×480×3       ≈ +0.9 MB
//   Total peak:                    ≈ 3.2 MB  (fits comfortably)
//
// Guideline: keep total tile area under ~350 000 px per slot for 8 MB
// boards (e.g. 4 tiles of 240×360 or 2 tiles of 360×480).

// Initialize the image fetch subsystem and start the background task.
void image_fetch_init();

// Request a new image slot.  Returns a slot_id or IMAGE_SLOT_INVALID on failure.
//   url         — HTTP(S) URL to fetch (must remain valid / be copied internally)
//   user, pass  — optional HTTP Basic Auth credentials (empty string = no auth)
//   target_w/h  — desired pixel dimensions (cover-mode scale)
//   interval_ms — 0 = fetch once, >0 = periodic refresh
image_slot_t image_fetch_request(
    const char* url, const char* user, const char* pass,
    uint16_t target_w, uint16_t target_h, uint32_t interval_ms);

// Cancel a slot and free its PSRAM buffers.
void image_fetch_cancel(image_slot_t slot);

// Cancel all slots (e.g., on screen destruction).
void image_fetch_cancel_all();

// Pause/resume a single slot (e.g., when page is hidden/shown).
void image_fetch_pause_slot(image_slot_t slot);
void image_fetch_resume_slot(image_slot_t slot);

// Pause/resume all active slots (e.g., config reload).
void image_fetch_pause();
void image_fetch_resume();

// Global suspend gate — independent of per-slot pause.
// Used by the screen saver to stop all fetching while the display is off
// without disturbing per-page pause state.
void image_fetch_suspend();
void image_fetch_unsuspend();

// Check if a slot has a new frame since last acknowledgement.
bool image_fetch_has_new_frame(image_slot_t slot);

// Get the front-buffer RGB565 pixel data for a slot.
// Returns the pixel buffer pointer; sets out_w, out_h to dimensions.
// Returns nullptr if no frame is available yet.
const uint16_t* image_fetch_get_frame(image_slot_t slot, uint16_t* out_w, uint16_t* out_h);

// Acknowledge that the caller has consumed the latest frame for this slot.
// Clears the new_frame flag.
void image_fetch_ack_frame(image_slot_t slot);

#endif // HAS_IMAGE_FETCH
