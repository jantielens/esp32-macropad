// E-paper SD blob cache implementation. See epaper_sd_cache.h for the contract.
//
// Aggregated into the build by device_classes/epaper_device_class.cpp (arduino-
// cli only compiles .cpp files in the sketch root). The entire body is
// #if-stripped on boards that do not define EPAPER_SD_CS_PIN, so it links out
// cleanly and the HAL vtable falls back to the header inline no-ops.

#include "board_config.h"

#if HAS_EPAPER && defined(EPAPER_SD_CS_PIN)

#include "device_classes/epaper/epaper_sd_cache.h"
#include "device_classes/epaper/epaper_driver.h"
#include "log_manager.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {

// Hardware wiring captured by epaper_sd_cache_init().
EpaperSdCacheConfig s_cfg = {nullptr, -1, -1, -1, nullptr, nullptr};

bool s_enabled = false;  // runtime toggle, set from config before draw
bool s_mounted = false;

// Blob staged by the last successful download, awaiting write-back to SD after
// the frame is on screen. Owned here; freed by flush/discard.
uint8_t* s_pending_buf = nullptr;
size_t   s_pending_len = 0;
uint32_t s_pending_crc32 = 0;
bool     s_pending_valid = false;

void restore_panel_bus() {
		if (s_cfg.restore_panel_bus) s_cfg.restore_panel_bus();
}

void prepare_sd_bus() {
		if (s_cfg.prepare_sd_bus) s_cfg.prepare_sd_bus();
}

// Mount the shared-bus microSD card. Clock for the SD half of the bus: the card
// shares the panel's HSPI lines, and the proven Seeed bring-up runs the card at
// 20 MHz, which keeps the 1.3 MB cache read under ~1 s instead of ~3 s at the
// 4 MHz default.
constexpr uint32_t SD_MOUNT_HZ = 20000000;
// The card's power rail needs time to settle after EN goes HIGH before the SPI
// handshake will complete. An earlier 5 ms ramp was too short and made
// SD.begin() report cardType=0 (handshake never completed) on every wake; the
// original working bring-up used 250 ms.
constexpr uint32_t SD_POWER_SETTLE_MS = 250;

bool sd_cache_mount() {
		if (s_mounted) return true;
		if (!s_cfg.bus || s_cfg.cs_pin < 0) return false;
		// Power the card and let the rail settle before talking to it.
		if (s_cfg.en_pin >= 0) {
				pinMode(s_cfg.en_pin, OUTPUT);
				digitalWrite(s_cfg.en_pin, HIGH);
				delay(SD_POWER_SETTLE_MS);
		}
		if (s_cfg.det_pin >= 0) {
				pinMode(s_cfg.det_pin, INPUT_PULLUP);
				if (digitalRead(s_cfg.det_pin) == HIGH) {  // HIGH = no card inserted
						LOGW("Epaper", "SD cache: no card detected");
						if (s_cfg.en_pin >= 0) digitalWrite(s_cfg.en_pin, LOW);
						return false;
				}
		}
		// Deselect the panel and re-init the shared bus exactly as the owning
		// driver requires before handing it to the SD library.
		prepare_sd_bus();
		if (!SD.begin(s_cfg.cs_pin, *s_cfg.bus, SD_MOUNT_HZ)) {
				// Distinguish a card-init failure (bus/power/wiring) from a FAT mount
				// failure (card formatted wrong). cardType()==NONE means the SPI
				// handshake never completed, so reformatting would not help.
				const uint8_t ct = SD.cardType();
				LOGW("Epaper", "SD cache: SD.begin failed (cardType=%u: %s)", ct,
						 ct == CARD_NONE ? "no card init - check bus/power"
														 : "card OK but FAT mount failed - reformat FAT32");
				// SD.begin may have reconfigured the bus even on failure; restore it.
				restore_panel_bus();
				// Leave the rail powered: power-cycling here forces a fresh ramp and
				// reintroduces the settle problem on the next mount attempt. It drops
				// naturally on deep sleep.
				return false;
		}
		s_mounted = true;
		return true;
}

void sd_cache_unmount() {
		if (!s_mounted) return;
		SD.end();
		s_mounted = false;
		// SD.end() tears the shared SPI bus down; the panel driver must re-init it
		// before the next panel access or the refresh draws garbage.
		restore_panel_bus();
		// Keep the SD rail powered while the device is awake. The E1003 card rail
		// needs the long early-boot settle window; dropping EN here makes a later
		// post-refresh write-back retry with only the short mount-time ramp and
		// SD.begin() falls back to cardType=0.
}

void sd_cache_path(uint32_t content_crc32, char* out, size_t out_sz) {
		snprintf(out, out_sz, "/cache/%08lx.blob", (unsigned long)content_crc32);
}

// Atomic write to /cache/<crc32>.blob (tmp file + rename).
bool sd_cache_write(uint32_t content_crc32, const uint8_t* data, size_t len) {
		if (!data || len == 0) return false;
		if (!sd_cache_mount()) return false;
		bool ok = false;
		SD.mkdir("/cache");
		char path[96], tmp[96];
		sd_cache_path(content_crc32, path, sizeof(path));
		snprintf(tmp, sizeof(tmp), "/cache/%08lx.tmp", (unsigned long)content_crc32);
		SD.remove(tmp);
		File f = SD.open(tmp, FILE_WRITE);
		if (f) {
				const uint32_t t0 = millis();
				const size_t wrote = f.write(data, len);
				f.close();
				if (wrote == len) {
						SD.remove(path);  // rename won't overwrite an existing file
						if (SD.rename(tmp, path)) {
								ok = true;
								LOGI("Epaper", "SD cache wrote %u bytes in %lu ms",
										 (unsigned)len, (unsigned long)(millis() - t0));
						} else {
								LOGW("Epaper", "SD cache rename failed");
								SD.remove(tmp);
						}
				} else {
						LOGW("Epaper", "SD cache write short (%u/%u)", (unsigned)wrote, (unsigned)len);
						SD.remove(tmp);
				}
		} else {
				LOGW("Epaper", "SD cache: open tmp for write failed");
		}
		sd_cache_unmount();
		return ok;
}

}  // namespace

void epaper_sd_cache_init(const EpaperSdCacheConfig& cfg) {
		s_cfg = cfg;
		// Power the SD rail HIGH up front so it has the whole panel-init + WiFi-
		// connect window (several seconds) to settle before the first mount. The
		// card's switching regulator needs far longer than the in-mount 250 ms
		// ramp to come up cleanly; without this early power-up SD.begin() reports
		// cardType=0 (SPI handshake never completed) on every wake. The rail drops
		// naturally on deep sleep. Driver init runs once at begin(), so this is a
		// one-time cost on the boot path, not per-refresh.
		if (s_cfg.en_pin >= 0) {
				pinMode(s_cfg.en_pin, OUTPUT);
				digitalWrite(s_cfg.en_pin, HIGH);
		}
}

void epaper_sd_cache_set_enabled(bool enabled) {
		s_enabled = enabled;
}

bool epaper_sd_cache_is_enabled() {
		return s_enabled;
}

// Read /cache/<crc32>.blob fully into a fresh PSRAM buffer. Returns false (and
// leaves *out_buf null) on miss or any read error.
bool epaper_sd_cache_read(uint32_t content_crc32, uint8_t** out_buf, size_t* out_len) {
		*out_buf = nullptr;
		*out_len = 0;
		if (!sd_cache_mount()) return false;
		char path[96];
		sd_cache_path(content_crc32, path, sizeof(path));
		bool ok = false;
		File f = SD.open(path, FILE_READ);
		if (f) {
				const size_t sz = f.size();
				if (sz > 0) {
						uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
						if (buf) {
								const uint32_t t0 = millis();
								const size_t got = f.read(buf, sz);
								if (got == sz) {
										*out_buf = buf;
										*out_len = sz;
										ok = true;
										LOGI("Epaper", "SD cache read %u bytes in %lu ms",
												 (unsigned)sz, (unsigned long)(millis() - t0));
								} else {
										heap_caps_free(buf);
										LOGW("Epaper", "SD cache read short (%u/%u)", (unsigned)got, (unsigned)sz);
								}
						} else {
								LOGW("Epaper", "SD cache read alloc failed (%u bytes)", (unsigned)sz);
						}
				}
				f.close();
		}
		sd_cache_unmount();
		return ok;
}

bool epaper_sd_cache_remove(uint32_t content_crc32) {
		if (!sd_cache_mount()) return false;
		char path[96];
		sd_cache_path(content_crc32, path, sizeof(path));
		const bool ok = !SD.exists(path) || SD.remove(path);
		sd_cache_unmount();
		return ok;
}

void epaper_sd_cache_stage_pending(uint32_t content_crc32, uint8_t* buf, size_t len) {
		// Free any blob staged by a previous draw that was never flushed.
		epaper_sd_cache_discard_pending();
		s_pending_buf = buf;
		s_pending_len = len;
		s_pending_crc32 = content_crc32;
		s_pending_valid = true;
}

void epaper_sd_cache_flush() {
		if (s_pending_buf && s_pending_valid) {
				sd_cache_write(s_pending_crc32, s_pending_buf, s_pending_len);
		}
		epaper_sd_cache_discard_pending();
}

void epaper_sd_cache_discard_pending() {
		if (s_pending_buf) heap_caps_free(s_pending_buf);
		s_pending_buf = nullptr;
		s_pending_len = 0;
		s_pending_crc32 = 0;
		s_pending_valid = false;
}

// Delete every file under /cache and remove the directory.
bool epaper_sd_cache_clear() {
		if (!sd_cache_mount()) return false;
		bool ok = true;
		File dir = SD.open("/cache");
		if (dir && dir.isDirectory()) {
				File entry = dir.openNextFile();
				while (entry) {
						String nm = entry.name();  // basename or full path, core-dependent
						const bool isdir = entry.isDirectory();
						entry.close();
						if (!isdir) {
								String full = nm.startsWith("/") ? nm : (String("/cache/") + nm);
								if (!SD.remove(full)) ok = false;
						}
						delay(1);
						entry = dir.openNextFile();
				}
				dir.close();
				SD.rmdir("/cache");
		}
		sd_cache_unmount();
		return ok;
}

#endif // HAS_EPAPER && defined(EPAPER_SD_CS_PIN)
