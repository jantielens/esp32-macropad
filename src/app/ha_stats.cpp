#include "ha_stats.h"

#if HAS_HA_HISTORY

#include "ha_stats_resample.h"
#include "config_manager.h"     // DeviceConfig, CONFIG_HA_*_MAX_LEN
#include "log_manager.h"
#include "net_activity.h"
#include "psram_json_allocator.h"
#include "rtos_task_utils.h"
#include "web_portal_state.h"   // web_portal_get_current_config()

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define TAG "HAStats"

// Recorder queries can be slow on large databases, and this runs off the LVGL
// task, so the timeout is generous compared to ha_service's loop()-bound calls.
#define HA_STATS_HTTP_TIMEOUT_MS 15000

// Stack for the fetch task: HTTPClient + TLS + JSON parsing. Lives in PSRAM.
#define HA_STATS_TASK_STACK_BYTES 16384

// Recorder keeps 5-minute statistics for a limited retention window and rolls
// everything older up to hourly, so long windows must switch period.
#define HA_STATS_HOURLY_SLOT_THRESHOLD_SECS 3600
#define HA_STATS_HOURLY_WINDOW_THRESHOLD_SECS 86400

// Upper bound on periods in one response: 24 h at 5-minute resolution is 288;
// longer supported windows use hourly statistics and need at most 168 periods.
#define HA_STATS_MAX_POINTS 288
#define HA_STATS_RESULT_MAX_SLOTS 1024

// PSRAM pool for the filtered response. Only the two fields we keep survive
// the filter, so this comfortably covers HA_STATS_MAX_POINTS periods.
#define HA_STATS_JSON_CAPACITY (48 * 1024)

// After a failed fetch, all hydration pauses before the next attempt. The first
// request often races WiFi/DNS coming up at boot, so start short and double on
// each consecutive failure — a genuinely unreachable HA still settles at the
// cap instead of retrying in a tight loop.
#define HA_STATS_RETRY_BASE_MS 2000
#define HA_STATS_RETRY_MAX_MS  60000

enum : uint8_t {
    ST_IDLE = 0,      // No work; accepts a request
    ST_QUEUED,        // Request handed to the task
    ST_FETCHING,      // HTTP in progress
    ST_READY,         // Result waiting for the LVGL task
};

struct HaStatsJob {
    data_stream_handle_t handle;
    uint32_t uid;
    char     entity_id[DATA_STREAM_HA_ENTITY_MAX_LEN];
    uint8_t  statistic;
    uint32_t slot_ms;
    uint16_t slot_count;
    uint64_t end_bucket;
    uint32_t queued_ms;   // millis() when the job was accepted (timing only)
};

static portMUX_TYPE   g_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t g_state = ST_IDLE;
static HaStatsJob     g_job;
static float*         g_values = nullptr;   // [slot_count] resampled result
static uint16_t       g_value_count = 0;
static SemaphoreHandle_t g_wake = nullptr;
static TaskHandle_t   g_task = nullptr;
static uint32_t       g_blocked_until_ms = 0;
static uint32_t       g_retry_delay_ms = HA_STATS_RETRY_BASE_MS;

// ============================================================================
// Helpers
// ============================================================================

static const char* stat_field(uint8_t statistic) {
    switch (statistic) {
        case HA_STAT_STATE: return "state";
        case HA_STAT_SUM:   return "sum";
        default:            return "mean";
    }
}

// Format a unix timestamp as UTC ISO 8601, which is what the Recorder service
// expects for start_time / end_time.
static void format_utc(uint64_t unix_sec, char* out, size_t out_sz) {
    time_t t = (time_t)unix_sec;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    strftime(out, out_sz, "%Y-%m-%dT%H:%M:%S+00:00", &tm_utc);
}

// Read a period start that HA may serialize either as an ISO 8601 string or as
// an epoch-milliseconds number.
static uint64_t parse_start(JsonVariantConst v) {
    if (v.is<const char*>()) return ha_stats_parse_iso8601(v.as<const char*>());
    if (v.is<double>())      return (uint64_t)(v.as<double>() / 1000.0);
    return 0;
}

// ============================================================================
// Fetch (runs on the ha_stats task)
// ============================================================================

// Fetch and resample one job into `out` (slot_count entries). Returns the
// number of buckets that received a value, or -1 on failure.
static int fetch_job(const HaStatsJob& job, float* out) {
    const uint32_t t_start = millis();
    const DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg || !cfg->ha_url[0] || !cfg->ha_token[0]) {
        // Filtered out in ha_stats_request(); only reachable if the config was
        // cleared between queueing and the fetch.
        LOGW(TAG, "HA URL/token not configured — skipping hydration");
        return -1;
    }
    if (WiFi.status() != WL_CONNECTED) return -1;

    uint64_t start_sec, end_sec;
    ha_stats_request_window(job.slot_ms, job.slot_count, job.end_bucket,
                            &start_sec, &end_sec);
    const uint64_t window_ms = (uint64_t)job.slot_ms * job.slot_count;
    const bool hourly = job.slot_ms >= HA_STATS_HOURLY_SLOT_THRESHOLD_SECS * 1000ULL ||
                        window_ms > HA_STATS_HOURLY_WINDOW_THRESHOLD_SECS * 1000ULL;

    char start_iso[32], end_iso[32];
    format_utc(start_sec, start_iso, sizeof(start_iso));
    format_utc(end_sec, end_iso, sizeof(end_iso));

    const char* field = stat_field(job.statistic);

    char url[CONFIG_HA_URL_MAX_LEN + 48];
    snprintf(url, sizeof(url), "%s/api/services/recorder/get_statistics?return_response",
             cfg->ha_url);

    char body[DATA_STREAM_HA_ENTITY_MAX_LEN + 192];
    snprintf(body, sizeof(body),
             "{\"start_time\":\"%s\",\"end_time\":\"%s\",\"statistic_ids\":[\"%s\"],"
             "\"period\":\"%s\",\"types\":[\"%s\"]}",
             start_iso, end_iso, job.entity_id, hourly ? "hour" : "5minute", field);

    HTTPClient http;
    http.setTimeout(HA_STATS_HTTP_TIMEOUT_MS);

    WiFiClientSecure tls_client;
    WiFiClient plain_client;
    const bool is_https = strncmp(cfg->ha_url, "https://", 8) == 0;
    bool began;
    if (is_https) {
        tls_client.setInsecure();  // HA installs often use self-signed certs
        tls_client.setTimeout(HA_STATS_HTTP_TIMEOUT_MS);
        began = http.begin(tls_client, url);
    } else {
        plain_client.setTimeout(HA_STATS_HTTP_TIMEOUT_MS);
        began = http.begin(plain_client, url);
    }
    if (!began) {
        LOGW(TAG, "HTTP begin failed: %s", url);
        return -1;
    }

    char auth[CONFIG_HA_TOKEN_MAX_LEN + 12];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg->ha_token);
    http.addHeader("Authorization", auth);
    http.addHeader("Content-Type", "application/json");

    const int code = http.POST((uint8_t*)body, strlen(body));
    net_activity_mark(NET_CH_HTTP);
    const uint32_t t_post = millis();
    if (code < 200 || code >= 300) {
        LOGW(TAG, "%s: HTTP %d", job.entity_id, code);
        http.end();
        return -1;
    }

    // Keep only the two fields we need so the response never has to be held in
    // memory in full — the rest is discarded as the body streams in. The service
    // response nests the per-entity arrays under a "statistics" object:
    //   {"changed_states":[],"service_response":{"statistics":{"<id>":[ ... ]}}}
    char filter_json[DATA_STREAM_HA_ENTITY_MAX_LEN + 96];
    snprintf(filter_json, sizeof(filter_json),
             "{\"service_response\":{\"statistics\":{\"%s\":[{\"start\":true,\"%s\":true}]}}}",
             job.entity_id, field);

    BasicJsonDocument<PsramJsonAllocator> filter(512);
    if (deserializeJson(filter, filter_json) != DeserializationError::Ok) {
        LOGE(TAG, "filter build failed");
        http.end();
        return -1;
    }

    BasicJsonDocument<PsramJsonAllocator> doc(HA_STATS_JSON_CAPACITY);
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    const uint32_t t_parse = millis();
    if (err) {
        LOGW(TAG, "%s: parse failed (%s)", job.entity_id, err.c_str());
        return -1;
    }

    JsonArrayConst periods =
        doc["service_response"]["statistics"][job.entity_id].as<JsonArrayConst>();
    if (periods.isNull() || periods.size() == 0) {
        LOGI(TAG, "%s: no statistics in window", job.entity_id);
        return -1;
    }

    HaStatPoint* points = (HaStatPoint*)heap_caps_malloc(
        sizeof(HaStatPoint) * HA_STATS_MAX_POINTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!points) points = (HaStatPoint*)malloc(sizeof(HaStatPoint) * HA_STATS_MAX_POINTS);
    if (!points) {
        LOGE(TAG, "OOM for %d statistic points", HA_STATS_MAX_POINTS);
        return -1;
    }

    size_t n = 0;
    for (JsonObjectConst p : periods) {
        if (n >= HA_STATS_MAX_POINTS) break;
        const uint64_t start = parse_start(p["start"]);
        if (start == 0) continue;
        JsonVariantConst val = p[field];
        if (!val.is<double>()) continue;
        points[n].start_sec = start;
        points[n].value = (float)val.as<double>();
        n++;
    }

    const size_t filled = ha_stats_resample(points, n, job.slot_ms, job.end_bucket,
                                            out, job.slot_count);
    free(points);
    const uint32_t t_done = millis();

    LOGI(TAG, "%s: %u periods -> %u/%u slots (%s)", job.entity_id,
         (unsigned)n, (unsigned)filled, job.slot_count, hourly ? "hour" : "5minute");
    // Phase breakdown so a slow hydration can be attributed to the right stage:
    // queue = wait behind other streams, post = TLS handshake + HA query time,
    // parse = streaming JSON decode, resample = bucket mapping on this task.
    LOGD(TAG, "%s: timing queue %lums, post %lums, parse %lums, resample %lums, total %lums",
         job.entity_id,
         (unsigned long)(t_start - job.queued_ms),
         (unsigned long)(t_post - t_start),
         (unsigned long)(t_parse - t_post),
         (unsigned long)(t_done - t_parse),
         (unsigned long)(t_done - job.queued_ms));
    return (int)filled;
}

static void ha_stats_task(void*) {
    for (;;) {
        xSemaphoreTake(g_wake, portMAX_DELAY);

        HaStatsJob job;
        portENTER_CRITICAL(&g_mux);
        const bool have_job = (g_state == ST_QUEUED);
        if (have_job) {
            job = g_job;
            g_state = ST_FETCHING;
        }
        portEXIT_CRITICAL(&g_mux);
        if (!have_job) continue;

        const int filled = fetch_job(job, g_values);

        portENTER_CRITICAL(&g_mux);
        uint32_t paused_ms = 0;
        if (filled > 0) {
            g_value_count = job.slot_count;
            g_retry_delay_ms = HA_STATS_RETRY_BASE_MS;
            g_state = ST_READY;
        } else {
            g_value_count = 0;
            paused_ms = g_retry_delay_ms;
            g_blocked_until_ms = millis() + paused_ms;
            g_retry_delay_ms = (g_retry_delay_ms >= HA_STATS_RETRY_MAX_MS / 2)
                                   ? (uint32_t)HA_STATS_RETRY_MAX_MS
                                   : (uint32_t)(g_retry_delay_ms * 2);
            g_state = ST_IDLE;
        }
        portEXIT_CRITICAL(&g_mux);

        if (filled <= 0) {
            // One global backoff: a failing HA install would otherwise have
            // every stream retry in a tight loop.
            LOGW(TAG, "%s: fetch failed — all hydration paused for %lums",
                 job.entity_id, (unsigned long)paused_ms);
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void ha_stats_init() {
    if (g_task) return;

    g_values = (float*)heap_caps_malloc(sizeof(float) * HA_STATS_RESULT_MAX_SLOTS,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_values) g_values = (float*)malloc(sizeof(float) * HA_STATS_RESULT_MAX_SLOTS);
    if (!g_values) {
        LOGE(TAG, "OOM for result buffer — history hydration disabled");
        return;
    }

    g_wake = xSemaphoreCreateBinary();
    if (!g_wake) {
        LOGE(TAG, "semaphore creation failed — history hydration disabled");
        free(g_values);
        g_values = nullptr;
        return;
    }

    RtosTaskPsramAlloc alloc;
    if (!rtos_create_task_psram_stack_pinned(ha_stats_task, "ha_stats",
                                             HA_STATS_TASK_STACK_BYTES, nullptr,
                                             2, &g_task, &alloc, 0)) {
        LOGE(TAG, "task creation failed — history hydration disabled");
        vSemaphoreDelete(g_wake);
        g_wake = nullptr;
        free(g_values);
        g_values = nullptr;
        return;
    }
    LOGI(TAG, "History hydration ready");
}

bool ha_stats_busy() {
    return g_state != ST_IDLE;
}

uint32_t ha_stats_backoff_remaining_ms() {
    const int32_t left = (int32_t)(g_blocked_until_ms - millis());
    return (left > 0) ? (uint32_t)left : 0;
}

bool ha_stats_request(data_stream_handle_t handle, uint32_t uid,
                      const char* entity_id, uint8_t statistic,
                      uint32_t slot_ms, uint16_t slot_count,
                      uint64_t end_bucket) {
    if (!g_task || !g_values) return false;
    if (!entity_id || !entity_id[0] || slot_count == 0 || slot_ms == 0) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    // The portal owns the live DeviceConfig and publishes it only once
    // web_portal_init() has run, which happens well after the streams exist.
    // Not being ready yet is not a failure — refuse the job here so the task
    // never runs and no retry backoff is charged for it.
    const DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg || !cfg->ha_url[0] || !cfg->ha_token[0]) return false;

    portENTER_CRITICAL(&g_mux);
    const bool accept = (g_state == ST_IDLE) &&
                        (int32_t)(millis() - g_blocked_until_ms) >= 0;
    if (accept) {
        g_job.handle = handle;
        g_job.uid = uid;
        strlcpy(g_job.entity_id, entity_id, sizeof(g_job.entity_id));
        g_job.statistic = statistic;
        g_job.slot_ms = slot_ms;
        g_job.slot_count = slot_count;
        g_job.end_bucket = end_bucket;
        g_job.queued_ms = millis();
        g_state = ST_QUEUED;
    }
    portEXIT_CRITICAL(&g_mux);

    if (accept) {
        xSemaphoreGive(g_wake);
        LOGD(TAG, "Queued %s (stream %d, %u slots x %lums)", entity_id,
             handle, slot_count, (unsigned long)slot_ms);
    }
    return accept;
}

bool ha_stats_deliver() {
    if (g_state != ST_READY) return false;

    // The task is idle while ST_READY, so g_job / g_values are stable here.
    data_stream_apply_history(g_job.handle, g_job.uid, g_job.end_bucket,
                              g_values, g_value_count);

    portENTER_CRITICAL(&g_mux);
    g_value_count = 0;
    g_state = ST_IDLE;
    portEXIT_CRITICAL(&g_mux);
    return true;
}

#endif // HAS_HA_HISTORY
