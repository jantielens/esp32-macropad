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

    // Double-buffered pixel data
    uint16_t* front_buf;       // Read by LVGL (stable pointer)
    uint16_t* back_buf;        // Written by fetch task
    size_t buf_size;           // Byte size of each buffer

    volatile bool new_frame;   // Set by fetch task, cleared by ack
    bool paused;               // Per-slot pause (hidden page)
};

// ============================================================================
// Module state
// ============================================================================

static ImageSlot g_slots[IMAGE_SLOT_MAX];
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
};

static SlotConn g_conn[IMAGE_SLOT_MAX];

static void conn_close(int slot) {
    SlotConn& c = g_conn[slot];
    if (!c.active) return;
    c.http.end();
    // Let WiFi MAC finish processing TCP close frames before destroying
    // the client.  Without this delay the MAC DMA can access freed memory
    // from the client's internal lwIP structures.
    vTaskDelay(pdMS_TO_TICKS(100));
    delete c.tls;   c.tls = nullptr;
    delete c.plain;  c.plain = nullptr;
    c.active = false;
    c.is_https = false;
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
        }

        xSemaphoreGive(g_mutex);

        if (next < 0) {
            // No slot ready — close connections for inactive/cancelled slots
            for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) {
                if (g_conn[i].active && !g_slots[i].active) conn_close(i);
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

        LOGD(TAG, "Fetch slot %d: %.60s (%ux%u)", next, url, tw, th);

        // --- Heap integrity checkpoint: BEFORE download ---
        if (!heap_caps_check_integrity_all(true)) {
            LOGE(TAG, "HEAP CORRUPT before download (slot %d)", next);
        }

        // Persistent connection: ensure client exists, then download
        uint8_t* raw_data = nullptr;
        size_t raw_len = 0;

        if (WiFi.status() != WL_CONNECTED) {
            LOGW(TAG, "WiFi not connected, skipping slot %d", next);
            // Close all persistent connections — they're dead sockets now
            for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) conn_close(i);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!conn_ensure(next, url, user, pass) ||
            !conn_download(next, &raw_data, &raw_len)) {
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

        // --- Heap integrity checkpoint: AFTER download, BEFORE decode ---
        if (!heap_caps_check_integrity_all(true)) {
            LOGE(TAG, "HEAP CORRUPT after download (slot %d, %u bytes)", next, (unsigned)raw_len);
        }

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
        if (s.active) {
            // Free old back buffer and assign new decoded pixels
            if (s.back_buf) heap_caps_free(s.back_buf);
            s.back_buf = pixels;
            s.buf_size = pixel_size;

            // Atomic swap: move current front to back, new decode to front
            uint16_t* old_front = s.front_buf;
            s.front_buf = s.back_buf;
            s.back_buf = old_front;
            s.new_frame = true;
            s.last_fetch_ms = (uint32_t)millis();
            s.fetched_once = true;

            LOGD(TAG, "Slot %d frame ready (%ux%u, %u bytes)", next, tw, th, (unsigned)pixel_size);
        } else {
            // Slot was cancelled while we were decoding
            heap_caps_free(pixels);
        }
        xSemaphoreGive(g_mutex);

        // --- Heap integrity checkpoint: AFTER decode + swap ---
        if (!heap_caps_check_integrity_all(true)) {
            LOGE(TAG, "HEAP CORRUPT after decode/swap (slot %d)", next);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// Public API
// ============================================================================

void image_fetch_init() {
    if (g_task) return;  // Already initialized

    g_mutex = xSemaphoreCreateMutex();
    memset(g_slots, 0, sizeof(g_slots));

    bool ok = rtos_create_task_psram_stack_pinned(
        fetch_task, "img_fetch",
        FETCH_TASK_STACK_WORDS, nullptr,
        FETCH_TASK_PRIORITY, &g_task, &g_task_alloc,
        tskNO_AFFINITY);

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
    if (slot < 0 || slot >= IMAGE_SLOT_MAX) return;
    if (!g_mutex) return;

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    ImageSlot& s = g_slots[slot];
    if (s.active) {
        if (s.front_buf) { heap_caps_free(s.front_buf); s.front_buf = nullptr; }
        if (s.back_buf) { heap_caps_free(s.back_buf); s.back_buf = nullptr; }
        s.active = false;
        LOGD(TAG, "Slot %d cancelled", slot);
    }
    xSemaphoreGive(g_mutex);
}

void image_fetch_cancel_all() {
    if (!g_mutex) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) {
        ImageSlot& s = g_slots[i];
        if (s.active) {
            if (s.front_buf) { heap_caps_free(s.front_buf); s.front_buf = nullptr; }
            if (s.back_buf) { heap_caps_free(s.back_buf); s.back_buf = nullptr; }
            s.active = false;
        }
    }
    xSemaphoreGive(g_mutex);
    LOGD(TAG, "All slots cancelled");
}

void image_fetch_pause_slot(image_slot_t slot) {
    if (slot < 0 || slot >= IMAGE_SLOT_MAX) return;
    g_slots[slot].paused = true;
}

void image_fetch_resume_slot(image_slot_t slot) {
    if (slot < 0 || slot >= IMAGE_SLOT_MAX) return;
    g_slots[slot].paused = false;
}

void image_fetch_pause() {
    for (int8_t i = 0; i < IMAGE_SLOT_MAX; i++) {
        if (g_slots[i].active) g_slots[i].paused = true;
    }
    LOGD(TAG, "Paused all");
}

void image_fetch_resume() {
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
    if (slot < 0 || slot >= IMAGE_SLOT_MAX) return false;
    return g_slots[slot].active && g_slots[slot].new_frame;
}

// Thread-safety note: get_frame / ack_frame deliberately omit the mutex.
// This is safe because:
//  1. Pointer reads/writes are atomic on 32-bit ESP32.
//  2. The fetch task never mutates front_buf's *data* — it decodes into a
//     fresh allocation, then swaps front_buf under mutex.  The old front
//     (now back_buf) survives until the *next* network round-trip (seconds),
//     while the LVGL-side memcpy in pollImageFrames() takes microseconds.
//  3. cancel() only runs from clearTiles() in the LVGL task — the same
//     task that calls pollImageFrames() — so no concurrent free is possible.
//  4. The caller (pollImageFrames) copies the data into its own owned_pixels
//     buffer immediately, so it never holds the front_buf pointer long-term.
const uint16_t* image_fetch_get_frame(image_slot_t slot, uint16_t* out_w, uint16_t* out_h) {
    if (slot < 0 || slot >= IMAGE_SLOT_MAX) return nullptr;
    const ImageSlot& s = g_slots[slot];
    if (!s.active || !s.front_buf) return nullptr;
    if (out_w) *out_w = s.target_w;
    if (out_h) *out_h = s.target_h;
    return s.front_buf;
}

void image_fetch_ack_frame(image_slot_t slot) {
    if (slot < 0 || slot >= IMAGE_SLOT_MAX) return;
    g_slots[slot].new_frame = false;
}

#endif // HAS_IMAGE_FETCH
