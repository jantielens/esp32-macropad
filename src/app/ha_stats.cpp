#include "ha_stats.h"

#if HAS_HA_HISTORY

#include "ha_stats_resample.h"
#include "ha_history_decoder.h"
#include "bounded_psram_json_allocator.h"
#include "config_manager.h"     // DeviceConfig, CONFIG_HA_*_MAX_LEN
#include "log_manager.h"
#include "net_activity.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"
#include "web_portal_state.h"   // web_portal_get_current_config()

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <math.h>
#include <new>
#include <string.h>
#include <time.h>

#define TAG "HAStats"

#define HA_HISTORY_JOB_DEADLINE_MS 30000
#define HA_HISTORY_CONNECT_TIMEOUT_MS 4000
#define HA_HISTORY_IDLE_TIMEOUT_MS 2000
#define HA_HISTORY_RECORD_DOC_BYTES (8 * 1024)

// Stack for the fetch task: HTTPClient + TLS + JSON parsing. Lives in PSRAM.
#define HA_STATS_TASK_STACK_BYTES 16384

// Must stay at least as large as every caller's per-widget slot cap. Requests
// are rejected at this module boundary before they can reach g_values.
#define HA_STATS_RESULT_MAX_SLOTS 1024

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
    ST_DELIVERING,    // LVGL task owns the snapshotted result
};

struct HaStatsJob {
    data_stream_handle_t handle;
    uint32_t uid;
    char     entity_id[DATA_STREAM_HA_ENTITY_MAX_LEN];
    char     ha_url[CONFIG_HA_URL_MAX_LEN];
    char     ha_token[CONFIG_HA_TOKEN_MAX_LEN];
    uint32_t config_revision;
    uint32_t ota_epoch;
    uint32_t slot_ms;
    uint16_t slot_count;
    uint16_t raw_hours;
    uint64_t end_bucket;
    uint32_t queued_ms;   // millis() when the job was accepted (timing only)
};

using FetchOutcome = HaHistoryFetchOutcome;

static portMUX_TYPE   g_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t g_state = ST_IDLE;
static HaStatsJob     g_job;
static float*         g_values = nullptr;   // [slot_count] resampled result
static float*         g_stats_values = nullptr;  // [slot_count] older-statistics scratch
static HaStatPoint*   g_points = nullptr;   // [HA_STATISTICS_MAX_POINTS]
static BoundedPsramJsonAllocator g_stats_allocator(HA_STATISTICS_JSON_BYTES);
static JsonDocument*  g_doc = nullptr;
static uint16_t       g_value_count = 0;
static FetchOutcome   g_result_outcome = FetchOutcome::RetryableFailure;
static std::atomic<uint32_t> g_config_revision{0};
static SemaphoreHandle_t g_wake = nullptr;
static TaskHandle_t   g_task = nullptr;
static std::atomic<TaskHandle_t> ha_stats_sample_task{nullptr};
static uint32_t       g_blocked_until_ms = 0;
static uint32_t       g_retry_delay_ms = HA_STATS_RETRY_BASE_MS;

// ============================================================================
// Helpers
// ============================================================================

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

enum class Checkpoint : uint8_t { Continue, Cancelled, Deadline, NetworkLost };
using SourceOutcome = HaHistorySourceOutcome;

static Checkpoint check_job(const HaStatsJob& job, uint32_t started_ms) {
    if (ota_activity_is_active() || ota_activity_epoch() != job.ota_epoch ||
        g_config_revision.load(std::memory_order_acquire) != job.config_revision)
        return Checkpoint::Cancelled;
    if (uint32_t(millis() - started_ms) >= HA_HISTORY_JOB_DEADLINE_MS)
        return Checkpoint::Deadline;
    if (WiFi.status() != WL_CONNECTED) return Checkpoint::NetworkLost;
    return Checkpoint::Continue;
}

static bool percent_encode(const char* source, char* output, size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    if (!source || !output || !capacity) return false;
    size_t used = 0;
    for (const uint8_t* p = (const uint8_t*)source; *p; p++) {
        const uint8_t c = *p;
        const bool unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                                c == '_' || c == '~';
        const size_t needed = unreserved ? 1 : 3;
        if (needed >= capacity - used) return false;
        if (unreserved) {
            output[used++] = (char)c;
        } else {
            output[used++] = '%';
            output[used++] = hex[c >> 4];
            output[used++] = hex[c & 0x0F];
        }
    }
    output[used] = '\0';
    return true;
}

static bool build_history_url(const HaStatsJob& job, uint64_t start_sec,
                              uint64_t end_sec, char* output, size_t capacity) {
    char start_iso[32], end_iso[32];
    char start_encoded[96], end_encoded[96];
    char entity_encoded[DATA_STREAM_HA_ENTITY_MAX_LEN * 3];
    format_utc(start_sec, start_iso, sizeof(start_iso));
    format_utc(end_sec, end_iso, sizeof(end_iso));
    if (!percent_encode(start_iso, start_encoded, sizeof(start_encoded)) ||
        !percent_encode(end_iso, end_encoded, sizeof(end_encoded)) ||
        !percent_encode(job.entity_id, entity_encoded, sizeof(entity_encoded)))
        return false;
    const size_t base_len = strlen(job.ha_url);
    const char* separator = base_len && job.ha_url[base_len - 1] == '/' ? "" : "/";
    const int length = snprintf(
        output, capacity,
        "%s%sapi/history/period/%s?filter_entity_id=%s&end_time=%s"
        "&minimal_response&no_attributes&significant_changes_only=0",
        job.ha_url, separator, start_encoded, entity_encoded, end_encoded);
    return length >= 0 && (size_t)length < capacity;
}

static bool build_statistics_url(const HaStatsJob& job, char* output, size_t capacity) {
    const size_t base_len = strlen(job.ha_url);
    const char* separator = base_len && job.ha_url[base_len - 1] == '/' ? "" : "/";
    const int length = snprintf(output, capacity, "%s%sapi/services/recorder/"
                                "get_statistics?return_response",
                                job.ha_url, separator);
    return length >= 0 && (size_t)length < capacity;
}

static bool begin_http(HTTPClient& http, WiFiClient& plain_client,
                       WiFiClientSecure& tls_client, const HaStatsJob& job,
                       const char* url) {
    http.setConnectTimeout(HA_HISTORY_CONNECT_TIMEOUT_MS);
    plain_client.setTimeout(HA_HISTORY_IDLE_TIMEOUT_MS);
    tls_client.setTimeout(HA_HISTORY_IDLE_TIMEOUT_MS);
    const bool is_https = strncmp(job.ha_url, "https://", 8) == 0;
    if (is_https) {
        tls_client.setInsecure();
        if (!http.begin(tls_client, url)) return false;
    } else if (!http.begin(plain_client, url)) {
        return false;
    }
    http.setTimeout(HA_HISTORY_IDLE_TIMEOUT_MS);
    char auth[CONFIG_HA_TOKEN_MAX_LEN + 8];
    const int length = snprintf(auth, sizeof(auth), "Bearer %s", job.ha_token);
    if (length < 0 || (size_t)length >= sizeof(auth)) {
        http.end();
        return false;
    }
    http.addHeader("Authorization", auth);
    return true;
}

struct RawHistoryContext {
    const HaStatsJob* job;
    HaRawHistorySampler* sampler;
    JsonDocument* document;
    uint32_t started_ms;
    uint32_t last_checkpoint_ms;
    uint32_t records_since_yield;
    SourceOutcome outcome;
};

static bool parse_history_record(const char* json, size_t size, void* context) {
    auto& raw = *static_cast<RawHistoryContext*>(context);
    if (++raw.records_since_yield >= 64 ||
        uint32_t(millis() - raw.last_checkpoint_ms) >= 10) {
        const Checkpoint checkpoint = check_job(*raw.job, raw.started_ms);
        if (checkpoint != Checkpoint::Continue) {
            raw.outcome = checkpoint == Checkpoint::Cancelled
                              ? SourceOutcome::Cancelled : SourceOutcome::Retryable;
            return false;
        }
        taskYIELD();
        raw.records_since_yield = 0;
        raw.last_checkpoint_ms = millis();
    }

    uint64_t timestamp_ms = 0;
    float value = NAN;
    if (!ha_history_parse_record(json, size, *raw.document, &timestamp_ms, &value) ||
        !ha_raw_history_sampler_add(raw.sampler, timestamp_ms, value)) {
        raw.outcome = SourceOutcome::Retryable;
        return false;
    }
    return true;
}

class HistoryBodySink : public Stream {
public:
    HistoryBodySink(HaHistoryDecoder& decoder, RawHistoryContext& context)
        : decoder_(decoder), context_(context) {}

    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t* data, size_t size) override {
        const Checkpoint checkpoint = check_job(*context_.job, context_.started_ms);
        if (checkpoint != Checkpoint::Continue) {
            context_.outcome = checkpoint == Checkpoint::Cancelled
                                   ? SourceOutcome::Cancelled : SourceOutcome::Retryable;
            return 0;
        }
        if (!decoder_.feed(data, size, parse_history_record, &context_)) {
            decoder_failed_ = true;
            return 0;
        }
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    bool decoder_failed() const { return decoder_failed_; }

private:
    HaHistoryDecoder& decoder_;
    RawHistoryContext& context_;
    bool decoder_failed_ = false;
};

struct HistoryReadStream : public Stream {
    HistoryReadStream(Stream& source, const HaStatsJob& job, uint32_t started_ms)
        : source_(source), job_(job), started_ms_(started_ms), last_checkpoint_ms_(millis()) {}

    int available() override {
        if (!checkpoint_if_due()) return 0;
        return source_.available();
    }
    int read() override {
        if (!checkpoint_if_due()) return -1;
        if (bytes_read_ >= HaHistoryDecoder::MAX_BODY_BYTES) {
            outcome_ = SourceOutcome::Permanent;
            return -1;
        }
        const int value = source_.read();
        if (value >= 0) bytes_read_++;
        return value;
    }
    int peek() override {
        if (!checkpoint_if_due()) return -1;
        return source_.peek();
    }
    void flush() override {}
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }
    SourceOutcome outcome() const { return outcome_; }
    size_t bytes_read() const { return bytes_read_; }

private:
    bool checkpoint_if_due() {
        if (outcome_ != SourceOutcome::Success) return false;
        if ((bytes_read_ % 64 != 0 || bytes_read_ == 0) &&
            uint32_t(millis() - last_checkpoint_ms_) < 10) return true;
        const Checkpoint checkpoint = check_job(job_, started_ms_);
        if (checkpoint != Checkpoint::Continue) {
            outcome_ = checkpoint == Checkpoint::Cancelled
                           ? SourceOutcome::Cancelled : SourceOutcome::Retryable;
            return false;
        }
        taskYIELD();
        last_checkpoint_ms_ = millis();
        return true;
    }

    Stream& source_;
    const HaStatsJob& job_;
    uint32_t started_ms_;
    uint32_t last_checkpoint_ms_;
    size_t bytes_read_ = 0;
    SourceOutcome outcome_ = SourceOutcome::Success;
};

static SourceOutcome fetch_raw_history(const HaStatsJob& job, uint32_t started_ms,
                                       uint64_t start_sec, uint64_t end_sec,
                                       float* values, uint16_t* coverage_start) {
    char url[CONFIG_HA_URL_MAX_LEN + DATA_STREAM_HA_ENTITY_MAX_LEN * 3 + 256];
    if (!build_history_url(job, start_sec, end_sec, url, sizeof(url)))
        return SourceOutcome::Permanent;
    const Checkpoint initial_checkpoint = check_job(job, started_ms);
    if (initial_checkpoint != Checkpoint::Continue)
        return initial_checkpoint == Checkpoint::Cancelled
                   ? SourceOutcome::Cancelled : SourceOutcome::Retryable;

    HaRawHistorySampler sampler;
    const uint64_t end_ms = (job.end_bucket + 1) * (uint64_t)job.slot_ms;
    const uint64_t start_ms = end_ms - (uint64_t)job.slot_count * job.slot_ms;
    if (!ha_raw_history_sampler_init(&sampler, values, job.slot_count, job.slot_ms,
                                     start_ms, end_ms,
                                     ha_history_raw_start_ms(job.slot_ms, job.slot_count,
                                                             job.end_bucket, job.raw_hours)))
        return SourceOutcome::Permanent;
    BoundedPsramJsonAllocator allocator(HA_HISTORY_RECORD_DOC_BYTES);
    JsonDocument document(&allocator);
    RawHistoryContext context{&job, &sampler, &document, started_ms, millis(), 0,
                              SourceOutcome::Success};
    HaHistoryDecoder decoder;
    HistoryBodySink sink(decoder, context);

    HTTPClient http;
    WiFiClientSecure tls_client;
    WiFiClient plain_client;
    if (!begin_http(http, plain_client, tls_client, job, url))
        return SourceOutcome::Retryable;
    const Checkpoint before_request = check_job(job, started_ms);
    if (before_request != Checkpoint::Continue) {
        http.end();
        return before_request == Checkpoint::Cancelled
                   ? SourceOutcome::Cancelled : SourceOutcome::Retryable;
    }
    const int code = http.GET();
    net_activity_mark(NET_CH_HTTP);
    if (code != 200) {
        LOGW(TAG, "%s: raw history HTTP %d", job.entity_id, code);
        http.end();
        if (code == 401 || code == 403) return SourceOutcome::AuthFailure;
        if (code == 404 || (code >= 400 && code < 500))
            return SourceOutcome::Permanent;
        return SourceOutcome::Retryable;
    }
    const int expected_size = http.getSize();
    if (expected_size > (int)HaHistoryDecoder::MAX_BODY_BYTES) {
        http.end();
        return SourceOutcome::Permanent;
    }
    const int received = http.writeToStream(&sink);
    http.end();
    if (sink.decoder_failed()) {
        if (context.outcome != SourceOutcome::Success) return context.outcome;
        LOGW(TAG, "%s: raw history rejected (decoder error %u, %u bytes, %u records)",
             job.entity_id, (unsigned)decoder.error(),
             (unsigned)decoder.bytes_seen(), (unsigned)decoder.records_seen());
        if (decoder.error() == HaHistoryDecoder::Error::BodyLimit ||
            decoder.error() == HaHistoryDecoder::Error::RecordTooLarge ||
            decoder.error() == HaHistoryDecoder::Error::RecordLimit)
            return SourceOutcome::Permanent;
        return SourceOutcome::Retryable;
    }
    if (received < 0 || (expected_size >= 0 && received != expected_size) ||
        !decoder.finish())
        return SourceOutcome::Retryable;
    const uint16_t cutoff = ha_raw_history_sampler_finish(&sampler);
    if (coverage_start) *coverage_start = cutoff;
    LOGI(TAG, "%s: raw history %u records, %u bytes, peak record %u bytes",
         job.entity_id, (unsigned)decoder.records_seen(),
         (unsigned)decoder.bytes_seen(), (unsigned)decoder.peak_record_bytes());
    return SourceOutcome::Success;
}

static SourceOutcome fetch_statistics(const HaStatsJob& job, uint32_t started_ms,
                                      const HaStatisticsWindow& window,
                                      float* out, uint16_t* finite_count) {
    char url[CONFIG_HA_URL_MAX_LEN + 64];
    if (!build_statistics_url(job, url, sizeof(url))) return SourceOutcome::Permanent;
    const bool hourly = window.period_sec == 3600;
    char start_iso[32], end_iso[32];
    format_utc(window.start_sec, start_iso, sizeof(start_iso));
    format_utc(window.end_sec, end_iso, sizeof(end_iso));
    char body[DATA_STREAM_HA_ENTITY_MAX_LEN + 192];
    const int body_length = snprintf(
        body, sizeof(body),
        "{\"start_time\":\"%s\",\"end_time\":\"%s\",\"statistic_ids\":[\"%s\"],"
        "\"period\":\"%s\",\"types\":[\"mean\"]}",
        start_iso, end_iso, job.entity_id, hourly ? "hour" : "5minute");
    if (body_length < 0 || (size_t)body_length >= sizeof(body))
        return SourceOutcome::Permanent;
    const Checkpoint initial_checkpoint = check_job(job, started_ms);
    if (initial_checkpoint != Checkpoint::Continue)
        return initial_checkpoint == Checkpoint::Cancelled
                   ? SourceOutcome::Cancelled : SourceOutcome::Retryable;

    HTTPClient http;
    WiFiClientSecure tls_client;
    WiFiClient plain_client;
    if (!begin_http(http, plain_client, tls_client, job, url))
        return SourceOutcome::Retryable;
    const Checkpoint before_request = check_job(job, started_ms);
    if (before_request != Checkpoint::Continue) {
        http.end();
        return before_request == Checkpoint::Cancelled
                   ? SourceOutcome::Cancelled : SourceOutcome::Retryable;
    }
    http.addHeader("Content-Type", "application/json");
    const int code = http.POST((uint8_t*)body, body_length);
    net_activity_mark(NET_CH_HTTP);
    if (code < 200 || code >= 300) {
        http.end();
        if (code == 401 || code == 403) return SourceOutcome::AuthFailure;
        if (code >= 400 && code < 500) return SourceOutcome::Permanent;
        return SourceOutcome::Retryable;
    }

    char filter_json[DATA_STREAM_HA_ENTITY_MAX_LEN + 96];
    const int filter_length = snprintf(
        filter_json, sizeof(filter_json),
        "{\"service_response\":{\"statistics\":{\"%s\":[{\"start\":true,\"mean\":true}]}}}",
        job.entity_id);
    if (filter_length < 0 || (size_t)filter_length >= sizeof(filter_json)) {
        http.end();
        return SourceOutcome::Permanent;
    }
    StaticJsonDocument<512> filter;
    if (deserializeJson(filter, filter_json) != DeserializationError::Ok) {
        http.end();
        return SourceOutcome::Permanent;
    }
    g_doc->clear();
    HistoryReadStream checked_stream(http.getStream(), job, started_ms);
    const DeserializationError error = deserializeJson(
        *g_doc, checked_stream, DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(8));
    http.end();
    if (checked_stream.outcome() != SourceOutcome::Success)
        return checked_stream.outcome();
    if (error || g_doc->overflowed()) {
        LOGW(TAG, "%s: statistics JSON failed (%s, peak %u bytes)", job.entity_id,
             error.c_str(), (unsigned)g_stats_allocator.peak);
        return error == DeserializationError::NoMemory
                   ? SourceOutcome::Permanent : SourceOutcome::Retryable;
    }
    const Checkpoint parsed_checkpoint = check_job(job, started_ms);
    if (parsed_checkpoint == Checkpoint::Cancelled) return SourceOutcome::Cancelled;
    if (parsed_checkpoint != Checkpoint::Continue) return SourceOutcome::Retryable;

    JsonArrayConst periods =
        (*g_doc)["service_response"]["statistics"][job.entity_id].as<JsonArrayConst>();
    if (periods.isNull()) {
        // Recorder can omit an entity with no statistics altogether.
        if ((*g_doc)["service_response"]["statistics"].is<JsonObjectConst>() &&
            !(*g_doc)["service_response"]["statistics"].as<JsonObjectConst>()
                 .containsKey(job.entity_id)) {
            for (uint16_t i = 0; i < job.slot_count; i++) out[i] = NAN;
            if (finite_count) *finite_count = 0;
            return SourceOutcome::Success;
        }
        LOGW(TAG, "%s: unexpected statistics response", job.entity_id);
        return SourceOutcome::Retryable;
    }
    if (periods.size() > HA_STATISTICS_MAX_POINTS) {
        LOGW(TAG, "%s: statistics period limit exceeded (%u)", job.entity_id,
             (unsigned)periods.size());
        return SourceOutcome::Permanent;
    }
    size_t n = 0;
    for (JsonObjectConst period : periods) {
        if ((n & 63U) == 0) {
            const Checkpoint checkpoint = check_job(job, started_ms);
            if (checkpoint == Checkpoint::Cancelled) return SourceOutcome::Cancelled;
            if (checkpoint != Checkpoint::Continue) return SourceOutcome::Retryable;
            taskYIELD();
        }
        const uint64_t start = parse_start(period["start"]);
        const JsonVariantConst value = period["mean"];
        if (!start || !value.is<double>() || !isfinite(value.as<double>())) continue;
        g_points[n++] = {start, (float)value.as<double>()};
    }
    const uint64_t source_period_ms = window.period_sec * 1000ULL;
    const size_t filled = ha_stats_resample(g_points, n, job.slot_ms, source_period_ms,
                                            job.end_bucket, out, job.slot_count);
    if (finite_count) *finite_count = (uint16_t)filled;
    LOGI(TAG, "%s: %u mean periods -> %u/%u slots (%s)",
         job.entity_id, (unsigned)n, (unsigned)filled, job.slot_count,
         hourly ? "hour" : "5minute");
    return SourceOutcome::Success;
}

// ============================================================================
// Fetch (runs on the ha_stats task)
// ============================================================================

static FetchOutcome fetch_job(const HaStatsJob& job) {
    const uint32_t started_ms = millis();
    uint64_t start_sec = 0, end_sec = 0;
    const uint64_t grid_end_ms = (job.end_bucket + 1) * (uint64_t)job.slot_ms;
    start_sec = ha_history_raw_start_ms(job.slot_ms, job.slot_count,
                                        job.end_bucket, job.raw_hours) / 1000ULL;
    end_sec = grid_end_ms / 1000ULL + (grid_end_ms % 1000ULL != 0);
    for (uint16_t i = 0; i < job.slot_count; i++) g_values[i] = NAN;

    uint16_t raw_coverage_start = job.slot_count;
    const SourceOutcome raw = fetch_raw_history(job, started_ms, start_sec, end_sec,
                                                g_values, &raw_coverage_start);
    if (raw == SourceOutcome::Cancelled)
        return FetchOutcome::Cancelled;
    if (raw == SourceOutcome::AuthFailure) {
        LOGW(TAG, "%s: raw history authentication/permission refused", job.entity_id);
        return FetchOutcome::PermanentFailure;
    }

    const bool raw_succeeded = raw == SourceOutcome::Success;
    if (!raw_succeeded)
        ha_history_discard_incomplete_raw(g_values, job.slot_count);
    const bool need_statistics = !raw_succeeded || raw_coverage_start > 0;
    SourceOutcome statistics = SourceOutcome::Success;
    if (need_statistics) {
        const uint16_t limit = raw_succeeded ? raw_coverage_start : job.slot_count;
        const HaStatisticsWindow window = ha_history_statistics_window(
            job.slot_ms, job.slot_count, job.end_bucket, limit);
        if (window.end_sec > window.start_sec) {
            uint16_t stats_finite = 0;
            statistics = fetch_statistics(job, started_ms, window,
                                          g_stats_values, &stats_finite);
            if (statistics == SourceOutcome::Cancelled)
                return FetchOutcome::Cancelled;
            if (statistics == SourceOutcome::AuthFailure && !raw_succeeded) {
                LOGW(TAG, "%s: statistics authentication/permission refused", job.entity_id);
                return FetchOutcome::PermanentFailure;
            }
            if (statistics == SourceOutcome::Success) {
                ha_history_merge_older_statistics(g_values, g_stats_values,
                                                   job.slot_count, limit);
            }
        }
    }

    uint16_t finite_count = 0;
    for (uint16_t i = 0; i < job.slot_count; i++)
        if (isfinite(g_values[i])) finite_count++;

    if (ota_activity_is_active() || ota_activity_epoch() != job.ota_epoch ||
        g_config_revision.load(std::memory_order_acquire) != job.config_revision)
        return FetchOutcome::Cancelled;

    if (raw_succeeded && need_statistics && statistics != SourceOutcome::Success)
        LOGW(TAG, "%s: older mean fallback unavailable; retaining raw history",
             job.entity_id);
    if (!raw_succeeded && statistics == SourceOutcome::Success && finite_count > 0)
        LOGW(TAG, "%s: raw history unavailable; using mean-statistics fallback", job.entity_id);
    const bool has_usable_history = finite_count > 0 ||
        (raw_succeeded && raw_coverage_start < job.slot_count);
    return ha_history_fetch_outcome(raw, statistics, need_statistics, has_usable_history);
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

        const FetchOutcome result = fetch_job(job);

        portENTER_CRITICAL(&g_mux);
        uint32_t paused_ms = 0;
        if (result != FetchOutcome::RetryableFailure) {
            g_result_outcome = result;
            g_value_count = (result == FetchOutcome::Success ||
                            result == FetchOutcome::PartialRetryable) ? job.slot_count : 0;
            g_state = ST_READY;
        } else {
            g_value_count = 0;
            g_state = ST_IDLE;
        }
        if (result == FetchOutcome::RetryableFailure ||
            result == FetchOutcome::PartialRetryable) {
            paused_ms = g_retry_delay_ms;
            g_blocked_until_ms = millis() + paused_ms;
            g_retry_delay_ms = (g_retry_delay_ms >= HA_STATS_RETRY_MAX_MS / 2)
                                   ? (uint32_t)HA_STATS_RETRY_MAX_MS
                                   : (uint32_t)(g_retry_delay_ms * 2);
        } else if (result == FetchOutcome::Success ||
                   result == FetchOutcome::PermanentFailure ||
                   result == FetchOutcome::Cancelled) {
            g_retry_delay_ms = HA_STATS_RETRY_BASE_MS;
        }
        portEXIT_CRITICAL(&g_mux);

        if (result == FetchOutcome::RetryableFailure ||
            result == FetchOutcome::PartialRetryable) {
            // One global backoff: a failing HA install would otherwise have
            // every stream retry in a tight loop.
            LOGW(TAG, "%s: history incomplete; retry backoff %lums",
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
    g_stats_values = (float*)heap_caps_malloc(
        sizeof(float) * HA_STATS_RESULT_MAX_SLOTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_stats_values)
        g_stats_values = (float*)malloc(sizeof(float) * HA_STATS_RESULT_MAX_SLOTS);

    g_points = (HaStatPoint*)heap_caps_malloc(
        sizeof(HaStatPoint) * HA_STATISTICS_MAX_POINTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_points) g_points = (HaStatPoint*)malloc(sizeof(HaStatPoint) * HA_STATISTICS_MAX_POINTS);
    g_doc = new (std::nothrow) JsonDocument(&g_stats_allocator);
    if (!g_stats_values || !g_points || !g_doc) {
        LOGE(TAG, "OOM for history workspace — history hydration disabled");
        free(g_stats_values);
        g_stats_values = nullptr;
        free(g_points);
        g_points = nullptr;
        delete g_doc;
        g_doc = nullptr;
        free(g_values);
        g_values = nullptr;
        return;
    }

    g_wake = xSemaphoreCreateBinary();
    if (!g_wake) {
        LOGE(TAG, "semaphore creation failed — history hydration disabled");
        free(g_values);
        g_values = nullptr;
        free(g_stats_values);
        g_stats_values = nullptr;
        free(g_points);
        g_points = nullptr;
        delete g_doc;
        g_doc = nullptr;
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
        free(g_stats_values);
        g_stats_values = nullptr;
        free(g_points);
        g_points = nullptr;
        delete g_doc;
        g_doc = nullptr;
        return;
    }
    ha_stats_sample_task.store(g_task, std::memory_order_release);
    LOGI(TAG, "History hydration ready");
}

bool ha_stats_get_stack_watermark(uint32_t* minimum_free_bytes, uint32_t* configured_bytes) {
    if (!minimum_free_bytes || !configured_bytes) return false;
    const TaskHandle_t task = ha_stats_sample_task.load(std::memory_order_acquire);
    if (!task) return false;
    *minimum_free_bytes = uxTaskGetStackHighWaterMark(task);
    *configured_bytes = HA_STATS_TASK_STACK_BYTES;
    return true;
}

bool ha_stats_busy() {
    return g_state != ST_IDLE;
}

uint32_t ha_stats_backoff_remaining_ms() {
    const int32_t left = (int32_t)(g_blocked_until_ms - millis());
    return (left > 0) ? (uint32_t)left : 0;
}

void ha_stats_config_changed() {
    g_config_revision.fetch_add(1, std::memory_order_acq_rel);
}

bool ha_stats_request(data_stream_handle_t handle, uint32_t uid,
                      const char* entity_id,
                      uint32_t slot_ms, uint16_t slot_count,
                      uint64_t end_bucket, uint16_t raw_hours) {
    if (ota_activity_is_active() || !g_task || !g_values || !g_stats_values) return false;
    if (!entity_id || !entity_id[0] || strlen(entity_id) >= DATA_STREAM_HA_ENTITY_MAX_LEN ||
        slot_count == 0 || raw_hours > HA_HISTORY_MAX_RAW_HOURS ||
        slot_count > HA_STATS_RESULT_MAX_SLOTS || slot_ms == 0 ||
        (uint64_t)slot_ms * slot_count > HA_HISTORY_MAX_WINDOW_SECS * 1000ULL) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    // The portal owns the live DeviceConfig and publishes it only once
    // web_portal_init() has run, which happens well after the streams exist.
    // Not being ready yet is not a failure — refuse the job here so the task
    // never runs and no retry backoff is charged for it.
    const DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg || !cfg->ha_url[0] || !cfg->ha_token[0]) return false;
    HaStatsJob queued{};
    const uint32_t ota_epoch = ota_activity_epoch();
    const uint32_t config_revision =
        g_config_revision.load(std::memory_order_acquire);
    strlcpy(queued.ha_url, cfg->ha_url, sizeof(queued.ha_url));
    strlcpy(queued.ha_token, cfg->ha_token, sizeof(queued.ha_token));
    if (ota_activity_is_active() || ota_epoch != ota_activity_epoch() ||
        config_revision != g_config_revision.load(std::memory_order_acquire))
        return false;
    queued.handle = handle;
    queued.uid = uid;
    strlcpy(queued.entity_id, entity_id, sizeof(queued.entity_id));
    queued.config_revision = config_revision;
    queued.ota_epoch = ota_epoch;
    queued.slot_ms = slot_ms;
    queued.slot_count = slot_count;
    queued.raw_hours = raw_hours;
    queued.end_bucket = end_bucket;
    queued.queued_ms = millis();

    portENTER_CRITICAL(&g_mux);
    const bool accept = (g_state == ST_IDLE) &&
                        (int32_t)(millis() - g_blocked_until_ms) >= 0;
    if (accept) {
        g_job = queued;
        g_state = ST_QUEUED;
    }
    portEXIT_CRITICAL(&g_mux);

    if (accept) {
        xSemaphoreGive(g_wake);
        LOGT(TAG, "Queued %s (stream %d, %u slots x %lums)", entity_id,
             handle, slot_count, (unsigned long)slot_ms);
    }
    return accept;
}

bool ha_stats_deliver() {
    HaStatsJob job;
    uint16_t value_count = 0;
    FetchOutcome outcome = FetchOutcome::RetryableFailure;
    portENTER_CRITICAL(&g_mux);
    const bool ready = (g_state == ST_READY);
    if (ready) {
        job = g_job;
        value_count = g_value_count;
        outcome = g_result_outcome;
        g_state = ST_DELIVERING;
    }
    portEXIT_CRITICAL(&g_mux);
    if (!ready) return false;

    if (outcome == FetchOutcome::Cancelled) {
        data_stream_cancel_history(job.handle, job.uid);
    } else if (outcome == FetchOutcome::PermanentFailure || value_count == 0) {
        data_stream_finish_history(job.handle, job.uid);
    } else if (value_count > 0) {
        data_stream_apply_history(job.handle, job.uid, job.end_bucket,
                                  g_values, value_count,
                                  outcome != FetchOutcome::PartialRetryable);
    }

    portENTER_CRITICAL(&g_mux);
    g_value_count = 0;
    g_result_outcome = FetchOutcome::RetryableFailure;
    g_state = ST_IDLE;
    portEXIT_CRITICAL(&g_mux);
    return true;
}

#endif // HAS_HA_HISTORY
