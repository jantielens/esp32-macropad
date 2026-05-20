#include "image_fetch.h"

#if HAS_IMAGE_FETCH

#include "image_decoder.h"
#include "log_manager.h"
#include "rtos_task_utils.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <new>
#include <string.h>
#include <strings.h>

#define TAG "ImgFetch"

// Buffer sizes for slot-local copies (match pad_config.h constants)
#define SLOT_URL_MAX_LEN   256
#define SLOT_USER_MAX_LEN  32
#define SLOT_PASS_MAX_LEN  64

// ============================================================================
// Slot data structure
// ============================================================================

struct ImageSlot {
    bool active;
    char url[SLOT_URL_MAX_LEN];
    char user[SLOT_USER_MAX_LEN];
    char pass[SLOT_PASS_MAX_LEN];
    uint16_t target_w;
    uint16_t target_h;
    uint32_t interval_ms;      // 0 = fetch once
    uint32_t last_fetch_ms;    // millis() of last successful fetch
    bool fetched_once;         // true after first successful fetch
    ImageScaleMode scale_mode; // Cover or letterbox

    // Triple-buffered pixel data — fetch task rotates front/back;
    // image_fetch_get_frame() swaps front<->lvgl under g_mutex so LVGL has
    // sole, stable ownership of lvgl_buf until its next get_frame call.
    // The fetch task never touches lvgl_buf, so LVGL can render from it
    // without holding the mutex and without copying.
    uint16_t* front_buf;       // Most-recent decode (handed to LVGL on next get_frame)
    uint16_t* back_buf;        // Scratch — fetch task writes new decode here
    uint16_t* lvgl_buf;        // Owned by LVGL between get_frame calls
    size_t buf_size;           // Byte size of each buffer

    volatile bool new_frame;   // Set by fetch task, cleared by ack
    bool paused;               // Per-slot pause (hidden page)
    uint32_t frame_drops;      // Frames overwritten before LVGL ack'd the previous one
};

// ============================================================================
// Module state
// ============================================================================

static ImageSlot* g_slots = nullptr;
static SemaphoreHandle_t g_mutex = nullptr;
static TaskHandle_t g_task = nullptr;
static RtosTaskPsramAlloc g_task_alloc;
static const uint32_t FETCH_TASK_STACK_WORDS = 16384;
static const UBaseType_t FETCH_TASK_PRIORITY = 2;  // Below LVGL (4), above idle
static const uint32_t IDLE_DELAY_MS = 500;
static volatile bool g_suspended = false;  // Global gate (screen saver)
static const uint32_t HTTP_TIMEOUT_MS = 10000;
static const uint32_t MAX_DOWNLOAD_WALL_MS = 30000;  // 30s total wall-clock limit
static const size_t   MAX_DOWNLOAD_SIZE = 2 * 1024 * 1024;  // 2 MB

// ============================================================================
// Per-slot persistent HTTP connections (fetch task only — no mutex needed)
// ============================================================================

struct SlotConn {
    WiFiClient*       plain;
    WiFiClientSecure* tls;
    HTTPClient        http;
    bool              active;     // connection has been set up
    bool              is_https;
    // MJPEG streaming state (valid only when is_streaming == true)
    bool              is_streaming;   // server returned multipart/x-mixed-replace
    char              boundary[64];   // MIME boundary extracted from Content-Type
};

static SlotConn* g_conn = nullptr;

static void conn_close(int slot) {
    SlotConn& c = g_conn[slot];
    if (!c.active) return;

    // For MJPEG streams, stop the underlying WiFiClient BEFORE
    // HTTPClient::end() runs.  Without this, end() → disconnect() →
    // _client->clear() tries to drain all buffered stream data over the
    // network, which is slow and pointless for a continuous MJPEG feed.
    // Calling stop() first closes the socket and marks the handle invalid,
    // so HTTPClient::end() sees connected()==false and skips the drain.
    if (c.is_streaming) {
        WiFiClient* client = c.is_https ? (WiFiClient*)c.tls : c.plain;
        if (client) client->stop();
    }

    c.http.end();
    // Let WiFi MAC finish processing TCP close frames before destroying
    // the client.  Without this delay the MAC DMA can access freed memory
    // from the client's internal lwIP structures.
    vTaskDelay(pdMS_TO_TICKS(100));
    delete c.tls;   c.tls = nullptr;
    delete c.plain;  c.plain = nullptr;
    c.active = false;
    c.is_https = false;
    c.is_streaming = false;
    c.boundary[0] = '\0';
}

// Ensure a persistent connection exists for a slot.  Creates the client
// on first call; subsequent calls reuse it (HTTP keep-alive).
static bool conn_ensure(int slot, const char* url, const char* user, const char* pass) {
    SlotConn& c = g_conn[slot];
    bool need_https = (strncmp(url, "https://", 8) == 0);

    // Protocol changed — tear down and recreate
    if (c.active && c.is_https != need_https) {
        conn_close(slot);
    }

    // Create client on first use
    if (!c.active) {
        if (need_https) {
            c.tls = new (std::nothrow) WiFiClientSecure();
            if (!c.tls) { LOGE(TAG, "OOM for WiFiClientSecure"); return false; }
            c.tls->setInsecure();
            c.tls->setTimeout(HTTP_TIMEOUT_MS);
        } else {
            c.plain = new (std::nothrow) WiFiClient();
            if (!c.plain) { LOGE(TAG, "OOM for WiFiClient"); return false; }
            c.plain->setTimeout(HTTP_TIMEOUT_MS);
        }
        c.http.setReuse(true);  // HTTP keep-alive — reuse TCP connection
        c.http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        c.is_https = need_https;
        c.active = true;
    }

    bool ok = need_https ? c.http.begin(*c.tls, url) : c.http.begin(*c.plain, url);
    if (!ok) {
        LOGW(TAG, "HTTP begin failed: %.60s", url);
        conn_close(slot);
        return false;
    }

    // setTimeout must be called AFTER begin() — begin() sets _client, and
    // setTimeout() internally calls connected() which dereferences _client.
    // Calling it before begin() would use a dangling pointer from a
    // previous conn_close() cycle.
    c.http.setTimeout(HTTP_TIMEOUT_MS);

    // Request Content-Type collection so mjpeg_read_frame can detect
    // multipart/x-mixed-replace without an extra round-trip.
    const char* headers[] = { "Content-Type", "content-type" };
    c.http.collectHeaders(headers, 2);

    if (user && user[0] && pass && pass[0]) {
        c.http.setAuthorization(user, pass);
    }
    return true;
}

// Download from an already-prepared connection.  Returns PSRAM buffer.
static bool conn_download(int slot, uint8_t** out_data, size_t* out_len) {
    *out_data = nullptr;
    *out_len = 0;

    SlotConn& c = g_conn[slot];
    int code = c.http.GET();
    if (code != 200) {
        LOGW(TAG, "HTTP %d for slot %d", code, slot);
        c.http.end();        // clears response; keep-alive preserves socket
        if (code < 0) conn_close(slot);  // transport error — full teardown
        return false;
    }

    int content_len = c.http.getSize();
    if (content_len == 0) {
        LOGW(TAG, "Empty response for slot %d", slot);
        c.http.end();
        return false;
    }
    if (content_len > 0 && (size_t)content_len > MAX_DOWNLOAD_SIZE) {
        LOGW(TAG, "Response too large: %d bytes for slot %d", content_len, slot);
        c.http.end();
        return false;
    }

    // Read response body into PSRAM buffer
    size_t total = (content_len > 0) ? (size_t)content_len : 0;
    size_t capacity = total ? total : 32768;

    uint8_t* buf = (uint8_t*)heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)malloc(capacity);
    if (!buf) {
        LOGE(TAG, "OOM for download buffer (%u bytes)", (unsigned)capacity);
        c.http.end();
        return false;
    }

    WiFiClient* stream = c.http.getStreamPtr();
    size_t received = 0;
    uint32_t dl_start = (uint32_t)millis();

    while (c.http.connected() && (content_len < 0 || received < (size_t)content_len)) {
        if ((uint32_t)millis() - dl_start > MAX_DOWNLOAD_WALL_MS) {
            LOGW(TAG, "Download wall-clock timeout (%us) slot %d",
                 (unsigned)(MAX_DOWNLOAD_WALL_MS / 1000), slot);
            heap_caps_free(buf);
            c.http.end();
            conn_close(slot);
            return false;
        }

        size_t avail = stream->available();
        if (avail == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Grow buffer if needed (for chunked responses)
        if (received + avail > capacity) {
            size_t new_cap = capacity * 2;
            if (new_cap > MAX_DOWNLOAD_SIZE) new_cap = MAX_DOWNLOAD_SIZE;
            if (received + avail > new_cap) {
                LOGW(TAG, "Download exceeds max size");
                heap_caps_free(buf);
                c.http.end();
                return false;
            }
            uint8_t* new_buf = (uint8_t*)heap_caps_realloc(buf, new_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!new_buf) {
                LOGE(TAG, "OOM growing download buffer to %u", (unsigned)new_cap);
                heap_caps_free(buf);
                c.http.end();
                return false;
            }
            buf = new_buf;
            capacity = new_cap;
        }

        size_t chunk = (avail < 4096) ? avail : 4096;
        int n = stream->readBytes(buf + received, chunk);
        if (n <= 0) break;
        received += n;
    }

    c.http.end();  // clears response; keep-alive preserves socket

    if (received == 0) {
        heap_caps_free(buf);
        return false;
    }

    *out_data = buf;
    *out_len = received;
    return true;
}

// ============================================================================
// MJPEG streaming: open the connection once, read frames as they arrive.
// ============================================================================
//
// Protocol: server sends a continuous HTTP response with
//   Content-Type: multipart/x-mixed-replace; boundary=<token>
// Each frame is preceded by a part header:
//   --<boundary>\r\n
//   Content-Type: image/jpeg\r\n
//   Content-Length: <N>\r\n
//   \r\n
//   <N bytes of JPEG data>
//
// This function:
//   1. On first call (c.is_streaming == false): sends the GET request,
//      reads the HTTP response headers, extracts the boundary string, and
//      leaves the socket open.
//   2. On subsequent calls: reads the next frame from the already-open socket.
//
// Returns a PSRAM-allocated buffer (caller must heap_caps_free) or nullptr.
// On any error the connection is fully closed so the next call reconnects.

// Read one line from stream (up to max_len-1 chars), stripping \r\n.
// Returns number of chars read, or -1 on timeout/disconnect.
static int read_line(WiFiClient* stream, char* buf, size_t max_len, uint32_t timeout_ms) {
    size_t pos = 0;
    uint32_t start = (uint32_t)millis();
    while (pos < max_len - 1) {
        if ((uint32_t)millis() - start > timeout_ms) return -1;
        if (!stream->available()) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        char c = (char)stream->read();
        if (c == '\n') break;
        if (c != '\r') buf[pos++] = c;
    }
    buf[pos] = '\0';
    return (int)pos;
}

// Read exactly `len` bytes from stream into buf.  Returns true on success.
static bool read_exact(WiFiClient* stream, uint8_t* buf, size_t len, uint32_t timeout_ms) {
    size_t received = 0;
    uint32_t start = (uint32_t)millis();
    while (received < len) {
        if ((uint32_t)millis() - start > timeout_ms) return false;
        size_t avail = stream->available();
        if (avail == 0) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
        size_t chunk = (avail < len - received) ? avail : (len - received);
        int n = stream->readBytes(buf + received, chunk);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

static bool mjpeg_read_frame(int slot, uint8_t** out_data, size_t* out_len) {
    *out_data = nullptr;
    *out_len  = 0;

    SlotConn& c = g_conn[slot];
    const uint32_t LINE_TIMEOUT_MS  = 5000;
    const uint32_t FRAME_TIMEOUT_MS = 15000;

    // ---- Step 1: First call — open connection and parse headers ----
    if (!c.is_streaming) {
        int code = c.http.GET();
        if (code != 200) {
            LOGW(TAG, "MJPEG slot %d: HTTP %d", slot, code);
            c.http.end();
            if (code < 0) conn_close(slot);
            return false;
        }

        // Extract boundary from Content-Type header.
        // e.g. "multipart/x-mixed-replace; boundary=frame"
        String ct = c.http.header("Content-Type");
        if (ct.isEmpty()) ct = c.http.header("content-type");

        if (ct.indexOf("multipart") < 0) {
            // Server returned a single image, not a stream.
            // Read the body from this response directly to avoid a wasted
            // second GET when the caller falls back to snapshot mode.
            LOGD(TAG, "MJPEG slot %d: not multipart (%s) — snapshot", slot, ct.c_str());
            int cl = c.http.getSize();
            if (cl <= 0 || (size_t)cl > MAX_DOWNLOAD_SIZE) { c.http.end(); return false; }
            uint8_t* buf = (uint8_t*)heap_caps_malloc((size_t)cl, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) buf = (uint8_t*)malloc((size_t)cl);
            if (!buf) { c.http.end(); return false; }
            WiFiClient* s = c.http.getStreamPtr();
            size_t got = 0;
            uint32_t t0 = (uint32_t)millis();
            while (got < (size_t)cl && (uint32_t)millis() - t0 < MAX_DOWNLOAD_WALL_MS) {
                size_t a = s->available();
                if (a == 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
                size_t chunk = (a < (size_t)cl - got) ? a : ((size_t)cl - got);
                int nr = s->readBytes(buf + got, chunk);
                if (nr <= 0) break;
                got += nr;
            }
            c.http.end();
            if (got == 0) { heap_caps_free(buf); return false; }
            *out_data = buf;
            *out_len  = got;
            return true;
        }

        // Parse boundary (after "boundary=")
        int bi = ct.indexOf("boundary=");
        if (bi < 0) {
            LOGW(TAG, "MJPEG slot %d: no boundary in Content-Type", slot);
            c.http.end();
            conn_close(slot);
            return false;
        }
        String bnd = ct.substring(bi + 9);
        bnd.trim();
        // Some cameras wrap boundary in quotes
        if (bnd.startsWith("\"") && bnd.endsWith("\"")) bnd = bnd.substring(1, bnd.length() - 1);
        strlcpy(c.boundary, bnd.c_str(), sizeof(c.boundary));
        c.is_streaming = true;

        LOGI(TAG, "MJPEG slot %d: stream open, boundary='%s'", slot, c.boundary);
        // Intentional fall-through: read first frame from the now-open stream.
    }

    // ---- Step 2: Read next frame from open stream ----
    WiFiClient* stream = c.http.getStreamPtr();
    if (!stream || !stream->connected()) {
        LOGW(TAG, "MJPEG slot %d: stream disconnected", slot);
        conn_close(slot);
        return false;
    }

    char line[256];
    // Scan for the boundary line (--<boundary>)
    uint32_t search_start = (uint32_t)millis();
    bool found_boundary = false;
    while ((uint32_t)millis() - search_start < FRAME_TIMEOUT_MS) {
        int n = read_line(stream, line, sizeof(line), LINE_TIMEOUT_MS);
        if (n < 0) {
            LOGW(TAG, "MJPEG slot %d: timeout waiting for boundary", slot);
            conn_close(slot);
            return false;
        }
        // Match "--<boundary>" or "----<boundary>" (some cameras prefix with --)
        if (strstr(line, c.boundary) != nullptr) {
            found_boundary = true;
            break;
        }
    }
    if (!found_boundary) {
        LOGW(TAG, "MJPEG slot %d: boundary not found within timeout", slot);
        conn_close(slot);
        return false;
    }

    // Read part headers until blank line, extracting Content-Length.
    int content_length = -1;
    for (int header_i = 0; header_i < 16; header_i++) {
        int n = read_line(stream, line, sizeof(line), LINE_TIMEOUT_MS);
        if (n < 0) { conn_close(slot); return false; }
        if (n == 0) break;  // blank line = end of part headers
        if (strncasecmp(line, "content-length:", 15) == 0) {
            content_length = atoi(line + 15);
        }
    }

    if (content_length <= 0 || (size_t)content_length > MAX_DOWNLOAD_SIZE) {
        LOGW(TAG, "MJPEG slot %d: bad Content-Length %d", slot, content_length);
        conn_close(slot);
        return false;
    }

    // Allocate PSRAM buffer and read the JPEG body.
    uint8_t* buf = (uint8_t*)heap_caps_malloc((size_t)content_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)malloc((size_t)content_length);
    if (!buf) {
        LOGE(TAG, "MJPEG slot %d: OOM %d bytes", slot, content_length);
        conn_close(slot);
        return false;
    }

    if (!read_exact(stream, buf, (size_t)content_length, FRAME_TIMEOUT_MS)) {
        LOGW(TAG, "MJPEG slot %d: incomplete frame (%d bytes)", slot, content_length);
        heap_caps_free(buf);
        conn_close(slot);
        return false;
    }

    *out_data = buf;
    *out_len  = (size_t)content_length;
    return true;
}

// ============================================================================
// Fetch task — round-robin through slots
// ============================================================================

static void fetch_task(void* param) {
    (void)param;
    LOGI(TAG, "Fetch task started");

    int8_t last_slot = -1;

    for (;;) {
        // Find next slot to fetch
        xSemaphoreTake(g_mutex, portMAX_DELAY);

        int8_t next = -1;
        uint32_t now = (uint32_t)millis();
        int8_t scan_start = (last_slot + 1 < IMAGE_SLOT_MAX) ? last_slot + 1 : 0;
        uint32_t min_wait_ms = IDLE_DELAY_MS;  // Shortest time until any slot is due

        // Round-robin: scan from last_slot+1 through all slots
        for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) {
            int8_t idx = (scan_start + i) % IMAGE_SLOT_MAX;
            ImageSlot& s = g_slots[idx];
            if (!s.active || s.paused || g_suspended) continue;

            // Check if this slot is due for a fetch
            if (!s.fetched_once) {
                // Never fetched — do it now
                next = idx;
                break;
            }
            // For MJPEG streams with interval_ms == 0, the server controls frame
            // rate (real-time mode). With interval_ms > 0, throttle the same way
            // as snapshots — TCP backpressure stalls the server between reads,
            // which reduces PSRAM bandwidth and frees the LVGL task to render.
            if (g_conn[idx].is_streaming && s.interval_ms == 0) {
                next = idx;
                break;
            }
            if (s.interval_ms > 0) {
                uint32_t elapsed = now - s.last_fetch_ms;
                if (elapsed >= s.interval_ms) {
                    next = idx;
                    break;
                }
                // Track how long until this slot becomes due
                uint32_t remaining = s.interval_ms - elapsed;
                if (remaining < min_wait_ms) min_wait_ms = remaining;
            }
            // Snapshot slots with interval_ms == 0 that have already fetched
            // once stay idle ("fetch once" mode). Streaming + interval_ms == 0
            // was handled by the earlier real-time branch.
        }

        xSemaphoreGive(g_mutex);

        if (next < 0) {
            // No slot ready — close connections for inactive/cancelled/paused slots
            for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) {
                if (g_conn[i].active && (!g_slots[i].active || g_slots[i].paused)) conn_close(i);
            }
            // Sleep until the soonest slot is due
            if (min_wait_ms < 10) min_wait_ms = 10;  // Floor to avoid busy-spin
            vTaskDelay(pdMS_TO_TICKS(min_wait_ms));
            continue;
        }

        last_slot = next;

        // Copy slot info (under mutex) for the fetch
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        ImageSlot& slot = g_slots[next];
        if (!slot.active) {
            xSemaphoreGive(g_mutex);
            continue;
        }
        char url[SLOT_URL_MAX_LEN];
        char user[SLOT_USER_MAX_LEN];
        char pass[SLOT_PASS_MAX_LEN];
        uint16_t tw = slot.target_w;
        uint16_t th = slot.target_h;
        ImageScaleMode sm = slot.scale_mode;
        strlcpy(url, slot.url, sizeof(url));
        strlcpy(user, slot.user, sizeof(user));
        strlcpy(pass, slot.pass, sizeof(pass));
        xSemaphoreGive(g_mutex);

        // Persistent connection: ensure client exists, then fetch a frame.
        // MJPEG streaming: if the server responds with multipart/x-mixed-replace,
        // mjpeg_read_frame() keeps the socket open and reads one frame per call.
        // Snapshot mode: conn_download() does a GET per frame (existing behaviour).
        // The mode is auto-detected on the first GET and cached in SlotConn.
        uint8_t* raw_data = nullptr;
        size_t raw_len = 0;

        if (WiFi.status() != WL_CONNECTED) {
            LOGW(TAG, "WiFi not connected, skipping slot %d", next);
            // Close all persistent connections — they're dead sockets now
            for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) conn_close(i);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!conn_ensure(next, url, user, pass)) {
            LOGW(TAG, "Slot %d connection failed", next);
            xSemaphoreTake(g_mutex, portMAX_DELAY);
            if (g_slots[next].active) {
                g_slots[next].last_fetch_ms = (uint32_t)millis();
                g_slots[next].fetched_once = true;
            }
            xSemaphoreGive(g_mutex);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        bool got_frame;
        if (g_conn[next].is_streaming) {
            // Already in MJPEG streaming mode — read next frame from open socket.
            got_frame = mjpeg_read_frame(next, &raw_data, &raw_len);
        } else {
            // First fetch (or after reconnect): try MJPEG first.
            // mjpeg_read_frame auto-detects: if the server returns multipart,
            // it sets is_streaming and returns the first frame.  If single-image,
            // it reads the body from the same response and returns it directly
            // (no wasted second GET).  Fall back to conn_download only on error.
            got_frame = mjpeg_read_frame(next, &raw_data, &raw_len);
            if (!got_frame && !g_conn[next].is_streaming) {
                if (conn_ensure(next, url, user, pass)) {
                    got_frame = conn_download(next, &raw_data, &raw_len);
                }
            }
        }

        if (!got_frame) {
            LOGW(TAG, "Slot %d download failed", next);
            // Mark last_fetch to avoid immediate retry
            xSemaphoreTake(g_mutex, portMAX_DELAY);
            if (g_slots[next].active) {
                g_slots[next].last_fetch_ms = (uint32_t)millis();
                g_slots[next].fetched_once = true;
            }
            xSemaphoreGive(g_mutex);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Decode + scale to RGB565
        uint16_t* pixels = nullptr;
        size_t pixel_size = 0;

        bool decoded = image_decode_to_rgb565(raw_data, raw_len, tw, th, sm, &pixels, &pixel_size);
        heap_caps_free(raw_data);

        if (!decoded || !pixels) {
            LOGW(TAG, "Slot %d decode failed", next);
            xSemaphoreTake(g_mutex, portMAX_DELAY);
            if (g_slots[next].active) {
                g_slots[next].last_fetch_ms = (uint32_t)millis();
                g_slots[next].fetched_once = true;
            }
            xSemaphoreGive(g_mutex);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Swap into slot's back buffer, then promote to front
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        ImageSlot& s = g_slots[next];
        if (s.active && !s.paused) {
            // Free old back buffer and assign new decoded pixels
            if (s.back_buf) heap_caps_free(s.back_buf);
            s.back_buf = pixels;
            s.buf_size = pixel_size;

            // Atomic swap: move current front to back, new decode to front
            uint16_t* old_front = s.front_buf;
            s.front_buf = s.back_buf;
            s.back_buf = old_front;
            if (s.new_frame) {
                s.frame_drops++;
            }
            s.new_frame = true;
            s.last_fetch_ms = (uint32_t)millis();
            s.fetched_once = true;
        } else {
            // Slot was cancelled or paused while we were decoding
            heap_caps_free(pixels);
        }
        xSemaphoreGive(g_mutex);

        // Yield after each successful decode so other tasks (especially LVGL)
        // get CPU time. MJPEG streaming previously yielded only 1 ms, which
        // could starve the LVGL render task on the same core and saturate
        // PSRAM bandwidth on P4 boards. 10 ms is a safe floor that still
        // allows ~100 fps if the server pushes that fast.
        vTaskDelay(pdMS_TO_TICKS(g_conn[next].is_streaming ? 10 : 5));
    }
}

// ============================================================================
// Public API
// ============================================================================

void image_fetch_init() {
    if (g_task) return;  // Already initialized

    // Allocate slot and connection arrays in PSRAM to save ~30 KB internal RAM.
    g_slots = (ImageSlot*)heap_caps_calloc(IMAGE_SLOT_MAX, sizeof(ImageSlot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_slots) { LOGE(TAG, "OOM: g_slots (%u bytes)", (unsigned)(IMAGE_SLOT_MAX * sizeof(ImageSlot))); return; }

    void* conn_mem = heap_caps_calloc(IMAGE_SLOT_MAX, sizeof(SlotConn), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!conn_mem) {
        LOGE(TAG, "OOM: g_conn (%u bytes)", (unsigned)(IMAGE_SLOT_MAX * sizeof(SlotConn)));
        heap_caps_free(g_slots); g_slots = nullptr;
        return;
    }
    g_conn = (SlotConn*)conn_mem;
    for (int i = 0; i < IMAGE_SLOT_MAX; i++) new (&g_conn[i]) SlotConn();

    g_mutex = xSemaphoreCreateMutex();

    // Pin the fetch task to core 0 (PRO_CPU). The LVGL render task runs on
    // core 1 (APP_CPU); isolating image fetch+decode to core 0 prevents CPU
    // contention with LVGL flushes. PSRAM bandwidth is still shared, but at
    // least the cores no longer fight for compute time.
    bool ok = rtos_create_task_psram_stack_pinned(
        fetch_task, "img_fetch",
        FETCH_TASK_STACK_WORDS, nullptr,
        FETCH_TASK_PRIORITY, &g_task, &g_task_alloc,
        0);

    if (!ok) {
        LOGE(TAG, "Failed to create fetch task");
        g_task = nullptr;
    } else {
        LOGI(TAG, "Fetch task created (stack=%u words, pri=%u)",
             (unsigned)FETCH_TASK_STACK_WORDS, (unsigned)FETCH_TASK_PRIORITY);
    }
}

image_slot_t image_fetch_request(
    const char* url, const char* user, const char* pass,
    uint16_t target_w, uint16_t target_h, uint32_t interval_ms,
    ImageScaleMode scale_mode)
{
    if (!url || !url[0] || target_w == 0 || target_h == 0) return IMAGE_SLOT_INVALID;
    if (!g_mutex) return IMAGE_SLOT_INVALID;

    xSemaphoreTake(g_mutex, portMAX_DELAY);

    // Find a free slot
    image_slot_t id = IMAGE_SLOT_INVALID;
    for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) {
        if (!g_slots[i].active) {
            id = i;
            break;
        }
    }

    if (id == IMAGE_SLOT_INVALID) {
        xSemaphoreGive(g_mutex);
        LOGW(TAG, "No free slots");
        return IMAGE_SLOT_INVALID;
    }

    ImageSlot& s = g_slots[id];
    memset(&s, 0, sizeof(ImageSlot));
    s.active = true;
    strlcpy(s.url, url, sizeof(s.url));
    if (user) strlcpy(s.user, user, sizeof(s.user));
    if (pass) strlcpy(s.pass, pass, sizeof(s.pass));
    s.target_w = target_w;
    s.target_h = target_h;
    s.interval_ms = interval_ms;
    s.scale_mode = scale_mode;

    xSemaphoreGive(g_mutex);

    LOGI(TAG, "Slot %d: %.60s %ux%u interval=%ums", id, url, target_w, target_h, (unsigned)interval_ms);
    return id;
}

void image_fetch_cancel(image_slot_t slot) {
    if (!g_slots || slot < 0 || slot >= IMAGE_SLOT_MAX) return;
    if (!g_mutex) return;

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    ImageSlot& s = g_slots[slot];
    if (s.active) {
        if (s.front_buf) { heap_caps_free(s.front_buf); s.front_buf = nullptr; }
        if (s.back_buf) { heap_caps_free(s.back_buf); s.back_buf = nullptr; }
        if (s.lvgl_buf) { heap_caps_free(s.lvgl_buf); s.lvgl_buf = nullptr; }
        s.active = false;
        LOGD(TAG, "Slot %d cancelled", slot);
    }
    xSemaphoreGive(g_mutex);
}

void image_fetch_cancel_all() {
    if (!g_slots || !g_mutex) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) {
        ImageSlot& s = g_slots[i];
        if (s.active) {
            if (s.front_buf) { heap_caps_free(s.front_buf); s.front_buf = nullptr; }
            if (s.back_buf) { heap_caps_free(s.back_buf); s.back_buf = nullptr; }
            if (s.lvgl_buf) { heap_caps_free(s.lvgl_buf); s.lvgl_buf = nullptr; }
            s.active = false;
        }
    }
    xSemaphoreGive(g_mutex);
    LOGD(TAG, "All slots cancelled");
}

void image_fetch_pause_slot(image_slot_t slot) {
    if (!g_slots || slot < 0 || slot >= IMAGE_SLOT_MAX) return;
    if (!g_mutex) return;

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    ImageSlot& s = g_slots[slot];
    s.paused = true;
    // Free double-buffers to reclaim PSRAM while the page is hidden.
    // lvgl_buf is unaffected, so LVGL keeps showing the last frame.
    // Buffers are re-allocated on next fetch after resume.
    size_t freed = 0;
    if (s.front_buf) { freed += s.buf_size; heap_caps_free(s.front_buf); s.front_buf = nullptr; }
    if (s.back_buf)  { freed += s.buf_size; heap_caps_free(s.back_buf);  s.back_buf  = nullptr; }
    s.buf_size = 0;
    xSemaphoreGive(g_mutex);

    if (freed) LOGD(TAG, "Slot %d paused, freed %u bytes", slot, (unsigned)freed);
}

void image_fetch_resume_slot(image_slot_t slot) {
    if (!g_slots || slot < 0 || slot >= IMAGE_SLOT_MAX) return;
    g_slots[slot].paused = false;
}

void image_fetch_pause() {
    if (!g_slots) return;
    for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) {
        if (g_slots[i].active) g_slots[i].paused = true;
    }
    LOGD(TAG, "Paused all");
}

void image_fetch_resume() {
    if (!g_slots) return;
    for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) {
        if (g_slots[i].active) g_slots[i].paused = false;
    }
    LOGD(TAG, "Resumed all");
}

void image_fetch_suspend() {
    g_suspended = true;
    LOGD(TAG, "Suspended (screen saver)");
}

void image_fetch_unsuspend() {
    g_suspended = false;
    LOGD(TAG, "Unsuspended (screen saver)");
}

bool image_fetch_has_new_frame(image_slot_t slot) {
    if (!g_slots || slot < 0 || slot >= IMAGE_SLOT_MAX) return false;
    return g_slots[slot].active && g_slots[slot].new_frame;
}

// Thread-safety contract for the returned pointer:
//  * Under g_mutex, swap front_buf <-> lvgl_buf when a new frame is ready.
//    The returned pointer is lvgl_buf, which the fetch task never touches.
//    LVGL can safely render from it (no memcpy, no lock held during render)
//    until the next image_fetch_get_frame() call for the same slot.
//  * If no new frame is ready, the previous lvgl_buf is returned again.
//  * cancel()/cancel_all() may free lvgl_buf, but they only run from the
//    LVGL task (clearTiles), which is the same task that calls get_frame,
//    so there is no concurrent free.
const uint16_t* image_fetch_get_frame(image_slot_t slot, uint16_t* out_w, uint16_t* out_h) {
    if (!g_slots || slot < 0 || slot >= IMAGE_SLOT_MAX) return nullptr;
    if (!g_mutex) return nullptr;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    ImageSlot& s = g_slots[slot];
    if (!s.active) {
        xSemaphoreGive(g_mutex);
        return nullptr;
    }
    // Promote the freshly-decoded front_buf to LVGL ownership.
    if (s.new_frame && s.front_buf) {
        uint16_t* prev_lvgl = s.lvgl_buf;
        s.lvgl_buf = s.front_buf;
        s.front_buf = prev_lvgl;   // may be nullptr on first hand-off
        s.new_frame = false;
    }
    uint16_t* result = s.lvgl_buf;
    if (out_w) *out_w = s.target_w;
    if (out_h) *out_h = s.target_h;
    xSemaphoreGive(g_mutex);
    return result;
}

uint32_t image_fetch_get_drops(image_slot_t slot) {
    if (!g_slots || slot < 0 || slot >= IMAGE_SLOT_MAX) return 0;
    return g_slots[slot].frame_drops;
}

#endif // HAS_IMAGE_FETCH
