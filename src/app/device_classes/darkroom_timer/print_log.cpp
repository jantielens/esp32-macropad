#include "print_log.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "binding_template.h"
#include "log_manager.h"

#if HAS_DISPLAY
#include "display_manager.h"
#endif

#include <ArduinoJson.h>
#include "storage.h"
#include <Preferences.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define TAG "PrintLog"

#define NVS_NAMESPACE       "print_log"
#define NVS_KEY_DATE        "print_date"
#define NVS_KEY_SEQ         "print_seq"
#define NVS_KEY_SEQ_FB      "print_seq_fb"
#define NVS_KEY_COUNT       "print_count"

// ============================================================================
// State
// ============================================================================

static Preferences s_prefs;

// Binding state
static char s_next_id[16]  = "---";
static char s_last_id[16]  = "---";
static uint16_t s_count    = 0;
static bool s_last_starred = false;

// Deferred I/O — pending save
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

enum PendingType : uint8_t {
    PENDING_NONE = 0,
    PENDING_EXPOSURE,
    PENDING_STRIP,
};

static PendingType           s_pending_type = PENDING_NONE;
static PrintLogExposureData  s_pending_exposure;
static PrintLogStripData     s_pending_strip;

// Star pending — independent of exposure/strip so a star toggle
// never silently drops an unprocessed save (or vice-versa).
static bool                  s_pending_star_active = false;
static bool                  s_pending_starred;    // target starred state
static char                  s_pending_star_id[16]; // print ID to update

// ============================================================================
// ID Generation
// ============================================================================

static uint32_t current_date_yymmdd() {
    time_t now = 0;
    time(&now);
    // If time is before 2024, NTP hasn't synced
    if (now < 1704067200) return 0;
    struct tm tm;
    localtime_r(&now, &tm);
    return (uint32_t)((tm.tm_year % 100) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
}

static void format_id(uint32_t date, uint16_t seq, char* buf, size_t len) {
    snprintf(buf, len, "%06u-%03u", (unsigned)date, (unsigned)seq);
}

// Compute the next ID and update NVS counters. Returns the assigned ID.
static void generate_next_id(char* out_id, size_t id_len) {
    uint32_t today = current_date_yymmdd();

    if (today == 0) {
        // No NTP — use fallback monotonic counter
        uint16_t fb_seq = s_prefs.getUShort(NVS_KEY_SEQ_FB, 0) + 1;
        s_prefs.putUShort(NVS_KEY_SEQ_FB, fb_seq);
        format_id(0, fb_seq, out_id, id_len);
    } else {
        uint32_t stored_date = s_prefs.getUInt(NVS_KEY_DATE, 0);
        uint16_t seq;
        if (stored_date != today) {
            seq = 1;
            s_prefs.putUInt(NVS_KEY_DATE, today);
        } else {
            seq = s_prefs.getUShort(NVS_KEY_SEQ, 0) + 1;
        }
        s_prefs.putUShort(NVS_KEY_SEQ, seq);
        format_id(today, seq, out_id, id_len);
    }
}

static void clear_current_id() {
    strlcpy(s_next_id, "---", sizeof(s_next_id));
}

// ============================================================================
// File path helpers
// ============================================================================

static void print_path(const char* id, char* buf, size_t len) {
    snprintf(buf, len, "%s/%s.json", PRINT_LOG_DIR, id);
}

// ============================================================================
// JSON writing helpers
// ============================================================================

static void write_json_string(File& f, const char* s) {
    f.print('"');
    while (*s) {
        if (*s == '"' || *s == '\\') f.print('\\');
        f.print(*s);
        s++;
    }
    f.print('"');
}

// ============================================================================
// Eviction — FIFO, oldest file first
// ============================================================================

static void evict_oldest() {
    File dir = Storage.open(PRINT_LOG_DIR);
    if (!dir || !dir.isDirectory()) return;

    char oldest_name[32] = "";
    File f = dir.openNextFile();
    while (f) {
        const char* name = f.name();
        if (!f.isDirectory() && strlen(name) > 0) {
            if (oldest_name[0] == '\0' || strcmp(name, oldest_name) < 0) {
                strlcpy(oldest_name, name, sizeof(oldest_name));
            }
        }
        f = dir.openNextFile();
    }

    if (oldest_name[0]) {
        char path[48];
        snprintf(path, sizeof(path), "%s/%s", PRINT_LOG_DIR, oldest_name);
        Storage.remove(path);
        LOGI(TAG, "Evicted oldest print: %s", oldest_name);
    }
}

// ============================================================================
// Display blanking — DSI panels DMA-scan PSRAM continuously; LittleFS flash
// writes cause bus contention that starves the display DMA, producing visible
// flicker.  Backlight-off alone is not enough on DSI panels because the DPI
// clock keeps running.  We send panel sleep commands (Sleep In / Sleep Out)
// to actually halt DMA during flash I/O, matching the screen saver pattern.
// ============================================================================

static uint8_t blank_display() {
#if HAS_DISPLAY
    uint8_t saved = display_manager_get_backlight_brightness();
    if (saved > 0) display_manager_set_backlight_brightness(0);
    if (displayManager && displayManager->getDriver()) {
        displayManager->lock();
        displayManager->getDriver()->displaySleep();
        displayManager->unlock();
    }
    return saved;
#else
    return 0;
#endif
}

static void restore_display(uint8_t saved) {
#if HAS_DISPLAY
    if (displayManager && displayManager->getDriver()) {
        displayManager->lock();
        displayManager->getDriver()->displayWake();
        // displaySleep() zeroes both DPI framebuffers, but LVGL's dirty-area
        // tracking still believes the screen is painted, so it would only
        // redraw self-invalidating widgets — leaving the rest black until the
        // next navigation. Force a full-screen invalidate so the entire UI
        // repaints into the freshly-blanked framebuffers.
        lv_obj_t* scr = lv_screen_active();
        if (scr) lv_obj_invalidate(scr);
        displayManager->unlock();
    }
    if (saved > 0) display_manager_set_backlight_brightness(saved);
#else
    (void)saved;
#endif
}

// ============================================================================
// Save — writes JSON to LittleFS (runs from loop() task)
// ============================================================================

static uint32_t get_timestamp() {
    time_t now = 0;
    time(&now);
    return (now > 1704067200) ? (uint32_t)now : 0;
}

static void save_exposure(const PrintLogExposureData& d) {
    char id[16];
    generate_next_id(id, sizeof(id));

    char path[48];
    print_path(id, path, sizeof(path));

    uint32_t ts = get_timestamp();

    // Blank display around all flash I/O (eviction + write)
    uint8_t saved_bl = blank_display();

    if (s_count >= DARKROOM_PRINT_LOG_MAX) {
        evict_oldest();
        s_count--;
    }

    File f = Storage.open(path, "w");
    if (!f) {
        restore_display(saved_bl);
        LOGE(TAG, "Failed to open %s", path);
        return;
    }

    f.print("{\"v\":1,\"fields\":[");
    f.print("{\"key\":\"type\",\"label\":\"Type\",\"value\":\"exposure\",\"format\":\"text\"},");
    f.printf("{\"key\":\"ts\",\"label\":\"Date\",\"value\":%lu,\"format\":\"datetime\"},", (unsigned long)ts);
    f.printf("{\"key\":\"id\",\"label\":\"Print ID\",\"value\":\"%s\",\"format\":\"text\"},", id);
    f.printf("{\"key\":\"set_time\",\"label\":\"Set Time\",\"value\":%.1f,\"unit\":\"s\",\"format\":\"duration_s\",\"loadable\":true},", d.set_time_s);
    f.printf("{\"key\":\"effective_time\",\"label\":\"Effective Time\",\"value\":%.1f,\"unit\":\"s\",\"format\":\"duration_s\"},", d.effective_time_s);
    f.printf("{\"key\":\"dry_down\",\"label\":\"Dry-Down\",\"value\":%.1f,\"unit\":\"%%\",\"format\":\"percent\",\"loadable\":true}", d.dry_down_pct);

    // Conditional metering context fields
    if (d.lref > 0.0f) {
        f.printf(",{\"key\":\"lref\",\"label\":\"Lref\",\"value\":%.1f,\"unit\":\"lux\",\"format\":\"number\"}", d.lref);
    }
    if (d.zone5_time > 0.0f) {
        f.printf(",{\"key\":\"zone5_time\",\"label\":\"Zone V Time\",\"value\":%.1f,\"unit\":\"s\",\"format\":\"duration_s\"}", d.zone5_time);
    }
    if (d.l_bright >= 0.0f) {
        f.printf(",{\"key\":\"l_bright\",\"label\":\"Bright Spot\",\"value\":%.1f,\"unit\":\"lux\",\"format\":\"number\"}", d.l_bright);
    }
    if (d.l_dark >= 0.0f) {
        f.printf(",{\"key\":\"l_dark\",\"label\":\"Dark Spot\",\"value\":%.1f,\"unit\":\"lux\",\"format\":\"number\"}", d.l_dark);
    }
    if (d.l_bright >= 0.0f && d.l_dark >= 0.0f) {
        f.printf(",{\"key\":\"sbr\",\"label\":\"SBR\",\"value\":%.2f,\"format\":\"number\"}", d.sbr);
        if (d.grade == (int)d.grade) {
            f.printf(",{\"key\":\"grade\",\"label\":\"Grade\",\"value\":%d,\"format\":\"number\"}", (int)d.grade);
        } else {
            f.printf(",{\"key\":\"grade\",\"label\":\"Grade\",\"value\":%.1f,\"format\":\"number\"}", d.grade);
        }
        if (d.grade_label) {
            f.print(",{\"key\":\"grade_label\",\"label\":\"Grade Label\",\"value\":");
            write_json_string(f, d.grade_label);
            f.print(",\"format\":\"text\"}");
        }
    }
    if (d.mag_factor > 0.0f) {
        f.printf(",{\"key\":\"mag_factor\",\"label\":\"Mag Factor\",\"value\":%.1f,\"format\":\"number\"}", d.mag_factor);
    }

    f.print("]}");
    f.close();

    // NVS write is also flash I/O — keep it inside the blanked region
    s_count++;
    s_prefs.putUShort(NVS_KEY_COUNT, s_count);

    restore_display(saved_bl);

    // Update RAM state (no flash)
    strlcpy(s_last_id, id, sizeof(s_last_id));
    strlcpy(s_next_id, id, sizeof(s_next_id));  // show assigned ID in [print:id]
    s_last_starred = false;  // new prints start unstarred

    LOGI(TAG, "Saved exposure %s (set=%.1fs eff=%.1fs dd=%.1f%%)", id, d.set_time_s, d.effective_time_s, d.dry_down_pct);
}

static void save_strip(const PrintLogStripData& d) {
    char id[16];
    generate_next_id(id, sizeof(id));

    char path[48];
    print_path(id, path, sizeof(path));

    uint32_t ts = get_timestamp();

    // Blank display around all flash I/O (eviction + write)
    uint8_t saved_bl = blank_display();

    if (s_count >= DARKROOM_PRINT_LOG_MAX) {
        evict_oldest();
        s_count--;
    }

    File f = Storage.open(path, "w");
    if (!f) {
        restore_display(saved_bl);
        LOGE(TAG, "Failed to open %s", path);
        return;
    }

    f.print("{\"v\":1,\"fields\":[");
    f.print("{\"key\":\"type\",\"label\":\"Type\",\"value\":\"test_strip\",\"format\":\"text\"},");
    f.printf("{\"key\":\"ts\",\"label\":\"Date\",\"value\":%lu,\"format\":\"datetime\"},", (unsigned long)ts);
    f.printf("{\"key\":\"id\",\"label\":\"Print ID\",\"value\":\"%s\",\"format\":\"text\"},", id);
    f.printf("{\"key\":\"base_time\",\"label\":\"Base Time\",\"value\":%.1f,\"unit\":\"s\",\"format\":\"duration_s\"},", d.base_time_s);

    // Step label — write as JSON string
    f.print("{\"key\":\"step\",\"label\":\"Step\",\"value\":");
    write_json_string(f, d.step_label ? d.step_label : "---");
    f.print(",\"format\":\"text\"},");

    f.printf("{\"key\":\"step_stops\",\"label\":\"Step (stops)\",\"value\":%.3f,\"format\":\"number\"},", d.step_stops);
    f.printf("{\"key\":\"segment_count\",\"label\":\"Segments\",\"value\":%d,\"format\":\"number\"}", d.segment_count);

    f.print("],\"segments\":[");
    for (int i = 0; i < d.segment_count && i < PRINT_LOG_MAX_SEGMENTS; i++) {
        if (i > 0) f.print(',');
        f.printf("{\"n\":%d,\"offset\":\"%+.1f\",\"cumulative_s\":%.1f,\"incremental_s\":%.1f}",
                 i + 1, d.segments[i].offset_stops,
                 d.segments[i].cumulative_s, d.segments[i].incremental_s);
    }
    f.print("]}");
    f.close();

    // NVS write is also flash I/O — keep it inside the blanked region
    s_count++;
    s_prefs.putUShort(NVS_KEY_COUNT, s_count);

    restore_display(saved_bl);

    // Update RAM state (no flash)
    strlcpy(s_last_id, id, sizeof(s_last_id));
    strlcpy(s_next_id, id, sizeof(s_next_id));  // show assigned ID in [print:id]
    s_last_starred = false;  // new prints start unstarred

    LOGI(TAG, "Saved strip %s (base=%.1fs step=%s segs=%d)", id, d.base_time_s, d.step_label, d.segment_count);
}

// ============================================================================
// Star write — deferred from LVGL task, runs on loop() task
// ============================================================================

static void write_star(const char* id, bool starred) {
    char path[48];
    print_path(id, path, sizeof(path));

    File f = Storage.open(path, "r");
    if (!f) {
        LOGE(TAG, "Star: failed to read %s", path);
        return;
    }
    size_t sz = f.size();
    if (sz > 4096) { f.close(); return; }
    char* buf = (char*)malloc(sz + 1);
    if (!buf) { f.close(); return; }
    f.readBytes(buf, sz);
    buf[sz] = '\0';
    f.close();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf, sz);
    free(buf);
    if (err) {
        LOGE(TAG, "Star: JSON parse error %s", path);
        return;
    }

    if (starred) {
        doc["starred"] = true;
    } else {
        doc.remove("starred");
    }

    uint8_t saved_bl = blank_display();
    f = Storage.open(path, "w");
    if (!f) {
        restore_display(saved_bl);
        LOGE(TAG, "Star: failed to write %s", path);
        return;
    }
    serializeJson(doc, f);
    f.close();
    restore_display(saved_bl);

    LOGI(TAG, "Print %s %s", id, starred ? "starred" : "unstarred");
}

// ============================================================================
// Binding resolver
// ============================================================================

static bool print_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:no_key");
        return false;
    }

    if (strcmp(params, "id") == 0) {
        snprintf(out, out_len, "%s", s_next_id);
        return true;
    }
    if (strcmp(params, "last_id") == 0) {
        snprintf(out, out_len, "%s", s_last_id);
        return true;
    }
    if (strcmp(params, "count") == 0) {
        snprintf(out, out_len, "%u", (unsigned)s_count);
        return true;
    }
    if (strcmp(params, "starred") == 0) {
        snprintf(out, out_len, "%d", s_last_starred ? 1 : 0);
        return true;
    }
    if (strcmp(params, "star_label") == 0) {
        snprintf(out, out_len, "%s", s_last_starred ? "*" : "");
        return true;
    }

    snprintf(out, out_len, "ERR:bad_key");
    return false;
}

static void print_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Public API
// ============================================================================

void print_log_pend_exposure(const PrintLogExposureData& data) {
    portENTER_CRITICAL(&s_lock);
    s_pending_exposure = data;
    s_pending_type = PENDING_EXPOSURE;
    portEXIT_CRITICAL(&s_lock);
}

void print_log_pend_strip(const PrintLogStripData& data) {
    portENTER_CRITICAL(&s_lock);
    s_pending_strip = data;
    s_pending_type = PENDING_STRIP;
    portEXIT_CRITICAL(&s_lock);
}

void print_log_loop() {
    portENTER_CRITICAL(&s_lock);
    PendingType type = s_pending_type;
    PrintLogExposureData exp_copy;
    PrintLogStripData strip_copy;
    bool has_star = s_pending_star_active;
    bool star_val = false;
    char star_id[16] = "";
    if (type == PENDING_EXPOSURE) {
        exp_copy = s_pending_exposure;
    } else if (type == PENDING_STRIP) {
        strip_copy = s_pending_strip;
    }
    if (has_star) {
        star_val = s_pending_starred;
        strlcpy(star_id, s_pending_star_id, sizeof(star_id));
        s_pending_star_active = false;
    }
    s_pending_type = PENDING_NONE;
    portEXIT_CRITICAL(&s_lock);

    if (type == PENDING_EXPOSURE) {
        save_exposure(exp_copy);
    } else if (type == PENDING_STRIP) {
        save_strip(strip_copy);
    }
    if (has_star) {
        write_star(star_id, star_val);
    }
}

void print_log_clear_id() {
    clear_current_id();
}

void print_log_dispatch(const char* command, const char* value) {
    // No-op if no print has been saved yet
    if (strcmp(s_last_id, "---") == 0 || s_last_id[0] == '\0') return;

    bool new_starred;
    if (strcmp(command, "toggle_star") == 0) {
        new_starred = !s_last_starred;
    } else if (strcmp(command, "set_star") == 0) {
        new_starred = (value && strcmp(value, "1") == 0);
    } else {
        LOGW(TAG, "Unknown print command: %s", command);
        return;
    }

    // Update RAM state immediately so bindings reflect the change
    s_last_starred = new_starred;

    // Defer file write to loop() task (LVGL task stack is in PSRAM,
    // which is inaccessible during SPI flash operations)
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_pending_star_id, s_last_id, sizeof(s_pending_star_id));
    s_pending_starred = new_starred;
    s_pending_star_active = true;
    portEXIT_CRITICAL(&s_lock);
}

uint16_t print_log_get_count() {
    return s_count;
}

void print_log_init() {
    s_prefs.begin(NVS_NAMESPACE, false);

    if (!Storage.exists(PRINT_LOG_DIR)) {
        Storage.mkdir(PRINT_LOG_DIR);
    }

    // Load count from NVS
    s_count = s_prefs.getUShort(NVS_KEY_COUNT, 0);

    // Reset session state
    clear_current_id();
    strlcpy(s_last_id, "---", sizeof(s_last_id));
    s_last_starred = false;

    if (!binding_template_register("print", print_resolve, print_collect)) {
        LOGE(TAG, "Failed to register print binding scheme");
    } else {
        LOGI(TAG, "Print binding scheme registered");
    }

    LOGI(TAG, "Init: next=%s count=%u", s_next_id, (unsigned)s_count);
}

#endif // IS_DARKROOM_TIMER
