#include "heart_capture.h"
#include "heart_catalog.h"
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

namespace heart_monitor {
namespace {

constexpr float reliable_correlation = 0.65f;
constexpr float reliable_distinctness = 0.15f;
constexpr float reliable_agreement = 0.15f;
constexpr size_t minimum_reliable_samples = 10 * sample_rate_hz;

class ReportWriter {
public:
    ReportWriter(CsvWrite write, void* context, ExportCheck check)
        : write_(write), context_(context), check_(check) {}
    size_t write(uint8_t byte) { return write(&byte, 1); }
    size_t write(const uint8_t* bytes, size_t size) {
        if (failed_) return 0;
        for (size_t index = 0; index < size; ++index) {
            buffer_[used_++] = static_cast<char>(bytes[index]);
            if (used_ == sizeof(buffer_) && !flush()) return 0;
        }
        return size;
    }
    bool text(const char* text) {
        return write(reinterpret_cast<const uint8_t*>(text), strlen(text)) == strlen(text);
    }
    bool document(const JsonDocument& document, size_t limit) {
        if (document.overflowed()) return false;
        const size_t size = measureJson(document);
        return size <= limit && serializeJson(document, *this) == size && !failed_;
    }
    bool checkpoint() {
        if (check_ && !check_(context_)) failed_ = true;
        return !failed_;
    }
    bool flush() {
        if (!checkpoint()) return false;
        if (used_ && write_(context_, buffer_, used_) != used_) failed_ = true;
        used_ = 0;
        return !failed_;
    }
private:
    CsvWrite write_;
    void* context_;
    ExportCheck check_;
    char buffer_[512];
    size_t used_ = 0;
    bool failed_ = false;
};

template <typename Target>
void number(Target target, double value, bool available = true) {
    if (available && isfinite(value)) target.set(value);
    else target.set(nullptr);
}

bool reliable_sample(const Capture& capture, size_t index) {
    return !capture.interrupted && report_reliable(capture.samples[index].live);
}

bool quality_changed(const BpmResult& previous, const BpmResult& current) {
    return previous.state != current.state || previous.quality_flags != current.quality_flags ||
        previous.waveform_valid != current.waveform_valid || previous.pulse_valid != current.pulse_valid ||
        report_reliable(previous) != report_reliable(current);
}

bool reading_changed(const BpmResult& previous, const BpmResult& current) {
    return quality_changed(previous, current) || previous.updated_ms != current.updated_ms ||
        previous.method != current.method || previous.bpm != current.bpm ||
        previous.rejected_intervals != current.rejected_intervals;
}

uint32_t sample_index_from_ms(double time_ms) {
    return static_cast<uint32_t>(llround(time_ms * sample_rate_hz / 1000.0));
}

bool median_at(const Capture& capture, size_t rank, float low, float high,
               ReportWriter& output, float& result) {
    while (low < high) {
        const double middle = double(low) + (double(high) - low) * 0.5;
        size_t below = 0;
        float nearest_below = low, nearest_above = high;
        for (size_t index = 0; index < capture.count; ++index) {
            if (index % 256 == 0 && !output.checkpoint()) return false;
            if (!reliable_sample(capture, index)) continue;
            const float bpm = capture.samples[index].live.bpm;
            if (bpm <= middle) {
                ++below;
                if (bpm > nearest_below) nearest_below = bpm;
            } else if (bpm < nearest_above) nearest_above = bpm;
        }
        if (below > rank) high = nearest_below;
        else low = nearest_above;
    }
    result = low;
    return true;
}

bool write_summary(const Capture& capture, JsonDocument& document, ReportWriter& output) {
    size_t reliable_count = 0, usable_count = 0, run = 0, longest = 0;
    size_t first_usable = capture.count, first_reliable = capture.count;
    float minimum = 180, maximum = 30;
    double sum = 0;
    for (size_t index = 0; index < capture.count; ++index) {
        if (index % 256 == 0 && !output.checkpoint()) return false;
        const BpmResult& live = capture.samples[index].live;
        if (live.state == BpmState::Usable && live.pulse_valid) {
            ++usable_count;
            if (first_usable == capture.count) first_usable = index;
        }
        if (reliable_sample(capture, index)) {
            ++reliable_count;
            if (first_reliable == capture.count) first_reliable = index;
            sum += live.bpm;
            minimum = fminf(minimum, live.bpm);
            maximum = fmaxf(maximum, live.bpm);
            if (++run > longest) longest = run;
        } else run = 0;
    }
    const bool enough = reliable_count >= minimum_reliable_samples;
    document.clear();
    document["weighting"] = "nominal_sample_duration";
    document["sufficient_reliable_data"] = enough;
    document["reliable_samples"] = reliable_count;
    document["usable_duration_s"] = usable_count / double(sample_rate_hz);
    document["reliable_duration_s"] = reliable_count / double(sample_rate_hz);
    document["longest_reliable_segment_s"] = longest / double(sample_rate_hz);
    number(document["reliable_coverage"], capture.count ? reliable_count / double(capture.count) : 0,
           capture.count != 0);
    number(document["usable_coverage"], capture.count ? usable_count / double(capture.count) : 0,
           capture.count != 0);
    number(document["first_usable_sample_index"], first_usable, first_usable != capture.count);
    number(document["first_reliable_sample_index"], first_reliable, first_reliable != capture.count);
    number(document["time_to_first_reading_s"], first_usable / double(sample_rate_hz),
           first_usable != capture.count);
    number(document["reliable_bpm_mean"], reliable_count ? sum / reliable_count : 0, enough);
    number(document["reliable_bpm_min"], minimum, enough);
    number(document["reliable_bpm_max"], maximum, enough);
    float lower = 0, upper = 0;
    if (enough && (!median_at(capture, (reliable_count - 1) / 2, minimum, maximum, output, lower) ||
                   !median_at(capture, reliable_count / 2, minimum, maximum, output, upper))) return false;
    number(document["reliable_bpm_median"], (double(lower) + upper) * 0.5, enough);
    return output.document(document, 4096);
}

void quality_fields(JsonDocument& document, const Capture& capture, size_t index) {
    const BpmResult& live = capture.samples[index].live;
    document["sample_index"] = index;
    document["state"] = bpm_state_name(live.state);
    document["method"] = bpm_method_name(live.method);
    document["quality_flags"] = live.quality_flags;
    document["waveform_valid"] = live.waveform_valid;
    document["pulse_valid"] = live.pulse_valid;
    document["reliable"] = reliable_sample(capture, index);
}

}

bool report_reliable(const BpmResult& result) {
    const uint32_t excluded = Settling | NoContact | Clipped | Flatline | AbruptChange | LowAmplitude |
        LowPeriodicity | AmbiguousPeriod | RedDisagreement | StaleBeat | OutOfRange;
    return result.state == BpmState::Usable && result.pulse_valid && !(result.quality_flags & excluded) &&
        isfinite(result.bpm) && result.bpm >= 30 && result.bpm <= 180 &&
        result.supporting_samples >= 10 * sample_rate_hz &&
        result.correlation >= reliable_correlation && result.correlation_distinctness >= reliable_distinctness &&
        result.red_bpm > 0 && fabsf(result.red_bpm / result.bpm - 1) <= reliable_agreement &&
        (result.method == BpmMethod::Autocorrelation ||
         (result.method == BpmMethod::Peaks && result.interval_dispersion <= 0.15f));
}

bool catalog_metadata(JsonVariantConst report, JsonDocument& metadata) {
    metadata.clear();
    if (report["format"] != "heart_session_v1" || report["schema_version"] != 1 ||
        report["export_complete"] != true || !report["session"].is<JsonObjectConst>() ||
        !report["summary"].is<JsonObjectConst>()) return false;
    const JsonObjectConst session = report["session"];
    const char* id = session["id"] | "";
    if (strlen(id) != 8) return false;
    for (size_t index = 0; index < 8; ++index)
        if (!((id[index] >= '0' && id[index] <= '9') || (id[index] >= 'a' && id[index] <= 'f'))) return false;
    if (!session["nominal_duration_s"].is<double>() || !session["sample_count"].is<uint32_t>() ||
        !session["complete"].is<bool>()) return false;
    metadata["session_id"] = session["id"];
    metadata["started_unix_time_s"] = session["started_unix_time_s"];
    metadata["created_at"] = session["started_unix_time_s"] | uint32_t(0);
    metadata["duration_s"] = session["nominal_duration_s"];
    metadata["sample_count"] = session["sample_count"];
    metadata["complete"] = session["complete"];
    metadata["stop_reason"] = session["stop_reason"];
    const JsonObjectConst summary = report["summary"];
    for (const char* field : {"sufficient_reliable_data", "reliable_coverage", "reliable_duration_s",
                             "reliable_bpm_mean", "reliable_bpm_min", "reliable_bpm_max"})
        metadata[field] = summary[field];
    return !metadata.overflowed();
}

bool catalog_read_metadata(CatalogRead read, void* context, const char* session_id,
                           JsonDocument& metadata, uint64_t& csv_bytes) {
    metadata.clear();
    csv_bytes = 0;
    if (!read || !session_id) return false;
    struct Reader {
        CatalogRead callback;
        void* context;
        int read() { return callback(context); }
        size_t readBytes(char* buffer, size_t length) {
            size_t count = 0;
            while (count < length) {
                const int value = read();
                if (value < 0) break;
                buffer[count++] = static_cast<char>(value);
            }
            return count;
        }
    } reader{read, context};
    JsonDocument filter, document;
    filter["format"] = filter["schema_version"] = filter["export_complete"] = true;
    for (const char* field : {"id", "started_unix_time_s", "nominal_duration_s", "sample_count",
                             "complete", "stop_reason", "raw_csv", "raw_csv_bytes"}) filter["session"][field] = true;
    for (const char* field : {"sufficient_reliable_data", "reliable_coverage", "reliable_duration_s",
                             "reliable_bpm_mean", "reliable_bpm_min", "reliable_bpm_max"}) filter["summary"][field] = true;
    const DeserializationError error = deserializeJson(document, reader, DeserializationOption::Filter(filter));
    char csv_name[32];
    snprintf(csv_name, sizeof(csv_name), "capture-%s.csv", session_id);
    if (error || document["session"]["id"] != session_id || document["session"]["raw_csv"] != csv_name ||
        !document["session"]["raw_csv_bytes"].is<uint64_t>() ||
        !catalog_metadata(document.as<JsonVariantConst>(), metadata)) return false;
    csv_bytes = document["session"]["raw_csv_bytes"].as<uint64_t>();
    return true;
}

bool write_session_json(const Capture& capture, const ReportIdentity& identity,
                        CsvWrite write, void* context, ExportCheck check) {
    if (!write || capture.count > max_samples || (capture.count && !capture.samples)) return false;
    ReportWriter output(write, context, check);
    JsonDocument document;
    if (!output.text("{\"schema_version\":1,\"format\":\"heart_session_v1\",\"session\":")) return false;
    document["id"] = capture.session_id;
    document["raw_csv"] = identity.raw_csv;
    document["raw_csv_bytes"] = identity.csv_bytes;
    document["raw_csv_checksum"]["algorithm"] = "crc32_ieee";
    document["raw_csv_checksum"]["value"] = identity.csv_crc32;
    document["firmware_version"] = identity.firmware_version;
    document["board"] = identity.board;
    document["started_uptime_ms"] = capture.started_ms;
    number(document["started_unix_time_s"], capture.unix_time_s, capture.unix_time_s != 0);
    document["host_duration_ms"] = capture.duration_ms;
    document["nominal_duration_s"] = capture.count / double(sample_rate_hz);
    document["sample_count"] = capture.count;
    document["stop_reason"] = capture.stop_reason;
    document["complete"] = !capture.interrupted;
    document["exercise_stop_sample_index"] = nullptr;
    document["annotations"].to<JsonArray>();
    if (!output.document(document, 4096) || !output.text(",\"acquisition\":")) return false;
    document.clear();
    document["sample_rate_hz"] = sample_rate_hz;
    document["sensor_sample_rate_hz"] = sample_rate_hz;
    document["sample_average"] = 1;
    document["time_basis"] = "stored_sample_index_not_hardware_sequence";
    document["read_elapsed_us_basis"] = "host_fifo_poll_not_sample_timestamp";
    document["sensor"] = "MAX30101";
    document["sensor_address"] = 0x57;
    document["part_id"] = 0x15;
    document["revision"] = capture.revision;
    document["pulse_width_us"] = pulse_width_us;
    document["adc_range_na"] = adc_range_na;
    document["led_red_code"] = led_amplitude;
    document["led_ir_code"] = led_amplitude;
    document["fifo_rollover"] = false;
    document["max_poll_gap_us"] = capture.max_poll_gap_us;
    document["fifo_overflow_counter"] = capture.overflow_counter;
    document["ambient_overflow_events"] = capture.ambient_overflow_events;
    number(document["lost_samples"], 0, !capture.interrupted);
    JsonArray warnings = document["warnings"].to<JsonArray>();
    if (capture.interrupted) warnings.add("interrupted_nominal_timing_cannot_reconstruct_unknown_gaps");
    if (capture.overflow_counter) warnings.add("fifo_overflow");
    if (capture.ambient_overflow_events) warnings.add("ambient_overflow");
    if (!output.document(document, 4096) || !output.text(",\"estimator\":")) return false;
    document.clear();
    const BpmConfig config;
    document["version"] = "heart_bpm_v2";
    document["window_s"] = config.window_s;
    document["startup_s"] = config.startup_s;
    document["update_s"] = config.update_s;
    document["min_bpm"] = config.min_bpm;
    document["max_bpm"] = config.max_bpm;
    document["highpass_hz"] = config.highpass_hz;
    document["lowpass_hz"] = config.lowpass_hz;
    document["min_correlation"] = config.min_correlation;
    document["min_rms_counts"] = config.min_rms_counts;
    document["refractory_s"] = config.refractory_s;
    document["min_contact_counts"] = config.min_contact_counts;
    document["waveform"] = "causal_second_order_highpass_then_lowpass_ir_counts";
    JsonObject policy = document["reliability_policy"].to<JsonObject>();
    policy["version"] = "heart_reliable_v1";
    policy["requires_complete_capture"] = true;
    policy["min_correlation"] = reliable_correlation;
    policy["min_period_distinctness"] = reliable_distinctness;
    policy["max_red_rate_relative_difference"] = reliable_agreement;
    policy["max_peak_interval_dispersion"] = 0.15;
    policy["minimum_summary_duration_s"] = minimum_reliable_samples / sample_rate_hz;
    policy["allows_autocorrelation_with_unstable_peak_intervals"] = true;
    const char* flag_names[] = {"settling", "no_contact", "clipped", "flatline", "abrupt_change",
        "low_amplitude", "low_periodicity", "ambiguous_period", "rate_disagreement", "red_disagreement",
        "unstable_intervals", "stale_beat", "out_of_range"};
    JsonArray flags = document["quality_flag_definitions"].to<JsonArray>();
    for (unsigned index = 0; index < 13; ++index) {
        JsonObject flag = flags.add<JsonObject>();
        flag["mask"] = uint32_t(1) << index;
        flag["name"] = flag_names[index];
    }
    policy["excluded_quality_flags"] = Settling | NoContact | Clipped | Flatline | AbruptChange |
        LowAmplitude | LowPeriodicity | AmbiguousPeriod | RedDisagreement | StaleBeat | OutOfRange;
    if (!output.document(document, 4096) || !output.text(",\"summary\":")) return false;
    if (!write_summary(capture, document, output) || !output.text(",\"readings\":[")) return false;
    bool first = true;
    for (size_t index = 0; index < capture.count; ++index) {
        if (index % 256 == 0 && !output.checkpoint()) return false;
        const BpmResult& live = capture.samples[index].live;
        if (index && index + 1 != capture.count && !reading_changed(capture.samples[index - 1].live, live)) continue;
        if (!first && !output.text(",")) return false;
        first = false;
        document.clear();
        quality_fields(document, capture, index);
        number(document["bpm"], live.bpm, live.state == BpmState::Usable && live.pulse_valid);
        number(document["candidate_bpm"], live.candidate_bpm, live.candidate_bpm > 0);
        number(document["peak_bpm"], live.peak_bpm, live.peak_bpm > 0);
        number(document["red_bpm"], live.red_bpm, live.red_bpm > 0);
        document["correlation"] = live.correlation;
        document["period_distinctness"] = live.correlation_distinctness;
        document["interval_dispersion"] = live.interval_dispersion;
        document["signal_quality"] = live.signal_quality;
        document["rms_counts"] = live.rms_counts;
        document["lag_samples"] = live.lag_samples;
        document["supporting_samples"] = live.supporting_samples;
        number(document["window_end_index_exclusive"], sample_index_from_ms(live.window_end_ms),
               live.supporting_samples != 0);
        number(document["window_start_index"], sample_index_from_ms(live.window_end_ms) - live.supporting_samples,
               live.supporting_samples != 0);
        number(document["estimator_updated_sample_index"], sample_index_from_ms(live.updated_ms),
               live.supporting_samples != 0);
        document["beat_count"] = live.beat_count;
        document["rejected_intervals"] = live.rejected_intervals;
        if (!output.document(document, report_reading_bytes)) return false;
    }
    if (!output.text("],\"beats\":[")) return false;
    first = true;
    for (size_t index = 0; index < capture.count; ++index) {
        if (index % 256 == 0 && !output.checkpoint()) return false;
        const Sample& sample = capture.samples[index];
        if (!sample.live.beat_detected) continue;
        if (!first && !output.text(",")) return false;
        first = false;
        document.clear();
        document["sample_index"] = sample_index_from_ms(sample.live.beat_timestamp_ms);
        document["detected_at_sample_index"] = index;
        document["beat_count"] = sample.live.beat_count;
        number(document["ibi_ms"], sample.live.ibi_ms, sample.live.ibi_ms > 0);
        document["interval_valid"] = sample.beat_interval_valid;
        document["interval_rejection_reason"] = sample.beat_interval_valid ? nullptr :
            (sample.live.ibi_ms > 0 ? "out_of_range" : "no_previous_beat");
        document["bpm_reliable_at_detection"] = reliable_sample(capture, index);
        if (!output.document(document, report_beat_bytes)) return false;
    }
    if (!output.text("],\"events\":[")) return false;
    document.clear();
    document["sample_index"] = 0;
    document["type"] = "recording_start";
    if (!output.document(document, report_event_bytes)) return false;
    for (size_t index = 0; index < capture.count; ++index) {
        if (index % 256 == 0 && !output.checkpoint()) return false;
        if (index && !quality_changed(capture.samples[index - 1].live, capture.samples[index].live)) continue;
        document.clear();
        quality_fields(document, capture, index);
        document["type"] = "quality_change";
        if (!output.text(",") || !output.document(document, report_event_bytes)) return false;
    }
    document.clear();
    document["sample_index"] = capture.count;
    document["type"] = "recording_stop";
    document["reason"] = capture.stop_reason;
    document["host_elapsed_ms"] = capture.duration_ms;
    document["interrupted"] = capture.interrupted;
    if (!output.text(",") || !output.document(document, report_event_bytes) ||
        !output.text("],\"waveform\":{\"start_sample_index\":0,\"step_samples\":1,\"filtered_ir_counts\":[")) return false;
    JsonDocument scalar;
    for (size_t index = 0; index < capture.count; ++index) {
        if (index % 256 == 0 && !output.checkpoint()) return false;
        if (index && !output.text(",")) return false;
        const BpmResult& live = capture.samples[index].live;
        if (live.waveform_valid && isfinite(live.waveform)) scalar.set(live.waveform);
        else scalar.set(nullptr);
        if (!output.document(scalar, report_waveform_bytes)) return false;
    }
    return output.text("]},\"export_complete\":true}\n") && output.flush();
}

PairCommit commit_session_pair(const char* csv_partial, const char* csv_path,
                               const char* json_partial, const char* json_path,
                               RenameFile rename_file, RemoveFile remove_file, void* context) {
    if (!rename_file(context, csv_partial, csv_path)) return PairCommit::CsvFailed;
    if (rename_file(context, json_partial, json_path)) return PairCommit::Complete;
    return remove_file(context, csv_path) ? PairCommit::JsonFailed : PairCommit::CleanupFailed;
}

}