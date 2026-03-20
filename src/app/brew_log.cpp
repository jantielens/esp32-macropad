#include "brew_log.h"

#if HAS_SENSOR_HX711

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
static float s_last_peak_flow = 0.0f;

// ============================================================================
// Helpers
// ============================================================================

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
                       const char* template_name, float dose_weight,
                       const BrewSample* series, uint16_t sample_count) {
    // Evict if at capacity
    if (brew_log_count() >= BREW_LOG_MAX_BREWS) {
        evict_oldest();
    }

    uint16_t id = next_id();
    char path[32];
    brew_log_path(id, path, sizeof(path));

    // Compute peak_flow and avg_flow from series.
    // Threshold filters out scale noise (±0.1–0.2 g/s jitter at rest).
    static constexpr float kFlowThreshold = 0.3f;  // g/s
    float peak_flow = 0.0f;
    float flow_sum = 0.0f;
    uint16_t flow_count = 0;
    for (uint16_t i = 0; i < sample_count; i++) {
        float f = series[i].flow;
        if (f > peak_flow) peak_flow = f;
        if (f > kFlowThreshold) {
            flow_sum += f;
            flow_count++;
        }
    }
    float avg_flow = (flow_count > 0) ? (flow_sum / flow_count) : 0.0f;

    s_last_peak_flow = peak_flow;

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
    f.print("{\"v\":1,\"fields\":[");
    if (template_name && template_name[0]) {
        f.printf("{\"key\":\"template\",\"label\":\"Template\",\"value\":\"%s\",\"format\":\"text\"},", template_name);
    } else {
        f.print("{\"key\":\"template\",\"label\":\"Template\",\"value\":\"free_pour\",\"format\":\"text\"},");
    }
    f.printf("{\"key\":\"ts\",\"label\":\"Date\",\"value\":%lu,\"format\":\"datetime\"},", (unsigned long)ts);
    f.printf("{\"key\":\"duration\",\"label\":\"Duration\",\"value\":%lu,\"unit\":\"ms\",\"format\":\"duration\"},", (unsigned long)elapsed_ms);

    // Use integer part check for cleaner output
    f.printf("{\"key\":\"water\",\"label\":\"Water\",\"value\":%.1f,\"unit\":\"g\",\"format\":\"number\"},", final_weight);
    f.printf("{\"key\":\"peak_flow\",\"label\":\"Peak Flow\",\"value\":%.2f,\"unit\":\"g/s\",\"format\":\"number\"},", peak_flow);
    f.printf("{\"key\":\"avg_flow\",\"label\":\"Avg Flow\",\"value\":%.2f,\"unit\":\"g/s\",\"format\":\"number\"}", avg_flow);

    if (dose_weight > 0.0f) {
        f.printf(",{\"key\":\"dose\",\"label\":\"Dose\",\"value\":%.1f,\"unit\":\"g\",\"format\":\"number\"}", dose_weight);
        float ratio = final_weight / dose_weight;
        f.printf(",{\"key\":\"ratio\",\"label\":\"Ratio\",\"value\":%.1f,\"format\":\"number\"}", ratio);
    }

    f.print("],\"series\":{\"interval_ms\":1000,\"weight\":[");

    for (uint16_t i = 0; i < sample_count; i++) {
        if (i > 0) f.print(',');
        f.printf("%.1f", series[i].weight);
    }

    f.print("],\"flow\":[");

    for (uint16_t i = 0; i < sample_count; i++) {
        if (i > 0) f.print(',');
        f.printf("%.2f", series[i].flow);
    }

    f.print("]}}");
    f.close();

    // Advance NVS counter
    set_next_id(id + 1);

    // Update fs health
    fs_health_set_storage_usage(LittleFS.usedBytes(), LittleFS.totalBytes());

    LOGI(TAG, "Saved brew %u [%s]: %.1fg in %lums, %u samples, peak=%.2f avg=%.2f g/s",
         (unsigned)id, template_name ? template_name : "free_pour",
         final_weight, (unsigned long)elapsed_ms,
         (unsigned)sample_count, peak_flow, avg_flow);

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
// Last peak flow
// ============================================================================

float brew_log_last_peak_flow() {
    return s_last_peak_flow;
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

#endif // HAS_SENSOR_HX711
