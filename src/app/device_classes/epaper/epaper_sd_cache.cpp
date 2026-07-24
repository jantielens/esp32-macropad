// E-paper SD blob cache implementation. See epaper_sd_cache.h for the contract.
//
// Aggregated into the build by device_classes/epaper_device_class.cpp (arduino-
// cli only compiles .cpp files in the sketch root). The entire body is
// #if-stripped on boards that do not define EPAPER_SD_CS_PIN, so it links out
// cleanly and the HAL vtable falls back to the header inline no-ops.

#include "board_config.h"

#if HAS_EPAPER && defined(EPAPER_SD_CS_PIN)

#include "device_classes/epaper/epaper_assignment_logic.h"
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
EpaperSdCacheConfig s_cfg = {nullptr, -1, -1, -1, nullptr, nullptr};

bool s_enabled = false;  // runtime toggle, set from config before draw
bool s_mounted = false;

// Blob staged by the last successful download, awaiting write-back to SD after
// the frame is on screen. Owned here; freed by flush/discard.
uint8_t* s_pending_buf = nullptr;
size_t   s_pending_len = 0;
char     s_pending_id[64] = {0};
char     s_assignment_key[17] = {0};
uint32_t s_assignment_crc = 0;
uint8_t  s_assignment_format = 0;

struct __attribute__((packed)) AssignmentCacheMeta {
		uint8_t schema;
		uint8_t format;
		uint16_t reserved;
		uint32_t content_length;
		uint32_t content_crc32;
};
static_assert(sizeof(AssignmentCacheMeta) == 12, "assignment cache meta size");

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

// Local cache file holds the original transport blob as downloaded -- normally
// G16Z (raw-DEFLATE compressed) but a plain G16P frame when the server found it
// incompressible. Both are self-describing via their 4-byte magic and feed back
// through the same decode on read, so the file uses a neutral .g16z extension.
void sd_cache_path(const char* id, char* out, size_t out_sz) {
		snprintf(out, out_sz, "/cache/%s.g16z", id);
}

void assignment_cache_paths(char* blob, size_t blob_sz, char* meta, size_t meta_sz) {
		snprintf(blob, blob_sz, "/cache/%s.blob", s_assignment_key);
		snprintf(meta, meta_sz, "/cache/%s.meta", s_assignment_key);
}

bool read_file(const char* path, uint8_t** out_buf, size_t* out_len) {
		File file = SD.open(path, FILE_READ);
		if (!file) return false;
		const size_t size = file.size();
		uint8_t* data = size ? (uint8_t*)heap_caps_malloc(
			size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
		const size_t got = data ? file.read(data, size) : 0;
		file.close();
		if (!data || got != size) {
			if (data) heap_caps_free(data);
			return false;
		}
		*out_buf = data;
		*out_len = size;
		return true;
}

bool read_assignment_cache(uint8_t** out_buf, size_t* out_len) {
		if (!s_assignment_key[0] || s_assignment_crc == 0 || !sd_cache_mount()) return false;
		char blob_path[64], meta_path[64];
		assignment_cache_paths(blob_path, sizeof(blob_path), meta_path, sizeof(meta_path));
		AssignmentCacheMeta meta = {};
		File meta_file = SD.open(meta_path, FILE_READ);
		const bool meta_ok = meta_file && meta_file.read((uint8_t*)&meta, sizeof(meta)) == sizeof(meta);
		if (meta_file) meta_file.close();
		bool ok = meta_ok && meta.schema == 1 &&
			(s_assignment_format == 0 || meta.format == s_assignment_format) &&
			meta.content_crc32 == s_assignment_crc && read_file(blob_path, out_buf, out_len) &&
			meta.content_length == *out_len &&
			epaper_assignment_transport_crc32(*out_buf, *out_len) == s_assignment_crc;
		if (!ok) {
			if (*out_buf) heap_caps_free(*out_buf);
			*out_buf = nullptr;
			*out_len = 0;
			SD.remove(blob_path);
			SD.remove(meta_path);
		}
		sd_cache_unmount();
		return ok;
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

// Atomic write to /cache/<id>.g16z (tmp file + rename).
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

bool assignment_cache_write(const uint8_t* data, size_t len) {
		if (!s_assignment_key[0] || s_assignment_crc == 0 ||
			!data || len == 0 || !sd_cache_mount()) return false;
		SD.mkdir("/cache");
		char blob_path[64], meta_path[64], blob_tmp[68], meta_tmp[68];
		assignment_cache_paths(blob_path, sizeof(blob_path), meta_path, sizeof(meta_path));
		snprintf(blob_tmp, sizeof(blob_tmp), "%s.tmp", blob_path);
		snprintf(meta_tmp, sizeof(meta_tmp), "%s.tmp", meta_path);
		SD.remove(blob_tmp);
		SD.remove(meta_tmp);
		AssignmentCacheMeta meta = {1, s_assignment_format, 0, (uint32_t)len, s_assignment_crc};
		File blob_file = SD.open(blob_tmp, FILE_WRITE);
		const bool blob_ok = blob_file && blob_file.write(data, len) == len;
		if (blob_file) blob_file.close();
		File meta_file = blob_ok ? SD.open(meta_tmp, FILE_WRITE) : File();
		const bool meta_ok = meta_file && meta_file.write((const uint8_t*)&meta, sizeof(meta)) == sizeof(meta);
		if (meta_file) meta_file.close();
		bool ok = blob_ok && meta_ok;
		if (ok) {
			SD.remove(blob_path);
			SD.remove(meta_path);
			ok = SD.rename(blob_tmp, blob_path) && SD.rename(meta_tmp, meta_path);
		}
		if (!ok) {
			SD.remove(blob_tmp);
			SD.remove(meta_tmp);
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

void epaper_sd_cache_set_assignment_context(const char* image_key,
		uint32_t content_crc32, const char* format) {
		s_assignment_key[0] = '\0';
		s_assignment_crc = content_crc32;
		s_assignment_format = !format || !format[0] ? 0
			: (strcmp(format, "jpeg") == 0 ? 2 : 1);
		if (image_key && strlen(image_key) == 16) {
			strlcpy(s_assignment_key, image_key, sizeof(s_assignment_key));
		}
}

bool epaper_sd_cache_has_assignment_context() {
		return s_assignment_key[0] != '\0';
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
				const uint32_t t_get = millis();
				const int code = http.GET();
				LOGI("Epaper", "Resolve hop %d: GET %lums (TCP+TLS+req) code=%d", hop,
						 (unsigned long)(millis() - t_get), code);
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

// Read /cache/<id>.g16z fully into a fresh PSRAM buffer. Returns false (and
// leaves *out_buf null) on miss or any read error.
bool epaper_sd_cache_read(const char* id, uint8_t** out_buf, size_t* out_len) {
		*out_buf = nullptr;
		*out_len = 0;
		if (s_assignment_key[0] && read_assignment_cache(out_buf, out_len)) return true;
		if (!id || !*id || (s_assignment_key[0] && s_assignment_crc == 0)) return false;
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
		if (ok && s_assignment_key[0] &&
			epaper_assignment_transport_crc32(*out_buf, *out_len) != s_assignment_crc) {
			heap_caps_free(*out_buf);
			*out_buf = nullptr;
			*out_len = 0;
			ok = false;
		}
		return ok;
}

void epaper_sd_cache_stage_pending(const char* id, uint8_t* buf, size_t len) {
		// Free any blob staged by a previous draw that was never flushed.
		epaper_sd_cache_discard_pending();
		s_pending_buf = buf;
		s_pending_len = len;
		strlcpy(s_pending_id, (id && *id) ? id : "assignment", sizeof(s_pending_id));
}

void epaper_sd_cache_flush() {
		if (s_pending_buf && s_pending_id[0]) {
			if (s_assignment_key[0]) assignment_cache_write(s_pending_buf, s_pending_len);
			else sd_cache_write(s_pending_id, s_pending_buf, s_pending_len);
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
