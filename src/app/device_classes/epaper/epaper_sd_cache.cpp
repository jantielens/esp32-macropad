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
#include <HTTPClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {

// Hardware wiring captured by epaper_sd_cache_init().
EpaperSdCacheConfig s_cfg = {nullptr, -1, -1, -1, nullptr};

bool s_enabled = false;  // runtime toggle, set from config before draw
bool s_mounted = false;

// Blob staged by the last successful download, awaiting write-back to SD after
// the frame is on screen. Owned here; freed by flush/discard.
uint8_t* s_pending_buf = nullptr;
size_t   s_pending_len = 0;
char     s_pending_id[64] = {0};

void restore_panel_bus() {
		if (s_cfg.restore_panel_bus) s_cfg.restore_panel_bus();
}

bool sd_cache_mount() {
		if (s_mounted) return true;
		if (!s_cfg.bus || s_cfg.cs_pin < 0) return false;
		// Power the card and let the rail settle before talking to it.
		if (s_cfg.en_pin >= 0) {
				pinMode(s_cfg.en_pin, OUTPUT);
				digitalWrite(s_cfg.en_pin, HIGH);
				delay(5);
		}
		if (s_cfg.det_pin >= 0) {
				pinMode(s_cfg.det_pin, INPUT_PULLUP);
				if (digitalRead(s_cfg.det_pin) == HIGH) {  // HIGH = no card inserted
						LOGW("Epaper", "SD cache: no card detected");
						if (s_cfg.en_pin >= 0) digitalWrite(s_cfg.en_pin, LOW);
						return false;
				}
		}
		if (!SD.begin(s_cfg.cs_pin, *s_cfg.bus)) {
				LOGW("Epaper", "SD cache: SD.begin failed");
				// SD.begin may have reconfigured the bus even on failure; restore it.
				restore_panel_bus();
				if (s_cfg.en_pin >= 0) digitalWrite(s_cfg.en_pin, LOW);
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
		if (s_cfg.en_pin >= 0) digitalWrite(s_cfg.en_pin, LOW);  // drop card power between accesses
}

void sd_cache_path(const char* id, char* out, size_t out_sz) {
		snprintf(out, out_sz, "/cache/%s.g16p", id);
}

// Parse the blob id out of a redirect Location like
// "https://.../<container>/images/<id>.g16p?<sas>". Returns false when the
// URL is not an images/<id>.g16p target (e.g. a non-blob redirect).
bool parse_image_id(const char* loc, char* out, size_t out_sz) {
		const char* p = strstr(loc, "images/");
		if (!p) return false;
		p += 7;  // strlen("images/")
		const char* end = strstr(p, ".g16p");
		if (!end || end <= p) return false;
		const size_t n = (size_t)(end - p);
		if (n + 1 > out_sz) return false;
		memcpy(out, p, n);
		out[n] = '\0';
		return true;
}

// Atomic write to /cache/<id>.g16p (tmp file + rename).
bool sd_cache_write(const char* id, const uint8_t* data, size_t len) {
		if (!data || len == 0) return false;
		if (!sd_cache_mount()) return false;
		bool ok = false;
		SD.mkdir("/cache");
		char path[96], tmp[96];
		sd_cache_path(id, path, sizeof(path));
		snprintf(tmp, sizeof(tmp), "/cache/%s.tmp", id);
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
}

void epaper_sd_cache_set_enabled(bool enabled) {
		s_enabled = enabled;
}

bool epaper_sd_cache_is_enabled() {
		return s_enabled;
}

// Issue the GET and resolve its 302 to the blob URL + image id WITHOUT
// downloading the 1.3 MB body. Follows up to kMaxRedirects hops. Redirected
// URLs are never logged because the Location carries a storage SAS token.
bool epaper_sd_cache_resolve(const char* url, String& out_blob_url,
                             char* out_id, size_t id_sz) {
		String current = url;
		const int kMaxRedirects = 3;
		for (int hop = 0; hop <= kMaxRedirects; ++hop) {
				const bool is_https = current.startsWith("https://");
				HTTPClient http;
				WiFiClientSecure secure;
				WiFiClient plain;
				bool begin_ok = false;
				if (is_https) {
						secure.setInsecure();
						begin_ok = http.begin(secure, current);
				} else {
						begin_ok = http.begin(plain, current);
				}
				if (!begin_ok) return false;
				http.setTimeout(8000);
				const char* collect[] = {"Location"};
				http.collectHeaders(collect, 1);
				const int code = http.GET();
				if (code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND ||
						code == HTTP_CODE_SEE_OTHER || code == HTTP_CODE_TEMPORARY_REDIRECT ||
						code == HTTP_CODE_PERMANENT_REDIRECT) {
						String loc = http.header("Location");
						http.end();
						if (loc.length() == 0) return false;
						if (parse_image_id(loc.c_str(), out_id, id_sz)) {
								out_blob_url = loc;
								return true;
						}
						if (hop == kMaxRedirects) return false;
						current = loc;
						delay(50);
						continue;
				}
				http.end();
				return false;  // 200/4xx/5xx: not a redirect we can resolve a blob id from
		}
		return false;
}

// Read /cache/<id>.g16p fully into a fresh PSRAM buffer. Returns false (and
// leaves *out_buf null) on miss or any read error.
bool epaper_sd_cache_read(const char* id, uint8_t** out_buf, size_t* out_len) {
		*out_buf = nullptr;
		*out_len = 0;
		if (!sd_cache_mount()) return false;
		char path[96];
		sd_cache_path(id, path, sizeof(path));
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

void epaper_sd_cache_stage_pending(const char* id, uint8_t* buf, size_t len) {
		// Free any blob staged by a previous draw that was never flushed.
		epaper_sd_cache_discard_pending();
		s_pending_buf = buf;
		s_pending_len = len;
		strlcpy(s_pending_id, id, sizeof(s_pending_id));
}

void epaper_sd_cache_flush() {
		if (s_pending_buf && s_pending_id[0]) {
				sd_cache_write(s_pending_id, s_pending_buf, s_pending_len);
		}
		epaper_sd_cache_discard_pending();
}

void epaper_sd_cache_discard_pending() {
		if (s_pending_buf) heap_caps_free(s_pending_buf);
		s_pending_buf = nullptr;
		s_pending_len = 0;
		s_pending_id[0] = '\0';
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
						entry = dir.openNextFile();
				}
				dir.close();
				SD.rmdir("/cache");
		}
		sd_cache_unmount();
		return ok;
}

#endif // HAS_EPAPER && defined(EPAPER_SD_CS_PIN)
