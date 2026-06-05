// Seeed reTerminal E1003 e-paper driver.
//
// 10.3" 1404x1872 16-level grayscale panel driven by an IT8951 controller over
// HSPI. Only compiled when the board is reTerminal E1003 and HAS_EPAPER is
// enabled. Wired into the build through device_classes/epaper/epaper_drivers.cpp.
//
// Unlike the Inkplate driver (which leans on a vendor library), this driver is
// self-contained: a 4 bpp (16-level) PSRAM framebuffer wrapped in an
// Adafruit_GFX canvas for status-screen/overlay drawing, plus raw IT8951 SPI
// control sequences. The 4 bpp framebuffer is uploaded to the IT8951 in native
// 4 BPP little-endian format and refreshed with the GC16 waveform. Ported from
// the Seeed_GxEPD2 "reTerminal E1003 Gray16" reference example.
//
// draw_url() is performance-optimized for e-paper: it fetches a JPEG that is
// assumed to already be at the panel's native resolution (no scaling), decodes
// 8-bit luma, then Floyd-Steinberg dithers to 16-level grayscale.

#include "board_config.h"

#if HAS_EPAPER && defined(BOARD_RETERMINAL_E1003)

#include "device_classes/epaper/epaper_driver.h"
#include "device_classes/epaper/epaper_sd_cache.h"
#include "log_manager.h"
#include "rtos_task_utils.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <miniz.h>  // esp_rom tinfl_decompress (raw DEFLATE)
#include <string.h>

// Bundled Inter fonts (shared with the Inkplate driver; resolved via the
// project's -I src/app include path). Included only in this implementation
// .cpp so the PROGMEM bitmap arrays do not multiply across translation units.
#include "fonts/Inter_Regular_8pt7b.h"
#include "fonts/Inter_Regular_12pt7b.h"
#include "fonts/Inter_Bold_20pt7b.h"

// Per-board font overrides — see the Inkplate driver for the rationale. A
// board_overrides.h can `#define` any of these to retune for pixel density.
#ifndef EPAPER_FONT_SMALL_PTR
#define EPAPER_FONT_SMALL_PTR  (&Inter_Regular8pt7b)
#endif
#ifndef EPAPER_FONT_MEDIUM_PTR
#define EPAPER_FONT_MEDIUM_PTR (&Inter_Regular12pt7b)
#endif
#ifndef EPAPER_FONT_LARGE_PTR
#define EPAPER_FONT_LARGE_PTR  (&Inter_Bold20pt7b)
#endif

// --- Panel geometry (native landscape) --------------------------------------
#ifndef EPAPER_PANEL_W
#define EPAPER_PANEL_W 1872
#endif
#ifndef EPAPER_PANEL_H
#define EPAPER_PANEL_H 1404
#endif

// --- IT8951 HSPI pin map (E1003 defaults; overridable per board) ------------
#ifndef EPAPER_PIN_SCK
#define EPAPER_PIN_SCK 7
#endif
#ifndef EPAPER_PIN_MISO
#define EPAPER_PIN_MISO 8
#endif
#ifndef EPAPER_PIN_MOSI
#define EPAPER_PIN_MOSI 9
#endif
#ifndef EPAPER_PIN_CS
#define EPAPER_PIN_CS 10
#endif
#ifndef EPAPER_PIN_BUSY
#define EPAPER_PIN_BUSY 13  // IT8951 HRDY
#endif
#ifndef EPAPER_PIN_TFT_ENABLE
#define EPAPER_PIN_TFT_ENABLE 11
#endif
#ifndef EPAPER_PIN_ITE_ENABLE
#define EPAPER_PIN_ITE_ENABLE 21
#endif
// IT8951 hardware reset. The Seeed reference wires RES to GPIO12 on the E1003.
// A reset pulse in begin() recovers the controller from a bad state after a
// crash/brownout (otherwise BUSY/HRDY can stay stuck LOW until a full power
// cycle). Set to -1 in board_overrides.h to disable.
#ifndef EPAPER_PIN_RES
#define EPAPER_PIN_RES 12
#endif

// IT8951 panel VCOM in millivolts (magnitude). The IT8951 stores this
// internally; this is unrelated to the TPS65186 VCOM portal page, which is
// inert on this board (see the stubs at the bottom of this file).
#ifndef EPAPER_VCOM_MV
#define EPAPER_VCOM_MV 1400
#endif

// Optional battery ADC. Left unsupported by default (returns 0 mV) until the
// Refresh/ADC GPIOs are confirmed during hardware bring-up. A board_overrides.h
// can define the pin and divider to enable it.
#ifndef EPAPER_BATTERY_ADC_PIN
#define EPAPER_BATTERY_ADC_PIN -1
#endif
#ifndef EPAPER_BATTERY_ENABLE_PIN
#define EPAPER_BATTERY_ENABLE_PIN -1  // pin driven HIGH to gate the divider, if any
#endif
#ifndef EPAPER_BATTERY_DIVIDER
#define EPAPER_BATTERY_DIVIDER 2.0f  // typical 1:1 resistor divider
#endif

// IT8951 host-write SPI clock. The Seeed reference runs ~12 MHz; some IT8951
// boards tolerate higher, which shrinks the ~1.2s frame upload proportionally.
// Bumped to 14 MHz; raise further only after verifying no corruption/banding
// on the panel. A board_overrides.h can override per board.
#ifndef EPAPER_SPI_HZ
#define EPAPER_SPI_HZ 14000000
#endif

// G16P payload integrity check. The server stamps a CRC32 over the packed
// nibble payload at upload time; the firmware can re-verify it before pushing
// the frame to the panel. The image is delivered over HTTPS, whose AEAD
// already authenticates every byte end-to-end, so this CRC is largely
// redundant for transport corruption and costs a full ~1.3 MB pass (~290 ms)
// on the wake hot path. Disabled by default for faster wakes; flip to 1 to
// re-enable (e.g. when serving over plain HTTP or chasing a PSRAM bit-rot bug).
#ifndef EPAPER_VERIFY_CRC32
#define EPAPER_VERIFY_CRC32 0
#endif

namespace {

// IT8951 command set (subset used by this driver).
constexpr uint16_t CMD_SYS_RUN     = 0x0001;
constexpr uint16_t CMD_STANDBY     = 0x0002;
constexpr uint16_t CMD_SLEEP       = 0x0003;
constexpr uint16_t CMD_REG_RD      = 0x0010;
constexpr uint16_t CMD_REG_WR      = 0x0011;
constexpr uint16_t CMD_LD_IMG_AREA = 0x0021;
constexpr uint16_t CMD_LD_IMG_END  = 0x0022;
constexpr uint16_t CMD_DPY_AREA    = 0x0034;
constexpr uint16_t REG_LISAR       = 0x0208;

constexpr uint8_t IT8951_4BPP     = 2;
constexpr uint8_t IT8951_L_ENDIAN = 0;
constexpr uint8_t IT8951_ROTATE_0 = 0;

constexpr uint8_t G16P_VERSION = 1;
constexpr size_t G16P_HEADER_SIZE = 18;
constexpr size_t G16P_PAYLOAD_SIZE = (size_t)EPAPER_PANEL_W * EPAPER_PANEL_H / 2;
constexpr size_t G16P_TOTAL_SIZE = G16P_HEADER_SIZE + G16P_PAYLOAD_SIZE;
const uint8_t G16P_MAGIC[4] = {'G', '1', '6', 'P'};

// G16Z: compressed transport wrapper for a G16P blob. 4-byte magic followed by
// a raw DEFLATE stream (no zlib/gzip header) of the complete G16P bytes. The
// server emits this at upload time so the device pulls ~0.3-0.5x the bytes off
// the wire; the firmware inflates into a heap G16P buffer with the ROM's
// malloc-free tinfl. Raw G16P (uncompressed) is still accepted for backward
// compatibility with blobs uploaded before this format existed. The SD cache
// stores the original (compressed) transport bytes, so a cache hit reads the
// small blob off the shared HSPI bus and re-inflates in PSRAM -- far cheaper
// than the extra SD read time a full-size G16P would cost.
const uint8_t G16Z_MAGIC[4] = {'G', '1', '6', 'Z'};

// Translate a HAL 3-bit grayscale color (0..7, 0=black .. 7=white) into the
// canvas' 4-bit grayscale (0..15). Used by every primitive so status screens
// render with the intended shade rather than the raw masked value.
inline uint8_t hal_to_gray4(uint8_t c) {
		if (c >= EPAPER_WHITE) return 15;
		return (uint8_t)(((uint16_t)c * 15) / 7);
}

// ---------------------------------------------------------------------------
// 4 bpp (16-level) grayscale canvas, PSRAM-backed. Each nibble holds a gray
// value 0..15 (0 = black, 15 = white). Wraps Adafruit_GFX so the shared
// status-screen / overlay code can draw text and shapes.
// ---------------------------------------------------------------------------
class Gray16Canvas : public Adafruit_GFX {
public:
		Gray16Canvas(uint16_t w, uint16_t h) : Adafruit_GFX(w, h), _buf(nullptr) {}

		bool begin() {
				if (_buf) return true;
				const uint32_t sz = (uint32_t)WIDTH * HEIGHT / 2;
				_buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
				return _buf != nullptr;
		}

		void drawPixel(int16_t x, int16_t y, uint16_t color) override {
				if (!_buf) return;
				if (x < 0 || x >= width() || y < 0 || y >= height()) return;
				// Map the (possibly rotated) logical coordinate back to the native
				// landscape framebuffer.
				switch (getRotation()) {
						case 1: { int16_t t = x; x = WIDTH - 1 - y; y = t; break; }
						case 2: x = WIDTH - 1 - x; y = HEIGHT - 1 - y; break;
						case 3: { int16_t t = x; x = y; y = HEIGHT - 1 - t; break; }
				}
				write_native(x, y, (uint8_t)(color & 0x0F));
		}

		void fillScreen(uint16_t color) override {
				if (!_buf) return;
				const uint8_t g = color & 0x0F;
				memset(_buf, (g << 4) | g, (uint32_t)WIDTH * HEIGHT / 2);
		}

		// Direct native-orientation write (rotation 0). Used by the JPEG decode
		// hot path to skip the rotation switch in drawPixel.
		inline void write_native(int16_t x, int16_t y, uint8_t g4) {
				const uint32_t idx = (uint32_t)y * (WIDTH / 2) + (x >> 1);
				if (x & 1) _buf[idx] = (_buf[idx] & 0xF0) | g4;
				else       _buf[idx] = (_buf[idx] & 0x0F) | (g4 << 4);
		}

		uint8_t* buffer() { return _buf; }

private:
		uint8_t* _buf;
};

Gray16Canvas* s_canvas = nullptr;
SPIClass s_spi(HSPI);
SPISettings s_spi_set(EPAPER_SPI_HZ, MSBFIRST, SPI_MODE0);
bool s_began = false;
bool s_power_on = false;  // IT8951 power state (SYS_RUN vs sleep); mirrors Seeed's _power_is_on guard
uint32_t s_img_buf_addr = 0;

const GFXfont* const s_font_table[3] = {
		EPAPER_FONT_SMALL_PTR,   // EPAPER_FONT_SMALL
		EPAPER_FONT_MEDIUM_PTR,  // EPAPER_FONT_MEDIUM
		EPAPER_FONT_LARGE_PTR,   // EPAPER_FONT_LARGE
};

// --- IT8951 low-level SPI helpers ------------------------------------------
inline void wait_hrdy() {
		// Bounded HRDY wait. The IT8951 normally raises HRDY within a few ms; an
		// unbounded spin here can deadlock the whole device if BUSY is stuck LOW
		// -- e.g. the controller is already asleep (a second CMD_SLEEP never gets
		// acked) or an inserted MicroSD shares the SPI bus and holds the line.
		const uint32_t start = millis();
		const uint32_t timeout_ms = 3000;
		while (digitalRead(EPAPER_PIN_BUSY) == LOW) {
				if (millis() - start >= timeout_ms) {
						LOGW("Epaper", "wait_hrdy timeout (%lums); BUSY stuck LOW", (unsigned long)timeout_ms);
						return;
				}
				delay(1);
		}
}

void write_cmd16(uint16_t cmd) {
		wait_hrdy();
		s_spi.beginTransaction(s_spi_set);
		digitalWrite(EPAPER_PIN_CS, LOW);
		s_spi.transfer16(0x6000);
		wait_hrdy();
		s_spi.transfer16(cmd);
		digitalWrite(EPAPER_PIN_CS, HIGH);
		s_spi.endTransaction();
}

void write_data16(uint16_t data) {
		wait_hrdy();
		s_spi.beginTransaction(s_spi_set);
		digitalWrite(EPAPER_PIN_CS, LOW);
		s_spi.transfer16(0x0000);
		wait_hrdy();
		s_spi.transfer16(data);
		digitalWrite(EPAPER_PIN_CS, HIGH);
		s_spi.endTransaction();
}

void write_reg(uint16_t reg, uint16_t val) {
		write_cmd16(CMD_REG_WR);
		write_data16(reg);
		write_data16(val);
}

void read_dev_info() {
		write_cmd16(0x0302);
		uint16_t buf[20];
		wait_hrdy();
		s_spi.beginTransaction(s_spi_set);
		digitalWrite(EPAPER_PIN_CS, LOW);
		s_spi.transfer16(0x1000);
		wait_hrdy();
		s_spi.transfer16(0);
		for (int i = 0; i < 20; i++) buf[i] = s_spi.transfer16(0);
		digitalWrite(EPAPER_PIN_CS, HIGH);
		s_spi.endTransaction();
		s_img_buf_addr = ((uint32_t)buf[3] << 16) | buf[2];
		LOGI("Epaper", "IT8951 panel %ux%u, imgBuf=0x%08lX",
				 (unsigned)buf[0], (unsigned)buf[1], (unsigned long)s_img_buf_addr);
}

void set_vcom(uint16_t mv) {
		write_cmd16(0x0039);
		write_data16(0x0002);  // sub-command 0x0002 = SET VCOM (0x0001 = get); matches Seeed reference
		write_data16(mv);
}

void set_temperature(uint16_t t) {
		write_cmd16(0x0040);
		write_data16(0x0001);
		write_data16(t);
}

// Wake the IT8951 from sleep/standby before any draw. Guarded by s_power_on so
// a redundant call is a cheap no-op -- mirrors Seeed's _powerOnIT8951(). Without
// this, a draw after epaper_driver_sleep() would issue commands to a controller
// that never raises HRDY, deadlocking wait_hrdy().
void power_on() {
		if (s_power_on) return;
		const uint32_t t0 = millis();
		digitalWrite(EPAPER_PIN_TFT_ENABLE, HIGH);
		digitalWrite(EPAPER_PIN_ITE_ENABLE, HIGH);
		delay(10);
		write_cmd16(CMD_SYS_RUN);
		wait_hrdy();
		s_power_on = true;
		LOGI("Epaper", "IT8951 power_on %lu ms", (unsigned long)(millis() - t0));
}

// Upload the 4 bpp framebuffer to the IT8951 in its native 4 BPP little-endian
// format (IT8951_4BPP / L_ENDIAN), matching the Seeed reference's tconLoadImage.
// The canvas already holds packed 4 bpp pixels, so this pushes half the bytes of
// an 8 BPP upload. The row is X-mirrored to match the panel scan orientation.
void upload_frame() {
		write_reg(REG_LISAR,     (uint16_t)(s_img_buf_addr & 0xFFFF));
		write_reg(REG_LISAR + 2, (uint16_t)(s_img_buf_addr >> 16));
		set_temperature(16);

		uint16_t args[5];
		args[0] = (IT8951_L_ENDIAN << 8) | (IT8951_4BPP << 4) | IT8951_ROTATE_0;
		args[1] = 0;
		args[2] = 0;
		args[3] = EPAPER_PANEL_W;
		args[4] = EPAPER_PANEL_H;
		write_cmd16(CMD_LD_IMG_AREA);
		for (int i = 0; i < 5; i++) write_data16(args[i]);

		const uint32_t t0 = millis();
		s_spi.beginTransaction(s_spi_set);
		digitalWrite(EPAPER_PIN_CS, LOW);
		wait_hrdy();
		s_spi.transfer16(0x0000);
		wait_hrdy();

		const uint16_t WB = EPAPER_PANEL_W / 2;  // packed 4 bpp bytes per row
		const uint16_t NW = WB / 2;              // 16-bit words per row (4 px/word)
		const uint8_t* fb = s_canvas->buffer();
		// Match the Seeed reference exactly. tconHostAreaPackedPixelWrite treats
		// the packed row as an array of 16-bit words (4 pixels each), reverses the
		// WORD order for the horizontal mirror (filp=false), then pushes that word
		// buffer with the ESP32-S3 pushPixels() path. That path uses _swapBytes=false,
		// so it emits the raw little-endian bytes for each word. In byte terms this
		// is word-order reversal only: [0,1, 2,3, ...] -> [..., 2,3, 0,1].
		static uint8_t rowbuf[EPAPER_PANEL_W / 2];  // packed 4 bpp pixels for one row
		for (uint16_t row = 0; row < EPAPER_PANEL_H; row++) {
				const uint8_t* rp = fb + (uint32_t)row * WB;
				for (uint16_t m = 0; m < NW; m++) {
						const uint16_t k = NW - 1 - m;     // reversed source word
						rowbuf[2 * m]     = rp[2 * k];
						rowbuf[2 * m + 1] = rp[2 * k + 1];
				}
				s_spi.transfer(rowbuf, WB);
				if ((row & 0x3F) == 0) yield();
		}

		digitalWrite(EPAPER_PIN_CS, HIGH);
		s_spi.endTransaction();
		write_cmd16(CMD_LD_IMG_END);
		wait_hrdy();
		LOGI("Epaper", "Gray16 upload %lu ms", (unsigned long)(millis() - t0));
}

void refresh_gc16() {
		const uint32_t t0 = millis();
		write_cmd16(CMD_DPY_AREA);
		wait_hrdy(); write_data16(0);
		wait_hrdy(); write_data16(0);
		wait_hrdy(); write_data16(EPAPER_PANEL_W);
		wait_hrdy(); write_data16(EPAPER_PANEL_H);
		wait_hrdy(); write_data16(2);  // mode 2 = GC16
		while (digitalRead(EPAPER_PIN_BUSY) == LOW) {
				if (millis() - t0 > 15000) break;
				delay(100);
		}
		LOGI("Epaper", "Gray16 GC16 refresh %lu ms", (unsigned long)(millis() - t0));
}

// --- JPEG decode → framebuffer ---------------------------------------------
JPEGDEC s_jpeg;

// Full-frame 8-bit grayscale working buffer for Floyd-Steinberg dithering.
// When non-null, the decode callback stores raw 8-bit luma here (one byte per
// pixel widened to int16 so error diffusion can push values out of [0,255]),
// and gray16_dither_to_canvas() quantizes the whole frame afterwards. When the
// buffer cannot be allocated, the callback falls back to direct per-pixel
// rounding into the canvas (no dither).
int16_t* s_gray16 = nullptr;

static inline int clamp_u8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Quantize an 8-bit gray value to one of the 16 panel levels (0..15).
// Matches the Seeed sample: (gray + 8) / 17 rounds to the nearest of the
// 16 evenly-spaced levels [0,17,34,...,255].
static inline int nearest_gray16(int gray) {
		int q = (gray + 8) / 17;
		if (q < 0) q = 0;
		if (q > 15) q = 15;
		return q;
}

static inline uint16_t read_le16(const uint8_t* p) {
		return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_le32(const uint8_t* p) {
		return (uint32_t)p[0] |
				((uint32_t)p[1] << 8) |
				((uint32_t)p[2] << 16) |
				((uint32_t)p[3] << 24);
}

#if EPAPER_VERIFY_CRC32
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
		crc = ~crc;
		for (size_t i = 0; i < len; i++) {
				crc ^= data[i];
				for (uint8_t bit = 0; bit < 8; bit++) {
						crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1));
				}
		}
		return ~crc;
}
#endif

bool load_g16p_to_canvas(const uint8_t* data, size_t len) {
		if (!data || len < G16P_HEADER_SIZE) return false;
		if (memcmp(data, G16P_MAGIC, sizeof(G16P_MAGIC)) != 0) return false;
		if (!s_canvas || !s_canvas->buffer()) return false;

		const uint8_t version = data[4];
		const uint8_t flags = data[5];
		const uint16_t width = read_le16(data + 6);
		const uint16_t height = read_le16(data + 8);
		const uint32_t payload_len = read_le32(data + 10);
		const uint32_t expected_crc = read_le32(data + 14);

		if (version != G16P_VERSION || flags != 0 ||
				width != EPAPER_PANEL_W || height != EPAPER_PANEL_H ||
				payload_len != G16P_PAYLOAD_SIZE || len != G16P_HEADER_SIZE + G16P_PAYLOAD_SIZE) {
				LOGW("Epaper", "G16P header invalid v=%u flags=%u %ux%u payload=%lu total=%u",
						 (unsigned)version, (unsigned)flags, (unsigned)width, (unsigned)height,
						 (unsigned long)payload_len, (unsigned)len);
				return false;
		}

		const uint8_t* payload = data + G16P_HEADER_SIZE;
#if EPAPER_VERIFY_CRC32
		const uint32_t t_crc = millis();
		const uint32_t actual_crc = crc32_update(0, payload, G16P_PAYLOAD_SIZE);
		if (actual_crc != expected_crc) {
				LOGW("Epaper", "G16P CRC mismatch expected=0x%08lX actual=0x%08lX",
						 (unsigned long)expected_crc, (unsigned long)actual_crc);
				return false;
		}
		const uint32_t crc_ms = millis() - t_crc;
#else
		(void)expected_crc;
		const uint32_t crc_ms = 0;  // CRC verification disabled (see EPAPER_VERIFY_CRC32)
#endif

		const uint32_t t_cpy = millis();
		memcpy(s_canvas->buffer(), payload, G16P_PAYLOAD_SIZE);
		LOGI("Epaper", "G16P loaded (%lu bytes): crc32 %lums, memcpy %lums",
				 (unsigned long)G16P_PAYLOAD_SIZE,
				 (unsigned long)crc_ms,
				 (unsigned long)(millis() - t_cpy));
		return true;
}

// Inflate a G16Z container (4-byte magic + raw DEFLATE of a full G16P blob)
// into a freshly-allocated PSRAM buffer holding the reconstructed G16P bytes.
// The whole compressed input and the exact output size are known up front, so
// a single tinfl_decompress() call into a non-wrapping output buffer suffices.
// The tinfl_decompressor state embeds a 32 KB dictionary (~33 KB total), far
// too large for the loop task stack, so it is heap-allocated rather than using
// the stack-based tinfl_decompress_mem_to_mem() helper. On success fills
// *out_buf/*out_len (caller frees with heap_caps_free).
bool inflate_g16z(const uint8_t* data, size_t len, uint8_t** out_buf, size_t* out_len) {
		*out_buf = nullptr;
		*out_len = 0;
		if (!data || len <= sizeof(G16Z_MAGIC)) return false;

		uint8_t* dst = (uint8_t*)heap_caps_malloc(G16P_TOTAL_SIZE,
				MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!dst) {
				LOGW("Epaper", "G16Z inflate buffer alloc failed (%u bytes)",
						 (unsigned)G16P_TOTAL_SIZE);
				return false;
		}
		tinfl_decompressor* decomp = (tinfl_decompressor*)heap_caps_malloc(
				sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!decomp) {
				LOGW("Epaper", "G16Z decompressor alloc failed (%u bytes)",
						 (unsigned)sizeof(tinfl_decompressor));
				heap_caps_free(dst);
				return false;
		}

		const uint32_t t0 = millis();
		tinfl_init(decomp);
		size_t in_bytes = len - sizeof(G16Z_MAGIC);  // all input present
		size_t out_bytes = G16P_TOTAL_SIZE;          // output buffer capacity
		const tinfl_status st = tinfl_decompress(
				decomp, data + sizeof(G16Z_MAGIC), &in_bytes,
				dst, dst, &out_bytes,
				TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
		heap_caps_free(decomp);

		if (st != TINFL_STATUS_DONE || out_bytes != G16P_TOTAL_SIZE) {
				LOGW("Epaper", "G16Z inflate failed (status=%d produced=%u expected=%u)",
						 (int)st, (unsigned)out_bytes, (unsigned)G16P_TOTAL_SIZE);
				heap_caps_free(dst);
				return false;
		}
		LOGI("Epaper", "G16Z inflated %u -> %u bytes in %lu ms",
				 (unsigned)len, (unsigned)out_bytes, (unsigned long)(millis() - t0));
		*out_buf = dst;
		*out_len = out_bytes;
		return true;
}

// Decode callback: store JPEGDEC's 8-bit grayscale output in the dither buffer
// (preferred) or, when no dither buffer is available, write the rounded 4-bit
// nibble straight into the canvas. No scaling -- the image is assumed to be
// delivered at the panel's native resolution. Out-of-panel blocks are clipped.
int jpeg_draw_cb(JPEGDRAW* pDraw) {
		if (!s_canvas) return 1;
		const uint8_t* px = (const uint8_t*)pDraw->pPixels;
		const int cols = pDraw->iWidthUsed > 0 ? pDraw->iWidthUsed : pDraw->iWidth;
		for (int row = 0; row < pDraw->iHeight; row++) {
				const int y = pDraw->y + row;
				if (y < 0 || y >= EPAPER_PANEL_H) continue;
				const uint8_t* src = px + (uint32_t)row * pDraw->iWidth;
				if (s_gray16) {
						int16_t* dst = s_gray16 + (uint32_t)y * EPAPER_PANEL_W;
						for (int col = 0; col < cols; col++) {
								const int x = pDraw->x + col;
								if (x < 0 || x >= EPAPER_PANEL_W) continue;
								dst[x] = src[col];
						}
				} else {
						for (int col = 0; col < cols; col++) {
								const int x = pDraw->x + col;
								if (x < 0 || x >= EPAPER_PANEL_W) continue;
								const int gray = src[col];
								s_canvas->write_native((int16_t)x, (int16_t)y,
										(uint8_t)nearest_gray16(gray));
						}
				}
		}
		return 1;
}

// Floyd-Steinberg quantization of the 8-bit gray frame to 16 levels, written
// into the canvas. Mirrors the Seeed reTerminal_E1003_SDcard_Gray16 sample's
// gray16_diffuse(): nearest-16 quantizer with the classic FS kernel
// (7/3/5/1 over 16). Operates in place on s_gray16 (raster order, so error is
// only ever pushed to not-yet-visited pixels).
void gray16_dither_to_canvas() {
		const int W = EPAPER_PANEL_W;
		const int H = EPAPER_PANEL_H;
		struct Tap { int dx, dy, num; };
		static const Tap K[4] = {{1, 0, 7}, {-1, 1, 3}, {0, 1, 5}, {1, 1, 1}};
		for (int y = 0; y < H; y++) {
				for (int x = 0; x < W; x++) {
						const int idx = y * W + x;
						const int old = clamp_u8(s_gray16[idx]);
						const int q = nearest_gray16(old);
						s_canvas->write_native((int16_t)x, (int16_t)y, (uint8_t)q);
						const int err = old - q * 17;
						for (int k = 0; k < 4; k++) {
								const int nx = x + K[k].dx;
								const int ny = y + K[k].dy;
								if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
								const int ni = ny * W + nx;
								s_gray16[ni] = (int16_t)clamp_u8(s_gray16[ni] + err * K[k].num / 16);
						}
				}
				if ((y & 0x3F) == 0) yield();
		}
}

// Download the full body of an HTTP(S) URL into a freshly-allocated PSRAM
// buffer. Mirrors the transport pattern used by epaper_crc32.cpp. Caller frees
// *out_buf with heap_caps_free(). Returns true on a 200 response with a body.
// Follows up to kMaxRedirects 3xx hops by re-issuing the GET against the
// Location header: HTTPClient's built-in cross-host redirect following is
// unreliable on ESP32, and the image endpoint 302-redirects to the blob's own
// host (different host, fresh TLS session). Redirected URLs are never logged
// because the Location carries a storage SAS token.
bool http_download(const char* url, uint8_t** out_buf, size_t* out_len) {
		*out_buf = nullptr;
		*out_len = 0;

		const int kMaxRedirects = 3;
		String current = url;

		for (int hop = 0; hop <= kMaxRedirects; ++hop) {
				const bool is_https = current.startsWith("https://");

				HTTPClient http;
				WiFiClientSecure secure;
				WiFiClient plain;
				bool begin_ok = false;
				if (is_https) {
						secure.setInsecure();  // image integrity is the publisher's concern
						begin_ok = http.begin(secure, current);
				} else {
						begin_ok = http.begin(plain, current);
				}
				if (!begin_ok) {
						LOGW("Epaper", "image GET begin failed%s", hop ? " (redirect)" : "");
						return false;
				}
				http.setTimeout(8000);
				const char* collect[] = {"Location"};
				http.collectHeaders(collect, 1);

				const int code = http.GET();

				// 3xx: capture Location and retry against it (one TCP session per hop).
				if (code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND ||
						code == HTTP_CODE_SEE_OTHER || code == HTTP_CODE_TEMPORARY_REDIRECT ||
						code == HTTP_CODE_PERMANENT_REDIRECT) {
						String loc = http.header("Location");
						http.end();
						if (loc.length() == 0) {
								LOGW("Epaper", "image GET %d with no Location header", code);
								return false;
						}
						if (hop == kMaxRedirects) {
								LOGW("Epaper", "image GET redirect limit reached");
								return false;
						}
						current = loc;
						delay(50);  // brief settle before reconnecting to the new host
						continue;
				}

				if (code != HTTP_CODE_OK) {
						LOGW("Epaper", "image GET HTTP %d%s", code, hop ? " (redirect)" : "");
						http.end();
						return false;
				}

				const int len = http.getSize();  // -1 when chunked / unknown
				WiFiClient* stream = http.getStreamPtr();

				size_t cap = (len > 0) ? (size_t)len : (size_t)(256 * 1024);
				uint8_t* buf = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
				if (!buf) {
						LOGW("Epaper", "image buffer alloc failed (%u bytes)", (unsigned)cap);
						http.end();
						return false;
				}

				size_t total = 0;
				const uint32_t t0 = millis();
				while (http.connected() && (len < 0 || total < (size_t)len)) {
						const size_t avail = stream->available();
						if (avail) {
								if (total + avail > cap) {
										// Chunked stream outgrew the estimate — grow the buffer.
										size_t new_cap = cap * 2;
										while (total + avail > new_cap) new_cap *= 2;
										uint8_t* grown = (uint8_t*)heap_caps_realloc(buf, new_cap,
												MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
										if (!grown) {
												LOGW("Epaper", "image buffer realloc failed (%u bytes)", (unsigned)new_cap);
												heap_caps_free(buf);
												http.end();
												return false;
										}
										buf = grown;
										cap = new_cap;
								}
								const int n = stream->readBytes(buf + total, avail);
								if (n <= 0) break;
								total += (size_t)n;
						} else {
								if (len > 0 && total >= (size_t)len) break;
								if (millis() - t0 > 15000) {
										LOGW("Epaper", "image download timed out (%u bytes)", (unsigned)total);
										break;
								}
								delay(1);
						}
				}

				// Protect the WiFi MAC DMA: brief pause after closing the TCP connection
				// (project convention, see image_fetch.cpp).
				http.end();
				delay(100);

				if (total == 0) {
						heap_caps_free(buf);
						return false;
				}
				LOGI("Epaper", "image downloaded %u bytes in %lu ms",
						 (unsigned)total, (unsigned long)(millis() - t0));
				*out_buf = buf;
				*out_len = total;
				return true;
		}

		return false;  // redirect loop exhausted without a 200
}

#ifdef EPAPER_SD_CS_PIN
// Prepare the shared HSPI bus for SD.begin(), matching the original working
// E1003 SD-cache bring-up: panel CS high, then force a clean SS=-1 bus init.
void prepare_sd_bus_cb() {
		digitalWrite(EPAPER_PIN_CS, HIGH);
		pinMode(EPAPER_SD_CS_PIN, OUTPUT);
		digitalWrite(EPAPER_SD_CS_PIN, HIGH);
		s_spi.end();
		s_spi.begin(EPAPER_PIN_SCK, EPAPER_PIN_MISO, EPAPER_PIN_MOSI, -1);
}

// Restore the panel's HSPI bus + CS after an SD op. Registered with the shared
// epaper_sd_cache module (see begin()) and invoked by it after every SD.end(),
// which deinitialises the bus. Without this an SD-cache hit followed by a panel
// refresh would draw garbage (the bus is left configured for the SD card).
void restore_panel_bus_cb() {
		s_spi.begin(EPAPER_PIN_SCK, EPAPER_PIN_MISO, EPAPER_PIN_MOSI, -1);
		pinMode(EPAPER_PIN_CS, OUTPUT);
		digitalWrite(EPAPER_PIN_CS, HIGH);
}
#endif // EPAPER_SD_CS_PIN

// Scan a JPEG's markers for a progressive Start-Of-Frame (SOF2/6/10/14).
// JPEGDEC's progressive decoder is unreliable and faults on some images
// (LoadStoreError inside JPEGDecodeMCU_P), so the caller detects and skips
// progressive JPEGs rather than crash. Baseline JPEGs decode reliably.
bool jpeg_is_progressive(const uint8_t* p, size_t len) {
		if (len < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;  // not a JPEG
		size_t i = 2;
		while (i + 3 < len) {
				if (p[i] != 0xFF) { i++; continue; }
				const uint8_t m = p[i + 1];
				if (m == 0xFF) { i++; continue; }  // fill byte
				// Standalone markers (no length field).
				if (m == 0xD8 || m == 0xD9 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) {
						i += 2;
						continue;
				}
				// SOF markers are 0xC0..0xCF except DHT(C4), JPG(C8), DAC(CC).
				if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
						return (m == 0xC2 || m == 0xC6 || m == 0xCA || m == 0xCE);
				}
				if (m == 0xDA) return false;  // Start-Of-Scan: header done, baseline
				const uint16_t seglen = ((uint16_t)p[i + 2] << 8) | p[i + 3];
				if (seglen < 2) return false;
				i += 2 + seglen;
		}
		return false;
}


// JPEGDEC decode runs in a dedicated worker task with a large PSRAM-backed
// stack. The duty-cycle path runs on the 8 KB Arduino loop task, which is too
// small for the decoder's working set; overflowing it corrupts memory and
// faults with a LoadStoreError on a garbage pointer inside JPEGDEC. The worker
// only writes the framebuffer (no SPI), so it is safe to run off the main task.
struct JpegDecodeCtx {
		const uint8_t* data;
		size_t len;
		volatile bool ok;
		TaskHandle_t caller;
};

constexpr uint32_t JPEG_DECODE_STACK_WORDS = 16384;  // 64 KB PSRAM stack

void jpeg_decode_worker(void* arg) {
		JpegDecodeCtx* ctx = (JpegDecodeCtx*)arg;
		bool ok = false;
		if (s_jpeg.openRAM((uint8_t*)ctx->data, (int)ctx->len, jpeg_draw_cb)) {
				LOGI("Epaper", "JPEG header %dx%d bpp=%d orient=%d",
						 s_jpeg.getWidth(), s_jpeg.getHeight(), s_jpeg.getBpp(),
						 s_jpeg.getOrientation());
				s_jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
				ok = (s_jpeg.decode(0, 0, 0) == 1);
				if (!ok) LOGW("Epaper", "JPEG decode failed err=%d", s_jpeg.getLastError());
				s_jpeg.close();
		} else {
				LOGW("Epaper", "JPEG openRAM failed err=%d (not a JPEG?)", s_jpeg.getLastError());
		}
		ctx->ok = ok;
		xTaskNotifyGive(ctx->caller);
		// Block until the caller deletes us; deleting from the caller (not self)
		// lets it free the static stack/TCB immediately without a cleanup race.
		for (;;) vTaskDelay(portMAX_DELAY);
}

// Pre-warm worker: brings the IT8951 rails up (power_on, ~1.6s of HRDY wait)
// concurrently with the HTTP download so the panel is hot when bytes land.
// SPI (panel) and WiFi (download) use independent buses, so no contention.
struct PrewarmCtx {
		TaskHandle_t caller;
};

constexpr uint32_t PREWARM_STACK_WORDS = 2048;  // 8 KB PSRAM stack (power_on only)

void prewarm_worker(void* arg) {
		PrewarmCtx* ctx = (PrewarmCtx*)arg;
		power_on();
		xTaskNotifyGive(ctx->caller);
		// Block until the caller deletes us (same pattern as jpeg_decode_worker).
		for (;;) vTaskDelay(portMAX_DELAY);
}

// Handle bundle for a pre-warm task so it can be started concurrently with a
// non-HSPI operation (WiFi resolve or download) and reaped afterwards.
struct Prewarm {
		PrewarmCtx ctx;          // must outlive the task; reaped in prewarm_join
		TaskHandle_t worker;
		RtosTaskPsramAlloc alloc;
		bool spawned;
};

// Start power_on() on a background HSPI task. Caller MUST prewarm_join() before
// touching the HSPI bus (SD read or panel upload), since power_on drives it.
void prewarm_start(Prewarm* pw) {
		pw->ctx.caller = xTaskGetCurrentTaskHandle();
		pw->worker = nullptr;
		pw->alloc = {};
		pw->spawned = rtos_create_task_psram_stack(
				prewarm_worker, "epwarm", PREWARM_STACK_WORDS, &pw->ctx, 5, &pw->worker,
				&pw->alloc);
		if (!pw->spawned)
				LOGW("Epaper", "pre-warm spawn failed; power_on will run inline at display");
}

// Wait for power_on() to finish, then free the task TCB/stack. No-op if the
// task was never spawned, so it is always safe to call.
void prewarm_join(Prewarm* pw) {
		if (!pw->spawned) return;
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		vTaskDelete(pw->worker);
		if (pw->alloc.stack) heap_caps_free(pw->alloc.stack);
		if (pw->alloc.tcb) heap_caps_free(pw->alloc.tcb);
		pw->spawned = false;
}

}  // namespace


bool epaper_driver_begin() {
		if (s_began) return true;

		if (!s_canvas) {
				s_canvas = new Gray16Canvas(EPAPER_PANEL_W, EPAPER_PANEL_H);
				if (!s_canvas || !s_canvas->begin()) {
						LOGE("Epaper", "Gray16 framebuffer alloc failed (~1.25 MB PSRAM)");
						return false;
				}
		}

		pinMode(EPAPER_PIN_CS, OUTPUT);
		digitalWrite(EPAPER_PIN_CS, HIGH);
		pinMode(EPAPER_PIN_BUSY, INPUT);
		pinMode(EPAPER_PIN_TFT_ENABLE, OUTPUT);
		digitalWrite(EPAPER_PIN_TFT_ENABLE, HIGH);
		pinMode(EPAPER_PIN_ITE_ENABLE, OUTPUT);
		digitalWrite(EPAPER_PIN_ITE_ENABLE, HIGH);

#ifdef EPAPER_SD_CS_PIN
		// Power the shared-bus microSD rail before panel init, matching the
		// original E1003 bring-up path. The card needs the whole panel-init +
		// WiFi-connect window to settle; powering it only inside sd_cache_mount()
		// leaves SD.begin() returning cardType=0 on this board.
		pinMode(EPAPER_SD_CS_PIN, OUTPUT);
		digitalWrite(EPAPER_SD_CS_PIN, HIGH);
#ifdef EPAPER_SD_EN_PIN
		pinMode(EPAPER_SD_EN_PIN, OUTPUT);
		digitalWrite(EPAPER_SD_EN_PIN, HIGH);
#endif
#endif

		s_spi.begin(EPAPER_PIN_SCK, EPAPER_PIN_MISO, EPAPER_PIN_MOSI, -1);

#if EPAPER_PIN_RES >= 0
		// Hardware reset pulse (Seeed sequence): LOW 10 ms, HIGH 10 ms. This
		// recovers the IT8951 from a stuck/bad state after a crash or brownout
		// reboot, where BUSY/HRDY would otherwise remain LOW indefinitely.
		pinMode(EPAPER_PIN_RES, OUTPUT);
		digitalWrite(EPAPER_PIN_RES, HIGH);
		delay(10);
		digitalWrite(EPAPER_PIN_RES, LOW);
		delay(10);
		digitalWrite(EPAPER_PIN_RES, HIGH);
		delay(10);
		wait_hrdy();  // wait for the controller to come back up after reset
#endif

		delay(100);
		write_cmd16(CMD_SYS_RUN);
		delay(100);
		s_power_on = true;
		read_dev_info();
		set_vcom(EPAPER_VCOM_MV);

		s_canvas->fillScreen(15);  // white
		s_began = true;
#ifdef EPAPER_SD_CS_PIN
		// Wire the shared SD blob cache to this board's microSD slot. The card
		// reuses the IT8951 HSPI bus (s_spi); restore_panel_bus_cb re-inits that
		// bus after every SD.end() so a cache hit never corrupts the next refresh.
		const EpaperSdCacheConfig sd_cfg = {
				&s_spi,
				EPAPER_SD_CS_PIN,
#ifdef EPAPER_SD_EN_PIN
				EPAPER_SD_EN_PIN,
#else
				-1,
#endif
#ifdef EPAPER_SD_DET_PIN
				EPAPER_SD_DET_PIN,
#else
				-1,
#endif
				&prepare_sd_bus_cb,
				&restore_panel_bus_cb,
		};
		epaper_sd_cache_init(sd_cfg);
#endif
		LOGI("Epaper", "reTerminal E1003 panel initialized (16-level grayscale)");
		return true;
}

// ---------------------------------------------------------------------------
// Asynchronous panel init (wake-time WiFi overlap).
// begin() drives the HSPI panel bus and allocates the 1.25 MB canvas in PSRAM
// (~1.87 s). Running it on a background PSRAM-stack task lets the duty-cycle
// hook associate WiFi (radio bus, ~1.7 s) concurrently -- the two use
// independent buses, so they overlap without contention. Same task pattern as
// the pre-warm worker, which is proven safe for HSPI on this board.
// ---------------------------------------------------------------------------
namespace {
struct BeginCtx {
		TaskHandle_t caller;
		bool ok;
};
constexpr uint32_t BEGIN_STACK_WORDS = 4096;  // 16 KB PSRAM stack (full init)
BeginCtx s_begin_ctx = {};
TaskHandle_t s_begin_worker = nullptr;
RtosTaskPsramAlloc s_begin_alloc = {};
bool s_begin_spawned = false;
bool s_begin_result = false;

void begin_worker(void* arg) {
		BeginCtx* ctx = (BeginCtx*)arg;
		ctx->ok = epaper_driver_begin();
		xTaskNotifyGive(ctx->caller);
		// Block until the caller deletes us (same pattern as prewarm_worker), so
		// it can free the static stack/TCB without a self-delete cleanup race.
		for (;;) vTaskDelay(portMAX_DELAY);
}
}  // namespace

void epaper_driver_begin_async() {
		if (s_began) {  // already initialized; nothing to overlap
				s_begin_result = true;
				s_begin_spawned = false;
				return;
		}
		s_begin_ctx.caller = xTaskGetCurrentTaskHandle();
		s_begin_ctx.ok = false;
		s_begin_worker = nullptr;
		s_begin_alloc = {};
		s_begin_spawned = rtos_create_task_psram_stack(
				begin_worker, "epbegin", BEGIN_STACK_WORDS, &s_begin_ctx, 5,
				&s_begin_worker, &s_begin_alloc);
		if (!s_begin_spawned) {
				// Spawn failed: run begin() inline so the panel is still ready. The
				// overlap is lost but correctness is preserved.
				LOGW("Epaper", "async begin spawn failed; running begin() inline");
				s_begin_result = epaper_driver_begin();
		}
}

bool epaper_driver_begin_join() {
		if (!s_begin_spawned) return s_begin_result;
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		vTaskDelete(s_begin_worker);
		if (s_begin_alloc.stack) heap_caps_free(s_begin_alloc.stack);
		if (s_begin_alloc.tcb) heap_caps_free(s_begin_alloc.tcb);
		s_begin_spawned = false;
		s_begin_result = s_begin_ctx.ok;
		return s_begin_result;
}

// Battery sense (GPIO1 ADC + GPIO40 enable) is independent of the panel rails
// and HSPI bus, so the cell voltage can be read before begin().
bool epaper_driver_battery_ready_before_begin() { return true; }

void epaper_driver_set_rotation(uint8_t rotation) {
		if (!s_began || !s_canvas) return;
		s_canvas->setRotation(rotation & 0x3);
}

bool epaper_driver_draw_url(const char* url) {
		if (!s_began || !s_canvas || !url || !*url) return false;

		uint8_t* data = nullptr;
		size_t len = 0;
		bool from_cache = false;
#ifdef EPAPER_SD_CS_PIN
		// Clear any image staged by a previous draw that was never flushed.
		epaper_sd_cache_discard_pending();
		String blob_url;
		char img_id[64] = {0};
		if (epaper_sd_cache_is_enabled()) {
				// Hide the ~1.6s panel power-on under the network resolve: power_on
				// drives the HSPI panel bus while the resolve waits on WiFi -- two
				// independent buses, so they run concurrently. Join before any HSPI
				// access (the SD read below) so the bus is free. On a cache hit this
				// removes power_on from the critical path entirely; on a miss the panel
				// is simply hot before the body download starts (no regression, since
				// the resolve already runs before the download either way).
				Prewarm pw = {};
				prewarm_start(&pw);
				const bool resolved =
						epaper_sd_cache_resolve(url, blob_url, img_id, sizeof(img_id)) &&
						img_id[0];
				prewarm_join(&pw);  // power_on done; HSPI now free for the SD read

				// Resolve the /api/next redirect to a blob URL + content-stable id
				// WITHOUT downloading the body, then try the SD cache. A hit skips the
				// slow HTTP body GET entirely; the SD read uses the panel's HSPI bus,
				// so the pre-warm worker (which also drives HSPI) must NOT run here.
				if (resolved) {
						if (epaper_sd_cache_read(img_id, &data, &len)) {
								from_cache = true;
								LOGI("Epaper", "SD cache hit: %s", img_id);
						} else {
								LOGI("Epaper", "SD cache miss: %s", img_id);
						}
				}
		}
#endif

		if (!from_cache) {
				// Pre-warm the IT8951 (power_on ~1.6s of rail boot + HRDY wait) on a
				// background task that runs concurrently with the HTTP download, so the
				// panel is ready when bytes arrive and epaper_driver_display()'s
				// power_on() collapses to a guarded no-op. SPI and WiFi don't share a
				// bus. Skipped when the panel is already warm (the SD-cache resolve
				// above prewarmed it); prewarm_join is then a no-op.
				Prewarm pw = {};
				if (!s_power_on) prewarm_start(&pw);

				// On a cache miss we may already have resolved the blob URL; download it
				// directly to skip the redirect hop. Otherwise download the original URL.
				const char* dl_url = url;
#ifdef EPAPER_SD_CS_PIN
				if (blob_url.length() > 0) dl_url = blob_url.c_str();
#endif
				const bool dl_ok = http_download(dl_url, &data, &len);

				// Join the pre-warm worker (notifies after power_on completes), then
				// reap its TCB/stack. Done regardless of download result so the HSPI
				// bus is free and the task is always cleaned up.
				prewarm_join(&pw);

				if (!dl_ok) return false;
		}

		// `data`/`len` now holds the transport bytes -- either a compressed G16Z
		// wrapper or a raw G16P frame, from a fresh download or the SD cache. The
		// SD cache stores the *compressed* blob (whatever the server sent), so a
		// cache hit must inflate too. Inflate into a separate buffer and render
		// from that, but keep the original (possibly compressed) bytes for the SD
		// write-back so the on-disk cache stays small (~0.4 MB vs ~1.3 MB): a later
		// hit then reads far fewer bytes off the shared HSPI bus and inflates in
		// PSRAM, which is much cheaper than the extra SD read time.
		const bool is_g16z = (len >= sizeof(G16Z_MAGIC) &&
				memcmp(data, G16Z_MAGIC, sizeof(G16Z_MAGIC)) == 0);
		const bool is_g16p = (len >= sizeof(G16P_MAGIC) &&
				memcmp(data, G16P_MAGIC, sizeof(G16P_MAGIC)) == 0);
		if (is_g16z || is_g16p) {
				const uint8_t* frame = data;
				size_t frame_len = len;
				uint8_t* inflated = nullptr;
				if (is_g16z) {
						size_t inflated_len = 0;
						if (!inflate_g16z(data, len, &inflated, &inflated_len)) {
								heap_caps_free(data);
								return false;
						}
						frame = inflated;
						frame_len = inflated_len;
				}
				const bool ok = load_g16p_to_canvas(frame, frame_len);
				if (inflated) heap_caps_free(inflated);
#ifdef EPAPER_SD_CS_PIN
				// Stage the original transport bytes (compressed G16Z when the server
				// sent it) for write-back to SD after display. Transfer buffer
				// ownership to the pending slot (don't free below).
				if (ok && !from_cache && epaper_sd_cache_is_enabled() && img_id[0]) {
						epaper_sd_cache_stage_pending(img_id, data, len);
						data = nullptr;
				}
#endif
				if (data) heap_caps_free(data);
				return ok;
		}

		// JPEGDEC's progressive decoder faults on some images (the duty-cycle
		// crash). Reject progressive JPEGs up front so a bad image can never
		// crash the device or brick the panel mid-upload. The image endpoint
		// must emit baseline JPEGs.
		if (jpeg_is_progressive(data, len)) {
				LOGW("Epaper", "progressive JPEG not supported; serve baseline: %s", url);
				heap_caps_free(data);
				return false;
		}

		// Decode JPEG straight to 16-level grayscale at native resolution (no
		// scaling) on a large-stack worker task. The compressed source stays in
		// PSRAM — JPEGDEC reads it fine; the prior crash was the loop task's 8 KB
		// stack overflowing during decode, not the PSRAM source.
		//
		// Allocate a full-frame 8-bit working buffer so the decoded image can be
		// Floyd-Steinberg dithered to 16 levels (matches the Seeed sample, which
		// dithers rather than truncating). If the ~5 MB buffer cannot be had, the
		// callback falls back to direct rounded quantization (no dither).
		s_gray16 = (int16_t*)heap_caps_malloc(
				(size_t)EPAPER_PANEL_W * EPAPER_PANEL_H * sizeof(int16_t),
				MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!s_gray16) {
				LOGW("Epaper", "dither buffer alloc failed; falling back to no dither");
		}

		JpegDecodeCtx ctx = {data, len, false, xTaskGetCurrentTaskHandle()};
		TaskHandle_t worker = nullptr;
		RtosTaskPsramAlloc alloc = {};
		bool ok = false;
		if (rtos_create_task_psram_stack(jpeg_decode_worker, "jpegdec",
				JPEG_DECODE_STACK_WORDS, &ctx, 5, &worker, &alloc)) {
				ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // wait for decode to finish
				ok = ctx.ok;
				vTaskDelete(worker);  // worker is blocked, not running — safe to reap
				if (alloc.stack) heap_caps_free(alloc.stack);
				if (alloc.tcb) heap_caps_free(alloc.tcb);
		} else {
				LOGW("Epaper", "JPEG decode task spawn failed; skipping %s", url);
		}

		// Quantize + dither the decoded frame into the canvas. When no dither
		// buffer was available, the callback already wrote the canvas directly.
		if (ok && s_gray16) {
				const uint32_t td = millis();
				gray16_dither_to_canvas();
				LOGI("Epaper", "Gray16 Floyd-Steinberg dither %lu ms",
						 (unsigned long)(millis() - td));
		}
		if (s_gray16) {
				heap_caps_free(s_gray16);
				s_gray16 = nullptr;
		}

		heap_caps_free(data);
		return ok;
}

void epaper_driver_display() {
		if (!s_began || !s_canvas) return;
		power_on();  // re-wake the IT8951 if a prior epaper_driver_sleep() put it down
		upload_frame();
		refresh_gc16();
}

void epaper_driver_sleep() {
		if (!s_began || !s_power_on) return;  // guard: redundant sleep is a no-op (Seeed _power_is_on)
		write_cmd16(CMD_SLEEP);
		digitalWrite(EPAPER_PIN_TFT_ENABLE, LOW);
		digitalWrite(EPAPER_PIN_ITE_ENABLE, LOW);
		s_power_on = false;
}

uint16_t epaper_driver_battery_mv() {
#if EPAPER_BATTERY_ADC_PIN >= 0
		// Gate the divider on (E1003 routes battery sense through an enable pin)
		// and give the rail a brief settle before sampling.
#if EPAPER_BATTERY_ENABLE_PIN >= 0
		pinMode(EPAPER_BATTERY_ENABLE_PIN, OUTPUT);
		digitalWrite(EPAPER_BATTERY_ENABLE_PIN, HIGH);
		delay(5);
#else
		delay(10);
#endif
		const uint32_t raw_mv = analogReadMilliVolts(EPAPER_BATTERY_ADC_PIN);
#if EPAPER_BATTERY_ENABLE_PIN >= 0
		digitalWrite(EPAPER_BATTERY_ENABLE_PIN, LOW);  // power the divider back down
#endif
		return (uint16_t)((float)raw_mv * EPAPER_BATTERY_DIVIDER);
#else
		return 0;
#endif
}

// ---------------------------------------------------------------------------
// SD image cache HAL surface (see epaper_driver.h). Thin pass-throughs to the
// shared epaper_sd_cache module. Defined only when this board has a shared-bus
// microSD slot (EPAPER_SD_CS_PIN); otherwise the header inline no-ops apply.
// ---------------------------------------------------------------------------
#ifdef EPAPER_SD_CS_PIN
void epaper_driver_set_sd_cache_enabled(bool enabled) {
		epaper_sd_cache_set_enabled(enabled);
}

void epaper_driver_cache_flush() {
		epaper_sd_cache_flush();
}

bool epaper_driver_sd_cache_clear() {
		return epaper_sd_cache_clear();
}
#endif // EPAPER_SD_CS_PIN


// ---------------------------------------------------------------------------
// GFX primitives — pass-through to the Gray16 canvas, translating the HAL's
// 3-bit color (0..7) into the canvas' 4-bit grayscale (0..15).
// ---------------------------------------------------------------------------

void epaper_driver_clear() {
		if (!s_began || !s_canvas) return;
		s_canvas->fillScreen(15);  // white
}

void epaper_driver_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
		if (!s_began || !s_canvas) return;
		s_canvas->fillRect(x, y, w, h, hal_to_gray4(color));
}

void epaper_driver_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
		if (!s_began || !s_canvas) return;
		s_canvas->drawRect(x, y, w, h, hal_to_gray4(color));
}

void epaper_driver_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color) {
		if (!s_began || !s_canvas) return;
		s_canvas->drawRoundRect(x, y, w, h, r, hal_to_gray4(color));
}

void epaper_driver_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color) {
		if (!s_began || !s_canvas) return;
		s_canvas->fillRoundRect(x, y, w, h, r, hal_to_gray4(color));
}

void epaper_driver_set_font(uint8_t font_id) {
		if (!s_began || !s_canvas) return;
		if (font_id > EPAPER_FONT_LARGE) font_id = EPAPER_FONT_MEDIUM;
		s_canvas->setFont(s_font_table[font_id]);
}

void epaper_driver_set_text_color(uint8_t color) {
		if (!s_began || !s_canvas) return;
		s_canvas->setTextColor(hal_to_gray4(color));
}

void epaper_driver_set_cursor(int16_t x, int16_t y) {
		if (!s_began || !s_canvas) return;
		s_canvas->setCursor(x, y);
}

void epaper_driver_print(const char* text) {
		if (!s_began || !s_canvas || !text) return;
		s_canvas->print(text);
}

void epaper_driver_get_text_bounds(const char* text, int16_t x, int16_t y,
                                   int16_t* x1, int16_t* y1,
                                   uint16_t* w, uint16_t* h) {
		if (!s_began || !s_canvas || !text) {
				if (x1) *x1 = 0;
				if (y1) *y1 = 0;
				if (w) *w = 0;
				if (h) *h = 0;
				return;
		}
		s_canvas->getTextBounds(text, x, y, x1, y1, w, h);
}

int16_t epaper_driver_width() {
		if (!s_began || !s_canvas) return 0;
		return s_canvas->width();
}

int16_t epaper_driver_height() {
		if (!s_began || !s_canvas) return 0;
		return s_canvas->height();
}

// ---------------------------------------------------------------------------
// VCOM management — inert on this board. The portal VCOM page targets the
// TPS65186 PMIC semantics used by Inkplate-class panels; the reTerminal E1003
// uses an IT8951 (its VCOM is set internally in begin()), so these are stubs.
// The portal page degrades gracefully: read returns NAN ("—"), write returns
// false (500), and the test pattern is a no-op.
// ---------------------------------------------------------------------------

float epaper_driver_read_vcom() {
		return NAN;
}

bool epaper_driver_write_vcom(float /*vcom*/) {
		return false;
}

void epaper_driver_show_vcom_test_pattern(float /*preview_vcom*/) {
		// No-op: IT8951 VCOM is not user-calibratable through this portal page.
}

#endif // HAS_EPAPER && defined(BOARD_RETERMINAL_E1003)
