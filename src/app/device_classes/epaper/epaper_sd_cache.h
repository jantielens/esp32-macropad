#pragma once
#ifndef EPAPER_SD_CACHE_H
#define EPAPER_SD_CACHE_H

#include "board_config.h"

#if HAS_EPAPER

#include <stddef.h>
#include <stdint.h>

// ----------------------------------------------------------------------------
// E-paper SD blob cache
// ----------------------------------------------------------------------------
// A downloaded-blob cache for e-paper boards that expose a microSD slot on the
// *same* SPI bus as the panel controller (e.g. the reTerminal E1003's IT8951
// HSPI bus). It is NOT a generic image cache: it stores the exact blob the
// firmware downloads for a Service refresh, keyed by the exact transport
// content CRC32 advertised by the next-image contract.
//
// On a cache hit, epaper_driver_draw_url() reads the blob straight from SD and
// skips the multi-second HTTP body download. A freshly downloaded blob is
// staged in PSRAM and written back to SD after the frame is on screen so the
// slow write lands in the awake tail rather than the wake-to-visible path.
//
// Bus ownership: the panel driver owns the SPI bus. This module never calls
// SPI.begin() itself; instead it drives the SD library's mount/unmount and,
// after every SD.end() (which tears the bus down), invokes a driver-supplied
// restore_panel_bus() callback to re-initialise the panel's SPI bus + CS.
// Without that callback an SD-hit refresh would draw garbage on the panel.
//
// Compiled in only when the board defines EPAPER_SD_CS_PIN. On boards without
// it, the SD-cache HAL vtable (epaper_driver.h) resolves to the inline no-ops
// below — the single source of truth, so per-driver stubs are not needed.
// ----------------------------------------------------------------------------

#if defined(EPAPER_SD_CS_PIN)

// Arduino types used only by reference/pointer in declarations — forward
// declared to keep this header light (the .cpp pulls in <SPI.h>/<WString.h>).
class SPIClass;
class String;

// Hardware coupling for the cache, populated by the driver's begin(). Capturing
// it in an explicit struct (rather than a positional 2-arg init) keeps the pin
// map and the bus-restore hook documented at the call site.
struct EpaperSdCacheConfig {
		SPIClass* bus;                // shared SPI bus owned by the panel driver
		int cs_pin;                   // SD chip-select
		int en_pin;                   // card power-enable, driven HIGH to mount (-1 if always powered)
		int det_pin;                  // card-detect, reads LOW when a card is inserted (-1 if none)
		void (*prepare_sd_bus)();     // deselect panel + re-init shared bus before SD.begin()
		void (*restore_panel_bus)();  // re-init the panel SPI bus + CS after every SD.end()
};

// One-time wiring of the cache to its hardware. Call from the driver's begin().
void epaper_sd_cache_init(const EpaperSdCacheConfig& cfg);

// Enable/disable SD caching for subsequent draws. Call before draw_url().
void epaper_sd_cache_set_enabled(bool enabled);

// Whether SD caching is currently enabled (runtime toggle).
bool epaper_sd_cache_is_enabled();

// Read /cache/<crc32>.blob fully into a fresh PSRAM buffer (caller frees with
// heap_caps_free). Returns false (and leaves *out_buf null) on miss/read error.
bool epaper_sd_cache_read(uint32_t content_crc32, uint8_t** out_buf, size_t* out_len);

// Remove one corrupt or stale cache entry.
bool epaper_sd_cache_remove(uint32_t content_crc32);

// Take ownership of a freshly downloaded PSRAM blob and stage it for write-back
// on the next flush. Any previously staged blob is freed first. `buf` must be a
// heap_caps_malloc allocation; the cache frees it on flush/discard.
void epaper_sd_cache_stage_pending(uint32_t content_crc32, uint8_t* buf, size_t len);

// Write the staged blob to SD (call after epaper_driver_display()). Always
// clears the staging slot, whether or not the write succeeded. No-op when
// nothing is staged.
void epaper_sd_cache_flush();

// Discard the staged blob without writing it (frees the pending buffer).
void epaper_sd_cache_discard_pending();

// Wipe the on-SD cache (portal "Clear SD cache" action). Returns true when the
// cache was cleared (or was already empty).
bool epaper_sd_cache_clear();

#else // !defined(EPAPER_SD_CS_PIN)

// Boards without a shared-bus microSD slot: the SD-cache vtable declared in
// epaper_driver.h resolves to these inline no-ops. This is the single source of
// truth for the "no SD cache on this board" behaviour — individual drivers do
// not (and must not) define their own stubs.
inline void epaper_driver_set_sd_cache_enabled(bool /*enabled*/) {}
inline void epaper_driver_cache_flush() {}
inline bool epaper_driver_sd_cache_clear() { return false; }

#endif // defined(EPAPER_SD_CS_PIN)

#endif // HAS_EPAPER

#endif // EPAPER_SD_CACHE_H
