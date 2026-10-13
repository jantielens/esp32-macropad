#include "device_classes/heart_monitor/heart_capture.h"
#include "device_classes/heart_monitor/heart_catalog.h"
#include <ArduinoJson.h>
#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>

using namespace heart_monitor;

namespace {
class FakeIO : public CaptureIO {
public:
    std::vector<Sample> buffer;
    bool storage_ok = true;
    bool allocation_ok = true;
    bool shutdown_ok = true;
    const char* sensor_error = nullptr;
    const char* read_error = nullptr;
    const char* save_error = nullptr;
    size_t batch_count = 1;
    bool periodic = false;
    bool conflicting_red = false;
    uint64_t free_bytes = UINT64_MAX;
    unsigned frees = 0;
    unsigned saves = 0;
    unsigned checkpoints = 0;
    std::string csv;
    std::string report;
    Sample* allocate_samples(size_t count) override {
        if (!allocation_ok) return nullptr;
        buffer.resize(count);
        return buffer.data();
    }
    void free_samples(Sample*) override { ++frees; }
    bool storage_available(size_t sample_count) override {
        return storage_ok && free_bytes >= capture_storage_required(sample_count);
    }
    const char* start_sensor(Capture&) override { return sensor_error; }
    const char* read_sensor(RawSample* batch, size_t& count, Capture& capture) override {
        if (read_error) {
            capture.overflow_counter = 31;
            return read_error;
        }
        count = batch_count;
        for (size_t index = 0; index < count && index < 31; ++index) {
            const uint32_t ir = periodic ? static_cast<uint32_t>(110000 + 100 *
                std::sin(6.283185307 * 0.8 * (capture.count + index) / sample_rate_hz)) : 456;
            const uint32_t red = conflicting_red ? static_cast<uint32_t>(80000 + 600 *
                std::sin(6.283185307 * 1.4 * (capture.count + index) / sample_rate_hz)) : 123;
            batch[index] = {red, ir, 0};
        }
        return nullptr;
    }
    bool stop_sensor() override { return shutdown_ok; }
    static size_t write(void* context, const char* bytes, size_t size) {
        static_cast<FakeIO*>(context)->csv.append(bytes, size);
        return size;
    }
    static size_t write_report(void* context, const char* bytes, size_t size) {
        static_cast<FakeIO*>(context)->report.append(bytes, size);
        return size;
    }
    const char* save(const Capture& capture, char* filename, size_t size) override {
        ++saves;
        if (save_error) return save_error;
        if (!storage_available(capture.count)) return "storage_unavailable_or_full";
        csv.clear();
        if (!write_csv(capture, write, this)) return "write_failed";
        report.clear();
        ReportIdentity identity;
        identity.raw_csv = "capture.csv";
        identity.firmware_version = "test";
        identity.board = "test";
        identity.csv_bytes = csv.size();
        if (!write_session_json(capture, identity, write_report, this)) return "report_failed";
        snprintf(filename, size, "/storage/heart/capture.csv");
        return nullptr;
    }
};

TEST(HeartCapture, CaptureStopExportAndNoOverwrite) {
    FakeIO io;
    Recorder recorder(io);
    ASSERT_TRUE(recorder.command(Command::Start, 100, 100000, false));
    EXPECT_FALSE(recorder.command(Command::Start, 101, 101000, false));
    EXPECT_FALSE(recorder.command(Command::Discard, 101, 101000, false));
    recorder.tick(120, 120000, false);
    ASSERT_TRUE(recorder.command(Command::Stop, 130, 130000, false));
    recorder.tick(140, 140000, false);
    EXPECT_EQ(recorder.snapshot().state, State::Saved);
    EXPECT_FALSE(recorder.snapshot().retained);
    EXPECT_EQ(io.frees, 1U);
    EXPECT_NE(io.csv.find("0,0,20000,123,456\n"), std::string::npos);
    EXPECT_NE(io.csv.find("# capture_status,complete\n# export_complete,true"), std::string::npos);
    JsonDocument report;
    ASSERT_FALSE(deserializeJson(report, io.report));
    EXPECT_TRUE(report["export_complete"].as<bool>());
    EXPECT_EQ(report["session"]["sample_count"].as<unsigned>(), 1U);
    EXPECT_TRUE(report["summary"]["reliable_bpm_mean"].isNull());
    EXPECT_EQ(report["waveform"]["filtered_ir_counts"].size(), 1U);
}

TEST(HeartCapture, CatalogReadSkipsArraysAndRejectsInterruptedOrMismatchedPairs) {
    JsonDocument report, metadata;
    ASSERT_FALSE(deserializeJson(report, R"({"format":"heart_session_v1","schema_version":1,"session":{"id":"0123abcd","raw_csv":"capture-0123abcd.csv","raw_csv_bytes":123,"started_unix_time_s":null,"nominal_duration_s":10,"sample_count":1000,"complete":true},"summary":{"sufficient_reliable_data":false,"reliable_coverage":0,"reliable_duration_s":0,"reliable_bpm_mean":null},"waveform":{"filtered_ir_counts":[]},"export_complete":true})"));
    for (unsigned index = 0; index < 1000; ++index) report["waveform"]["filtered_ir_counts"].as<JsonArray>().add(index);
    struct Source {
        std::string bytes;
        size_t position = 0;
        size_t stop_at = SIZE_MAX;
        static int read(void* context) {
            Source& source = *static_cast<Source*>(context);
            if (source.position == source.bytes.size() || source.position == source.stop_at) return -1;
            return static_cast<unsigned char>(source.bytes[source.position++]);
        }
    } source;
    serializeJson(report, source.bytes);
    uint64_t csv_bytes = 0;
    ASSERT_TRUE(catalog_read_metadata(Source::read, &source, "0123abcd", metadata, csv_bytes));
    EXPECT_EQ(csv_bytes, 123U);
    EXPECT_TRUE(metadata["started_unix_time_s"].isNull());
    EXPECT_TRUE(metadata["waveform"].isNull());
    EXPECT_LT(measureJson(metadata), 512U);
    for (const size_t stop_at : {size_t(0), source.bytes.size() / 2, source.bytes.size() - 1}) {
        source.position = 0;
        source.stop_at = stop_at;
        EXPECT_FALSE(catalog_read_metadata(Source::read, &source, "0123abcd", metadata, csv_bytes));
        EXPECT_EQ(metadata.size(), 0U);
        EXPECT_EQ(csv_bytes, 0U);
    }
    source.position = 0;
    source.stop_at = SIZE_MAX;
    EXPECT_FALSE(catalog_read_metadata(Source::read, &source, "0123abce", metadata, csv_bytes));
    report["session"]["raw_csv"] = "../capture-0123abcd.csv";
    source.position = 0;
    source.bytes.clear();
    serializeJson(report, source.bytes);
    EXPECT_FALSE(catalog_read_metadata(Source::read, &source, "0123abcd", metadata, csv_bytes));
}

TEST(HeartCapture, CatalogMetadataPreservesSummaryAndRejectsIncompleteExports) {
    JsonDocument report, metadata;
    ASSERT_FALSE(deserializeJson(report, R"({"format":"heart_session_v1","schema_version":1,"export_complete":true,"session":{"id":"d94c9109","started_unix_time_s":null,"nominal_duration_s":145.16,"sample_count":14516,"complete":false,"stop_reason":"ota"},"summary":{"sufficient_reliable_data":false,"reliable_coverage":0.2,"reliable_duration_s":29.032,"reliable_bpm_mean":null,"reliable_bpm_min":null,"reliable_bpm_max":null},"waveform":{"filtered_ir_counts":[1,2,3]}})"));
    ASSERT_TRUE(catalog_metadata(report.as<JsonVariantConst>(), metadata));
    EXPECT_EQ(metadata["session_id"].as<std::string>(), "d94c9109");
    EXPECT_TRUE(metadata["started_unix_time_s"].isNull());
    EXPECT_EQ(metadata["created_at"].as<uint32_t>(), 0U);
    EXPECT_FALSE(metadata["complete"].as<bool>());
    EXPECT_TRUE(metadata["reliable_bpm_mean"].isNull());
    EXPECT_NEAR(metadata["reliable_coverage"].as<double>(), 0.2, 0.000001);
    EXPECT_FALSE(metadata["waveform"].is<JsonObject>());
    report["export_complete"] = false;
    EXPECT_FALSE(catalog_metadata(report.as<JsonVariantConst>(), metadata));
    EXPECT_EQ(metadata.size(), 0U);
    report["export_complete"] = true;
    report["session"]["id"] = "../escape";
    EXPECT_FALSE(catalog_metadata(report.as<JsonVariantConst>(), metadata));
}

TEST(HeartCapture, LiveOutputsPreserveRawExportAndExpireOnStop) {
    FakeIO io;
    io.periodic = true;
    Recorder recorder(io);
    ASSERT_TRUE(recorder.command(Command::Start, 0, 0, false));
    bool pulse_seen = false;
    for (uint32_t index = 0; index < 1400; ++index) {
        recorder.tick(index * 10, index * 10000, false);
        pulse_seen = pulse_seen || recorder.snapshot().live.pulse;
    }
    ASSERT_EQ(recorder.snapshot().live.state, BpmState::Usable);
    EXPECT_NEAR(recorder.snapshot().live.bpm, 48.0f, 1.0f);
    EXPECT_EQ(io.buffer[1399].live.state, BpmState::Usable);
    EXPECT_FLOAT_EQ(io.buffer[1399].live.bpm, recorder.snapshot().live.bpm);
    EXPECT_EQ(io.buffer[0].live.beat_count, 0U);
    EXPECT_TRUE(recorder.snapshot().live.waveform_valid);
    EXPECT_TRUE(pulse_seen);
    io.batch_count = 0;
    recorder.tick(15000, 15000000, false);
    EXPECT_FALSE(recorder.snapshot().live.pulse);
    ASSERT_TRUE(recorder.command(Command::Stop, 15010, 15010000, false));
    EXPECT_FALSE(recorder.snapshot().live.waveform_valid);
    EXPECT_FALSE(recorder.snapshot().live.pulse);
    EXPECT_EQ(recorder.snapshot().live.bpm, 0.0f);
    recorder.tick(15020, 15020000, false);
    EXPECT_EQ(recorder.snapshot().state, State::Saved);
    EXPECT_NE(io.csv.find("0,0,0,123,110000\n"), std::string::npos);
    EXPECT_NE(io.csv.find("# samples,1400"), std::string::npos);
    EXPECT_NE(io.csv.find("# export_complete,true"), std::string::npos);
    JsonDocument report;
    ASSERT_FALSE(deserializeJson(report, io.report));
    EXPECT_EQ(report["estimator"]["version"].as<std::string>(), "heart_bpm_v2");
    EXPECT_EQ(report["estimator"]["reliability_policy"]["version"].as<std::string>(), "heart_reliable_v1");
    EXPECT_EQ(report["session"]["raw_csv_bytes"].as<size_t>(), io.csv.size());
    ASSERT_EQ(report["waveform"]["filtered_ir_counts"].size(), 1400U);
    for (size_t index = 0; index < 1400; ++index) {
        SCOPED_TRACE(index);
        if (io.buffer[index].live.waveform_valid)
            EXPECT_NEAR(report["waveform"]["filtered_ir_counts"][index].as<float>(),
                        io.buffer[index].live.waveform, 0.001);
        else EXPECT_TRUE(report["waveform"]["filtered_ir_counts"][index].isNull());
    }
    for (JsonObject reading : report["readings"].as<JsonArray>()) {
        const size_t index = reading["sample_index"];
        ASSERT_LT(index, 1400U);
        const BpmResult& live = io.buffer[index].live;
        EXPECT_EQ(reading["quality_flags"].as<uint32_t>(), live.quality_flags);
        if (!reading["window_end_index_exclusive"].isNull()) {
            EXPECT_EQ(reading["window_end_index_exclusive"].as<unsigned>(),
                      static_cast<unsigned>(std::llround(live.window_end_ms / 10)));
            EXPECT_EQ(reading["window_end_index_exclusive"].as<unsigned>() -
                      reading["window_start_index"].as<unsigned>(), live.supporting_samples);
        }
        if (live.state == BpmState::Usable) EXPECT_NEAR(reading["bpm"].as<float>(), live.bpm, 0.001);
        else EXPECT_TRUE(reading["bpm"].isNull());
    }
    for (JsonObject beat : report["beats"].as<JsonArray>()) {
        const size_t detected = beat["detected_at_sample_index"];
        ASSERT_LT(detected, 1400U);
        EXPECT_TRUE(io.buffer[detected].live.beat_detected);
        EXPECT_EQ(beat["sample_index"].as<unsigned>(),
                  static_cast<unsigned>(std::llround(io.buffer[detected].live.beat_timestamp_ms / 10)));
        EXPECT_EQ(beat["interval_valid"].as<bool>(), io.buffer[detected].beat_interval_valid);
    }
    ASSERT_TRUE(recorder.command(Command::Start, 16000, 16000000, false));
    EXPECT_EQ(recorder.snapshot().live.state, BpmState::Measuring);
    EXPECT_EQ(recorder.snapshot().live.beat_count, 0U);
    recorder.tick(16010, 16010000, true);
    EXPECT_FALSE(recorder.snapshot().live.waveform_valid);
    EXPECT_FALSE(recorder.snapshot().live.pulse);
}

TEST(HeartCapture, StreamingEstimatorRejectsFlatAndClippedInput) {
    BpmEstimator estimator;
    ASSERT_TRUE(estimator.reset(100));
    for (unsigned index = 0; index < 1200; ++index) estimator.push(110000);
    EXPECT_EQ(estimator.result().state, BpmState::WeakSignal);
    EXPECT_EQ(estimator.result().bpm, 0.0f);
    EXPECT_FALSE(estimator.result().pulse);
    estimator.push(262143);
    EXPECT_FALSE(estimator.result().waveform_valid);
    EXPECT_EQ(estimator.result().state, BpmState::WeakSignal);
    EXPECT_FALSE(estimator.reset(0));
    EXPECT_FALSE(estimator.push(110000));
}

TEST(HeartCapture, AdmissionAndAllocationFailuresAreNotRecording) {
    FakeIO io;
    Recorder recorder(io);
    io.storage_ok = false;
    EXPECT_FALSE(recorder.command(Command::Start, 0, 0, false));
    io.storage_ok = true;
    io.allocation_ok = false;
    EXPECT_FALSE(recorder.command(Command::Start, 0, 0, false));
    EXPECT_STREQ(recorder.snapshot().error, "psram_allocation_failed");
    EXPECT_FALSE(recorder.snapshot().retained);
    io.allocation_ok = true;
    io.sensor_error = "sensor_missing";
    EXPECT_FALSE(recorder.command(Command::Start, 0, 0, false));
    EXPECT_EQ(io.frees, 1U);
    EXPECT_EQ(io.saves, 0U);
}

TEST(HeartCapture, StorageBoundAllowsReportedSpaceAndSaveUsesActualCount) {
    FakeIO io;
    Recorder recorder(io);
    io.free_bytes = capture_storage_required(max_samples);
    ASSERT_TRUE(recorder.command(Command::Start, 0, 0, false));
    recorder.tick(10, 10000, false);
    ASSERT_TRUE(recorder.command(Command::Stop, 20, 20000, false));
    io.free_bytes = capture_storage_required(1);
    recorder.tick(30, 30000, false);
    EXPECT_EQ(recorder.snapshot().state, State::Saved);
    io.free_bytes = capture_storage_required(max_samples) - 1;
    EXPECT_FALSE(recorder.command(Command::Start, 40, 40000, false));
    io.free_bytes = capture_storage_required(max_samples);
    ASSERT_TRUE(recorder.command(Command::Start, 50, 50000, false));
    recorder.tick(60, 60000, false);
    ASSERT_TRUE(recorder.command(Command::Stop, 70, 70000, false));
    io.free_bytes = capture_storage_required(1) - 1;
    recorder.tick(80, 80000, false);
    EXPECT_EQ(recorder.snapshot().state, State::Error);
    EXPECT_TRUE(recorder.snapshot().retained);
    io.free_bytes = capture_storage_required(1);
    ASSERT_TRUE(recorder.command(Command::RetrySave, 90, 90000, false));
    recorder.tick(100, 100000, false);
    EXPECT_EQ(recorder.snapshot().state, State::Saved);
}

TEST(HeartCapture, CsvBoundCoversLargestExportedSampleValues) {
    FakeIO io;
    io.buffer.assign(max_samples, {262143, 262143, UINT32_MAX, {}, false});
    Capture capture;
    capture.samples = io.buffer.data();
    capture.count = max_samples;
    capture.unix_time_s = INT64_MAX;
    capture.started_ms = capture.duration_ms = capture.max_poll_gap_us = UINT32_MAX;
    capture.ambient_overflow_events = UINT32_MAX;
    capture.revision = capture.overflow_counter = UINT8_MAX;
    ASSERT_TRUE(write_csv(capture, FakeIO::write, &io));
    for (size_t index = 0; index < max_samples; ++index) {
        BpmResult& live = io.buffer[index].live;
        live.quality_flags = index % 2 ? NoContact : Clipped;
        live.updated_ms = index * 10;
        live.beat_detected = true;
        live.beat_timestamp_ms = index * 10;
        live.beat_count = live.rejected_intervals = UINT32_MAX;
        live.waveform_valid = true;
        live.waveform = index % 2 ? -262143 : 262143;
    }
    ReportIdentity identity;
    ASSERT_TRUE(write_session_json(capture, identity, FakeIO::write_report, &io));
    EXPECT_LE(io.csv.size() + io.report.size() + 65536, capture_storage_required(max_samples));
    EXPECT_LT(capture_storage_required(max_samples), 100000000U);
    EXPECT_EQ(capture_storage_required(max_samples + 1), UINT64_MAX);
}

TEST(HeartCapture, ReportReliabilityAllowsStrongFallbackAndRejectsWeakOrConflictingRates) {
    BpmResult live;
    live.state = BpmState::Usable;
    live.method = BpmMethod::Autocorrelation;
    live.pulse_valid = true;
    live.bpm = live.red_bpm = 60;
    live.correlation = 0.8f;
    live.correlation_distinctness = 0.3f;
    live.supporting_samples = 1000;
    live.quality_flags = UnstableIntervals;
    EXPECT_TRUE(report_reliable(live));
    live.correlation = 0.5f;
    EXPECT_FALSE(report_reliable(live));
    live.correlation = 0.8f;
    live.red_bpm = 90;
    EXPECT_FALSE(report_reliable(live));
    live.red_bpm = 60;
    live.quality_flags = NoContact;
    EXPECT_FALSE(report_reliable(live));
    live.quality_flags = 0;
    live.method = BpmMethod::Peaks;
    live.interval_dispersion = 0.2f;
    EXPECT_FALSE(report_reliable(live));
}

TEST(HeartCapture, ReportSummaryWeightsReliableDurationAndKeepsImmediateInvalidations) {
    FakeIO io;
    io.buffer.assign(2400, {80000, 110000, 0, {}, false});
    for (size_t index = 0; index < 2400; ++index) {
        BpmResult& live = io.buffer[index].live;
        live.state = BpmState::Usable;
        live.method = BpmMethod::Autocorrelation;
        live.pulse_valid = true;
        live.quality_flags = 0;
        live.bpm = live.red_bpm = index < 600 ? 60 : 80;
        live.correlation = 0.8f;
        live.correlation_distinctness = 0.3f;
        live.supporting_samples = 1000;
        live.window_end_ms = 10000;
        live.updated_ms = 12000;
        if (index >= 2000) {
            live.state = BpmState::WeakSignal;
            live.pulse_valid = false;
            live.quality_flags = NoContact;
        }
    }
    Capture capture;
    capture.samples = io.buffer.data();
    capture.count = 2400;
    ReportIdentity identity;
    ASSERT_TRUE(write_session_json(capture, identity, FakeIO::write_report, &io));
    JsonDocument report;
    ASSERT_FALSE(deserializeJson(report, io.report));
    EXPECT_DOUBLE_EQ(report["summary"]["reliable_duration_s"].as<double>(), 20);
    EXPECT_NEAR(report["summary"]["reliable_bpm_mean"].as<double>(), 74, 0.001);
    EXPECT_DOUBLE_EQ(report["summary"]["reliable_bpm_median"].as<double>(), 80);
    EXPECT_NEAR(report["summary"]["reliable_coverage"].as<double>(), 2000.0 / 2400, 0.00001);
    bool invalidation_found = false;
    for (JsonObject event : report["events"].as<JsonArray>()) {
        if (event["sample_index"].as<unsigned>() == 2000 &&
            event["type"].as<std::string>() == "quality_change") {
            invalidation_found = true;
            EXPECT_FALSE(event["pulse_valid"].as<bool>());
        }
    }
    EXPECT_TRUE(invalidation_found);
    capture.interrupted = true;
    capture.stop_reason = "fifo_overflow_or_full";
    io.report.clear();
    ASSERT_TRUE(write_session_json(capture, identity, FakeIO::write_report, &io));
    ASSERT_FALSE(deserializeJson(report, io.report));
    EXPECT_FALSE(report["session"]["complete"].as<bool>());
    EXPECT_TRUE(report["acquisition"]["lost_samples"].isNull());
    EXPECT_TRUE(report["summary"]["reliable_bpm_mean"].isNull());
    EXPECT_FALSE(report["readings"][0]["reliable"].as<bool>());
    EXPECT_GT(report["acquisition"]["warnings"].size(), 0U);
    capture.interrupted = false;
    capture.count = 2000;
    const float lower = std::nextafter(60.0f, 61.0f);
    const float upper = std::nextafter(lower, 61.0f);
    for (size_t index = 0; index < capture.count; ++index)
        io.buffer[index].live.bpm = io.buffer[index].live.red_bpm = index < 1000 ? lower : upper;
    io.report.clear();
    ASSERT_TRUE(write_session_json(capture, identity, FakeIO::write_report, &io,
        [](void* context) { return ++static_cast<FakeIO*>(context)->checkpoints < 1000; }));
    ASSERT_FALSE(deserializeJson(report, io.report));
    EXPECT_NEAR(report["summary"]["reliable_bpm_median"].as<double>(),
                (double(lower) + upper) * 0.5, 0.000001);
}

TEST(HeartCapture, ReportEmptyCaptureEscapesMetadataAndHandlesWriteOrOtaFailure) {
    FakeIO io;
    Capture capture;
    capture.stop_reason = "test\"reason";
    ReportIdentity identity;
    ASSERT_TRUE(write_session_json(capture, identity, FakeIO::write_report, &io));
    JsonDocument report;
    ASSERT_FALSE(deserializeJson(report, io.report));
    EXPECT_EQ(report["session"]["stop_reason"].as<std::string>(), "test\"reason");
    EXPECT_TRUE(report["summary"]["reliable_coverage"].isNull());
    EXPECT_EQ(report["readings"].size(), 0U);
    EXPECT_EQ(report["events"].size(), 2U);
    EXPECT_FALSE(write_session_json(capture, identity,
        [](void*, const char*, size_t size) { return size - 1; }, nullptr));
    io.report.clear();
    EXPECT_FALSE(write_session_json(capture, identity, FakeIO::write_report, &io,
        [](void*) { return false; }));
    EXPECT_TRUE(io.report.empty());
}

TEST(HeartCapture, PairedPublicationRollsBackCsvWhenJsonRenameFails) {
    struct CommitState {
        unsigned renames = 0;
        bool removed = false;
        bool cleanup_ok = true;
    } state;
    const auto rename_file = [](void* context, const char*, const char*) {
        return ++static_cast<CommitState*>(context)->renames == 1;
    };
    const auto remove_file = [](void* context, const char*) {
        CommitState& state = *static_cast<CommitState*>(context);
        state.removed = true;
        return state.cleanup_ok;
    };
    EXPECT_EQ(commit_session_pair("csv.part", "csv", "json.part", "json",
        rename_file, remove_file, &state), PairCommit::JsonFailed);
    EXPECT_TRUE(state.removed);
    state.renames = 0;
    state.cleanup_ok = false;
    EXPECT_EQ(commit_session_pair("csv.part", "csv", "json.part", "json",
        rename_file, remove_file, &state), PairCommit::CleanupFailed);
    EXPECT_EQ(commit_session_pair("csv.part", "csv", "json.part", "json",
        [](void*, const char*, const char*) { return true; }, remove_file, &state), PairCommit::Complete);
}

TEST(HeartCapture, RecorderUsesRedChannelForQualityVeto) {
    FakeIO io;
    io.periodic = io.conflicting_red = true;
    Recorder recorder(io);
    ASSERT_TRUE(recorder.command(Command::Start, 0, 0, false));
    for (uint32_t index = 0; index < 2500; ++index) {
        recorder.tick(index * 10, index * 10000, false);
        EXPECT_FALSE(recorder.snapshot().live.pulse_valid);
    }
    EXPECT_NE(recorder.snapshot().live.quality_flags & RedDisagreement, 0U);
}

TEST(HeartCapture, EstimatorTracksCleanRatesAndRetainsNominalBeatTimes) {
    for (float bpm : {30.0f, 48.0f, 58.0f, 90.0f, 120.0f, 180.0f}) {
        SCOPED_TRACE(bpm);
        BpmEstimator estimator;
        ASSERT_TRUE(estimator.reset(100));
        for (unsigned index = 0; index < 4000; ++index) {
            const float phase = 6.283185307f * bpm / 60 * index / 100;
            estimator.push(static_cast<uint32_t>(110000 + 1000 * std::sin(phase)),
                           static_cast<uint32_t>(80000 + 600 * std::sin(phase - 0.2f)));
            if (index < 200) {
                EXPECT_FALSE(estimator.result().pulse_valid);
                EXPECT_FALSE(estimator.result().beat_detected);
            }
        }
        const BpmResult result = estimator.result();
        ASSERT_TRUE(result.pulse_valid);
        EXPECT_EQ(result.method, BpmMethod::Peaks);
        EXPECT_NEAR(result.bpm, bpm, 1.0f);
        EXPECT_EQ(result.supporting_samples, 1000U);
        EXPECT_GT(result.updated_ms, result.window_end_ms);
        EXPECT_LT(result.updated_ms - result.window_end_ms, 2000);
        ASSERT_GT(estimator.beat_history_count(), 4U);
        Beat previous;
        ASSERT_TRUE(estimator.beat_at(0, previous));
        for (size_t index = 1; index < estimator.beat_history_count(); ++index) {
            Beat beat;
            ASSERT_TRUE(estimator.beat_at(index, beat));
            EXPECT_TRUE(beat.ibi_valid);
            EXPECT_GT(beat.sample_index, previous.sample_index);
            EXPECT_DOUBLE_EQ(beat.timestamp_ms, beat.sample_index * 10.0);
            EXPECT_NEAR(beat.ibi_ms, 60000 / bpm, 10.1f);
            previous = beat;
        }
        Beat unavailable;
        EXPECT_FALSE(estimator.beat_at(estimator.beat_history_count(), unavailable));
    }
}

TEST(HeartCapture, PeakDetectorRejectsRecordedRisingShoulderDoubleCount) {
    const float waveform[] = {
        -45.8277f, -45.6864f, -45.2901f, -44.5499f, -43.7024f, -43.0046f, -42.7233f, -42.6957f,
        -42.0491f, -40.9306f, -40.0319f, -39.2280f, -38.2462f, -36.4848f, -33.6201f, -30.1918f,
        -26.9323f, -24.1719f, -22.0006f, -20.5756f, -19.5509f, -18.6965f, -17.8614f, -16.7484f,
        -15.7793f, -14.7850f, -12.8455f, -10.1804f, -7.4732f, -4.7997f, -2.3587f, -0.4969f,
        1.0435f, 2.6481f, 4.2011f, 5.5851f, 6.4673f, 6.5892f, 6.4667f, 6.6051f,
        7.2213f, 8.2188f, 9.4530f, 10.8865f, 12.3725f, 13.8702f, 15.2685f, 16.3381f,
        16.9430f, 17.2282f, 17.3231f, 17.4301f, 17.8787f, 18.4477f, 18.8346f, 19.1598f,
        19.4536f, 19.5536f, 19.8063f, 20.7066f, 21.9792f, 23.1366f, 23.8557f, 24.2064f,
        24.7279f, 25.6114f, 26.5525f, 27.3009f, 27.7498f, 28.1189f, 28.7279f, 29.3281f,
        29.6458f, 29.6175f, 29.3784f, 29.0840f, 28.3970f, 27.3654f, 26.4373f, 25.5809f,
        24.8104f, 24.3268f, 24.1414f, 23.9808f, 23.7900f, 23.7552f, 23.6893f, 23.8208f,
        24.4582f, 25.4202f, 26.4948f, 26.7446f, 25.4749f, 23.5614f, 22.0676f, 21.2641f,
        20.9617f, 20.9881f, 21.2310f, 21.4295f, 21.2920f, 20.9577f, 21.1074f, 21.7203f,
        21.7305f, 20.4877f, 17.9269f, 14.2395f, 9.9227f, 5.1786f, 0.0644f, -5.2402f,
        -10.6534f, -15.9683f, -20.8945f, -25.2750f, -29.0404f, -32.2908f, -35.2736f, -38.3585f,
        -41.5286f, -44.2861f, -46.5762f, -48.6191f, -50.2308f, -51.2047f
    };
    const size_t period = sizeof(waveform) / sizeof(waveform[0]);
    PeakDetector detector;
    detector.reset(100, BpmConfig{});
    unsigned stable_beats = 0;
    for (uint64_t sample_index = 0; sample_index <= period * 10; ++sample_index) {
        uint64_t peak_index = 0;
        if (!detector.push(waveform[sample_index % period], sample_index, peak_index) ||
            sample_index < period * 2) continue;
        SCOPED_TRACE(sample_index);
        EXPECT_EQ(peak_index, (stable_beats + 2) * period - 1);
        EXPECT_EQ(peak_index + 1, sample_index);
        ++stable_beats;
    }
    EXPECT_EQ(stable_beats, 9U);
}

TEST(HeartCapture, EstimatorRejectsNoiseAndOutOfRangePeriodicity) {
    BpmEstimator estimator;
    ASSERT_TRUE(estimator.reset(100));
    uint32_t random_state = 17;
    for (unsigned index = 0; index < 6000; ++index) {
        random_state = random_state * 1664525U + 1013904223U;
        estimator.push(110000 + (random_state >> 16) % 2000);
        EXPECT_FALSE(estimator.result().pulse_valid);
    }
    ASSERT_TRUE(estimator.reset(100));
    for (unsigned index = 0; index < 4000; ++index)
        estimator.push(static_cast<uint32_t>(110000 + 1000 * std::sin(6.283185307f * 4 * index / 100)));
    EXPECT_FALSE(estimator.result().pulse_valid);
    EXPECT_NE(estimator.result().quality_flags & OutOfRange, 0U);
}

TEST(HeartCapture, EstimatorRejectsContactLossAndRecoversWithoutBridgingIntervals) {
    BpmEstimator estimator;
    ASSERT_TRUE(estimator.reset(100));
    for (unsigned index = 0; index < 2000; ++index)
        estimator.push(static_cast<uint32_t>(110000 + 1000 * std::sin(6.283185307f * index / 100)));
    ASSERT_TRUE(estimator.result().pulse_valid);
    estimator.push(0);
    EXPECT_FALSE(estimator.result().pulse_valid);
    EXPECT_FALSE(estimator.result().waveform_valid);
    EXPECT_NE(estimator.result().quality_flags & NoContact, 0U);
    bool first_beat = true;
    for (unsigned index = 0; index < 2000; ++index) {
        estimator.push(static_cast<uint32_t>(110000 + 1000 * std::sin(6.283185307f * index / 100)));
        if (first_beat && estimator.result().beat_detected) {
            first_beat = false;
            Beat beat;
            ASSERT_TRUE(estimator.beat_at(estimator.beat_history_count() - 1, beat));
            EXPECT_FALSE(beat.ibi_valid);
        }
    }
    EXPECT_FALSE(first_beat);
    EXPECT_TRUE(estimator.result().pulse_valid);
    estimator.push(262143);
    EXPECT_FALSE(estimator.result().pulse_valid);
    EXPECT_NE(estimator.result().quality_flags & Clipped, 0U);
}

TEST(HeartCapture, EstimatorFallbackDoesNotInventPulsesAndRedCanVetoRate) {
    BpmEstimator estimator;
    ASSERT_TRUE(estimator.reset(100));
    for (unsigned index = 0; index < 4000; ++index) {
        const float phase = 6.283185307f * index / 100;
        const float amplitude = (index / 100) % 3 == 2 ? 200.0f : 1000.0f;
        estimator.push(static_cast<uint32_t>(110000 + amplitude * std::sin(phase)),
                       static_cast<uint32_t>(80000 + 600 * std::sin(phase)));
        if (index >= 1500) {
            ASSERT_TRUE(estimator.result().pulse_valid);
            EXPECT_EQ(estimator.result().method, BpmMethod::Autocorrelation);
            EXPECT_NEAR(estimator.result().bpm, 60, 1);
            EXPECT_FALSE(estimator.result().pulse);
        }
    }
    ASSERT_TRUE(estimator.reset(100));
    EXPECT_EQ(estimator.beat_history_count(), 0U);
    EXPECT_EQ(estimator.result().beat_count, 0U);
    for (unsigned index = 0; index < 4000; ++index) {
        const float phase = 6.283185307f * index / 100;
        estimator.push(static_cast<uint32_t>(110000 + 1000 * std::sin(phase)),
                       static_cast<uint32_t>(80000 + 600 * std::sin(1.6f * phase)));
        EXPECT_FALSE(estimator.result().pulse_valid);
    }
    EXPECT_NE(estimator.result().quality_flags & RedDisagreement, 0U);
}

TEST(HeartCapture, OverflowExportsOnlyKnownSamplesAsInterrupted) {
    FakeIO io;
    Recorder recorder(io);
    ASSERT_TRUE(recorder.command(Command::Start, 0, 0, false));
    recorder.tick(10, 10000, false);
    io.read_error = "fifo_overflow";
    recorder.tick(20, 20000, false);
    recorder.tick(30, 30000, false);
    EXPECT_EQ(recorder.snapshot().state, State::Error);
    EXPECT_EQ(recorder.snapshot().samples, 1U);
    EXPECT_STREQ(recorder.snapshot().error, "fifo_overflow");
    EXPECT_FALSE(recorder.snapshot().retained);
    EXPECT_NE(io.csv.find("# fifo_overflow_counter,31"), std::string::npos);
    EXPECT_NE(io.csv.find("# capture_status,interrupted"), std::string::npos);
}

TEST(HeartCapture, EstimatorDoesNotHalveRateWhenAlternateBeatsDiffer) {
    for (float rate : {48.0f, 72.0f, 120.0f}) {
        SCOPED_TRACE(rate);
        BpmEstimator estimator;
        ASSERT_TRUE(estimator.reset(100));
        bool baseline_valid = false;
        for (unsigned index = 0; index < 5000; ++index) {
            const float phase = 6.283185307f * rate / 60 * index / 100;
            const float alternating = index >= 1800 ? 0.9f * std::sin(phase * 0.5f) : 0;
            estimator.push(static_cast<uint32_t>(110000 + 1000 * (std::sin(phase) + alternating)),
                           static_cast<uint32_t>(80000 + 600 * (std::sin(phase) + alternating)));
            if (index >= 1500 && index < 1800 && estimator.result().pulse_valid) baseline_valid = true;
            if (index >= 3000 && estimator.result().pulse_valid) {
                ASSERT_NEAR(estimator.result().bpm, rate, 2);
            }
        }
        EXPECT_TRUE(baseline_valid);
    }
}

TEST(HeartCapture, SaveFailureRetainsForRetryOrDiscard) {
    FakeIO io;
    Recorder recorder(io);
    ASSERT_TRUE(recorder.command(Command::Start, 0, 0, false));
    recorder.tick(10, 10000, false);
    ASSERT_TRUE(recorder.command(Command::Stop, 20, 20000, false));
    io.save_error = "short_write";
    recorder.tick(30, 30000, false);
    EXPECT_TRUE(recorder.snapshot().retained);
    EXPECT_EQ(io.frees, 0U);
    EXPECT_FALSE(recorder.command(Command::Start, 40, 40000, false));
    io.save_error = nullptr;
    ASSERT_TRUE(recorder.command(Command::RetrySave, 40, 40000, false));
    recorder.tick(50, 50000, false);
    EXPECT_EQ(recorder.snapshot().state, State::Saved);
    EXPECT_EQ(io.frees, 1U);
    ASSERT_TRUE(recorder.command(Command::Start, 60, 60000, false));
    ASSERT_TRUE(recorder.command(Command::Stop, 70, 70000, false));
    io.save_error = "open_failed";
    recorder.tick(80, 80000, false);
    EXPECT_TRUE(recorder.command(Command::Discard, 90, 90000, false));
    EXPECT_FALSE(recorder.snapshot().retained);
    EXPECT_EQ(io.frees, 2U);
}

TEST(HeartCapture, OtaStopsCaptureAndDefersSaving) {
    FakeIO io;
    Recorder recorder(io);
    EXPECT_FALSE(recorder.command(Command::Start, 0, 0, true));
    ASSERT_TRUE(recorder.command(Command::Start, 0, 0, false));
    recorder.tick(10, 10000, true);
    recorder.tick(20, 20000, true);
    EXPECT_EQ(io.saves, 0U);
    EXPECT_TRUE(recorder.snapshot().retained);
    recorder.tick(30, 30000, false);
    EXPECT_EQ(io.saves, 1U);
    EXPECT_STREQ(recorder.snapshot().error, "ota_interrupted");
}

TEST(HeartCapture, DurationLimitHandlesUptimeWrap) {
    FakeIO io;
    Recorder recorder(io);
    constexpr uint32_t start = UINT32_MAX - 100;
    ASSERT_TRUE(recorder.command(Command::Start, start, 0, false));
    recorder.tick(start + max_duration_ms, 0, false);
    recorder.tick(0, 0, false);
    EXPECT_EQ(recorder.snapshot().state, State::Saved);
    EXPECT_EQ(recorder.snapshot().elapsed_ms, max_duration_ms);
    EXPECT_NE(io.csv.find("# stop_reason,duration_limit"), std::string::npos);
}

TEST(HeartCapture, CapacityAndShutdownErrorsAreExplicit) {
    FakeIO io;
    Recorder recorder(io);
    ASSERT_TRUE(recorder.command(Command::Start, 0, 0, false));
    io.batch_count = 32;
    recorder.tick(10, 10000, false);
    recorder.tick(20, 20000, false);
    EXPECT_STREQ(recorder.snapshot().error, "buffer_capacity");
    ASSERT_TRUE(recorder.command(Command::Start, 30, 30000, false));
    io.shutdown_ok = false;
    ASSERT_TRUE(recorder.command(Command::Stop, 40, 40000, false));
    recorder.tick(50, 50000, false);
    EXPECT_STREQ(recorder.snapshot().error, "sensor_shutdown_failed");
}

TEST(HeartCapture, CsvShortWriteDoesNotEmitCompletionFooter) {
    Capture capture;
    unsigned calls = 0;
    auto short_write = [](void* context, const char*, size_t size) -> size_t {
        ++*static_cast<unsigned*>(context);
        return size - 1;
    };
    EXPECT_FALSE(write_csv(capture, short_write, &calls));
    EXPECT_EQ(calls, 1U);
}
}