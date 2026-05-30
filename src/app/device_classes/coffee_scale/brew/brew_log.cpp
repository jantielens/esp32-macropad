#include "brew_log.h"

#if HAS_SCALE

#include "fs_health.h"
#include "log_manager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

#define TAG "BrewLog"

#define BREW_LOG_NVS_NAMESPACE  "brew_log"
#define BREW_LOG_NVS_KEY_NEXT   "next_id"

static Preferences s_prefs;

// ============================================================================
// Helpers
// ============================================================================

// Write a JSON-safe string to file (escapes \ and ").
static void write_json_string(File& f, const char* s) {
    f.print('"');
    while (*s) {
        if (*s == '"' || *s == '\\') f.print('\\');
        f.print(*s);
        s++;
    }
    f.print('"');
}

static void brew_log_path(uint16_t id, char* buf, size_t len) {
    snprintf(buf, len, "%s/%04u.json", BREW_LOG_DIR, (unsigned)id);
}

static uint16_t next_id() {
    return s_prefs.getUShort(BREW_LOG_NVS_KEY_NEXT, 1);
}

static void set_next_id(uint16_t id) {
    s_prefs.putUShort(BREW_LOG_NVS_KEY_NEXT, id);
}

// Scan /brews/ to find the lowest ID file (for eviction).
static uint16_t find_oldest_id() {
    File dir = LittleFS.open(BREW_LOG_DIR);
    if (!dir || !dir.isDirectory()) return 0;

    uint16_t oldest = UINT16_MAX;
    File f = dir.openNextFile();
    while (f) {
        const char* name = f.name();
        // Parse NNNN from "NNNN.json"
        unsigned id = 0;
        if (sscanf(name, "%u.json", &id) == 1 && id < oldest) {
            oldest = (uint16_t)id;
        }
        f = dir.openNextFile();
    }
    return (oldest == UINT16_MAX) ? 0 : oldest;
}

static void evict_oldest() {
    uint16_t oldest = find_oldest_id();
    if (oldest == 0) return;

    char path[32];
    brew_log_path(oldest, path, sizeof(path));
    LittleFS.remove(path);
    LOGI(TAG, "Evicted brew %u", (unsigned)oldest);
}

// ============================================================================
// Init
// ============================================================================

void brew_log_init() {
    s_prefs.begin(BREW_LOG_NVS_NAMESPACE, false);

    if (!LittleFS.exists(BREW_LOG_DIR)) {
        LittleFS.mkdir(BREW_LOG_DIR);
    }

    LOGI(TAG, "Init: next_id=%u, count=%u", (unsigned)next_id(), (unsigned)brew_log_count());
}

// ============================================================================
// Save
// ============================================================================

uint16_t brew_log_save(uint32_t elapsed_ms, float final_weight,
                       const BrewTemplate* tmpl, float dose_weight,
                       const BrewSample* series, uint16_t sample_count,
                       const BrewMarker* markers, uint8_t marker_count,
                       const BrewCapture* captures, uint8_t capture_count) {
    const char* template_name = tmpl ? tmpl->name : "free_pour";
    // Evict if at capacity
    if (brew_log_count() >= BREW_LOG_MAX_BREWS) {
        evict_oldest();
    }

    uint16_t id = next_id();
    char path[32];
    brew_log_path(id, path, sizeof(path));

    // Get timestamp (epoch seconds, 0 if NTP not synced)
    time_t now = 0;
    time(&now);
    // If time is before 2024, NTP hasn't synced
    uint32_t ts = (now > 1704067200) ? (uint32_t)now : 0;

    File f = LittleFS.open(path, "w");
    if (!f) {
        LOGE(TAG, "Failed to open %s for writing", path);
        return 0;
    }

    // Write JSON manually for memory efficiency (series can be large)
    f.print("{\"v\":2,\"fields\":[");
    if (template_name && template_name[0]) {
        f.printf("{\"key\":\"template\",\"label\":\"Template\",\"value\":\"%s\",\"format\":\"text\"},", template_name);
    } else {
        f.print("{\"key\":\"template\",\"label\":\"Template\",\"value\":\"free_pour\",\"format\":\"text\"},");
    }
    f.printf("{\"key\":\"ts\",\"label\":\"Date\",\"value\":%lu,\"format\":\"datetime\"},", (unsigned long)ts);
    f.printf("{\"key\":\"duration\",\"label\":\"Duration\",\"value\":%lu,\"unit\":\"ms\",\"format\":\"duration\"},", (unsigned long)elapsed_ms);

    // Use integer part check for cleaner output
    f.printf("{\"key\":\"water\",\"label\":\"Water\",\"value\":%.1f,\"unit\":\"g\",\"format\":\"number\"}", final_weight);

    if (dose_weight > 0.0f) {
        f.printf(",{\"key\":\"dose\",\"label\":\"Dose\",\"value\":%.1f,\"unit\":\"g\",\"format\":\"number\"}", dose_weight);
        float ratio = final_weight / dose_weight;
        f.printf(",{\"key\":\"ratio\",\"label\":\"Ratio\",\"value\":%.1f,\"format\":\"number\"}", ratio);
    }

    // Write named captures as additional fields
    for (uint8_t i = 0; i < capture_count; i++) {
        const BrewCapture& c = captures[i];
        if (c.unit[0]) {
            f.printf(",{\"key\":\"%s\",\"label\":\"%s\",\"value\":%.1f,\"unit\":\"%s\",\"format\":\"number\"}",
                     c.key, c.label, c.value, c.unit);
        } else {
            f.printf(",{\"key\":\"%s\",\"label\":\"%s\",\"value\":%.1f,\"format\":\"number\"}",
                     c.key, c.label, c.value);
        }
    }

    f.print("],");

    // Write markers
    f.print("\"markers\":[");
    for (uint8_t i = 0; i < marker_count; i++) {
        if (i > 0) f.print(',');
        f.printf("{\"t\":%u,\"label\":\"%s\"}",
                 (unsigned)markers[i].sample_index, markers[i].label);
    }
    f.print("],");

    // Write series
    f.print("\"series\":{\"interval_ms\":1000,\"weight\":[");

    for (uint16_t i = 0; i < sample_count; i++) {
        if (i > 0) f.print(',');
        f.printf("%.1f", series[i].weight);
    }

    f.print("],\"flow\":[");

    for (uint16_t i = 0; i < sample_count; i++) {
        if (i > 0) f.print(',');
        f.printf("%.2f", series[i].flow);
    }

    f.print("]}");

    // Write template snapshot (targets, display name, description)
    if (tmpl) {
        f.print(",\"template_info\":{\"display_name\":");
        write_json_string(f, tmpl->display_name);
        if (tmpl->description[0]) {
            f.print(",\"description\":");
            write_json_string(f, tmpl->description);
        }
        // Write per-stage targets (only stages with non-zero targets)
        bool has_targets = false;
        for (uint8_t i = 0; i < tmpl->stage_count; i++) {
            const BrewStage& st = tmpl->stages[i];
            if (st.target_weight > 0 || st.target_flow_rate > 0 || st.auto_time_ms > 0) {
                has_targets = true;
                break;
            }
        }
        if (has_targets) {
            f.print(",\"targets\":{");
            bool first = true;
            for (uint8_t i = 0; i < tmpl->stage_count; i++) {
                const BrewStage& st = tmpl->stages[i];
                if (st.target_weight <= 0 && st.target_flow_rate <= 0 && st.auto_time_ms == 0) continue;
                if (!first) f.print(',');
                first = false;
                write_json_string(f, st.name);
                f.print(":{");
                bool first_f = true;
                if (st.target_weight > 0) {
                    f.printf("\"weight\":%.1f", st.target_weight);
                    first_f = false;
                }
                if (st.target_flow_rate > 0) {
                    if (!first_f) f.print(',');
                    f.printf("\"flow_rate\":%.1f", st.target_flow_rate);
                    first_f = false;
                }
                if (st.auto_time_ms > 0) {
                    if (!first_f) f.print(',');
                    f.printf("\"time_s\":%u", (unsigned)(st.auto_time_ms / 1000));
                    first_f = false;
                }
                if (st.capture_key[0]) {
                    if (!first_f) f.print(',');
                    f.print("\"capture_key\":");
                    write_json_string(f, st.capture_key);
                }
                f.print('}');
            }
            f.print('}');  // close targets
        }

        // Write field_targets: flat map from field key → target weight.
        // Makes it trivial for the UI to show "actual vs target" per field.
        {
            f.print(",\"field_targets\":{");
            bool ft_first = true;
            float water_target = 0;
            for (uint8_t i = 0; i < tmpl->stage_count; i++) {
                const BrewStage& st = tmpl->stages[i];
                if (st.target_weight <= 0) continue;
                // Dose stage (legacy EFFECT_CAPTURE_DOSE)
                if (st.on_exit & EFFECT_CAPTURE_DOSE) {
                    if (!ft_first) f.print(',');
                    ft_first = false;
                    f.printf("\"dose\":%.1f", st.target_weight);
                }
                // Named capture stages (EFFECT_CAPTURE_WEIGHT with capture_key)
                if ((st.on_exit & EFFECT_CAPTURE_WEIGHT) && st.capture_key[0]) {
                    if (!ft_first) f.print(',');
                    ft_first = false;
                    write_json_string(f, st.capture_key);
                    f.printf(":%.1f", st.target_weight);
                }
                // Track highest weight target as the total water target
                if (st.target_weight > water_target) water_target = st.target_weight;
            }
            if (water_target > 0) {
                if (!ft_first) f.print(',');
                f.printf("\"water\":%.1f", water_target);
            }
            f.print('}');
        }

        f.print('}');  // close template_info
    }

    f.print("}");
    f.close();

    // Advance NVS counter
    set_next_id(id + 1);

    // Update fs health
    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());

    LOGI(TAG, "Saved brew %u [%s]: %.1fg in %lums, %u samples",
         (unsigned)id, template_name ? template_name : "free_pour",
         final_weight, (unsigned long)elapsed_ms,
         (unsigned)sample_count);

    return id;
}

// ============================================================================
// Count
// ============================================================================

uint16_t brew_log_count() {
    File dir = LittleFS.open(BREW_LOG_DIR);
    if (!dir || !dir.isDirectory()) return 0;

    uint16_t count = 0;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) count++;
        f = dir.openNextFile();
    }
    return count;
}

// ============================================================================
// Import raw JSON
// ============================================================================

uint16_t brew_log_import_raw(const char* json, size_t json_len) {
    if (brew_log_count() >= BREW_LOG_MAX_BREWS) return 0;

    uint16_t id = next_id();
    char path[32];
    brew_log_path(id, path, sizeof(path));

    File f = LittleFS.open(path, "w");
    if (!f) {
        LOGE(TAG, "Import: failed to open %s", path);
        return 0;
    }
    f.write((const uint8_t*)json, json_len);
    f.close();

    set_next_id(id + 1);
    LOGI(TAG, "Imported brew %u (%u bytes)", (unsigned)id, (unsigned)json_len);
    return id;
}

#endif // HAS_SCALE
