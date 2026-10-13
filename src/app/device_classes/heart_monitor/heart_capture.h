#pragma once

#include <stddef.h>
#include <stdint.h>
#include "heart_bpm.h"

namespace heart_monitor {

constexpr uint32_t sample_rate_hz = 100;
constexpr uint32_t max_duration_ms = 300000;
constexpr size_t max_samples = sample_rate_hz * (max_duration_ms / 1000);
constexpr uint8_t led_amplitude = 0x1f;
constexpr uint16_t pulse_width_us = 411;
constexpr uint16_t adc_range_na = 4096;
constexpr size_t report_reading_bytes = 2048;
constexpr size_t report_event_bytes = 512;
constexpr size_t report_beat_bytes = 512;
constexpr size_t report_waveform_bytes = 16;

constexpr uint64_t capture_storage_required(size_t sample_count) {
    return sample_count > max_samples ? UINT64_MAX : uint64_t(sample_count) *
        (41 + 4 + report_reading_bytes + report_event_bytes + report_beat_bytes + report_waveform_bytes) +
        2 * 768 + 16384 + 65536;
}

enum class State { Idle, Recording, AwaitingSave, Saving, Saved, Error };
enum class Command { Start, Stop, RetrySave, Discard };

struct RawSample {
    uint32_t red;
    uint32_t ir;
    uint32_t read_elapsed_us;
};

struct Sample {
    uint32_t red;
    uint32_t ir;
    uint32_t read_elapsed_us;
    BpmResult live;
    bool beat_interval_valid;
};

struct Snapshot {
    State state = State::Idle;
    uint32_t elapsed_ms = 0;
    uint32_t samples = 0;
    bool retained = false;
    BpmResult live;
    char error[48] = {};
    char filename[96] = {};
};

struct Capture {
    Sample* samples = nullptr;
    size_t count = 0;
    uint32_t started_ms = 0;
    uint32_t started_us = 0;
    uint32_t duration_ms = 0;
    int64_t unix_time_s = 0;
    uint8_t revision = 0;
    uint32_t max_poll_gap_us = 0;
    uint8_t overflow_counter = 0;
    uint32_t ambient_overflow_events = 0;
    const char* stop_reason = "manual_stop";
    bool interrupted = false;
    char session_id[9] = {};
};

class CaptureIO {
public:
    virtual ~CaptureIO() = default;
    virtual Sample* allocate_samples(size_t count) = 0;
    virtual void free_samples(Sample* samples) = 0;
    virtual bool storage_available(size_t sample_count) = 0;
    virtual const char* start_sensor(Capture& capture) = 0;
    virtual const char* read_sensor(RawSample* batch, size_t& count, Capture& capture) = 0;
    virtual bool stop_sensor() = 0;
    virtual const char* save(const Capture& capture, char* filename, size_t size) = 0;
};

class Recorder {
public:
    explicit Recorder(CaptureIO& io) : io_(io) {}
    bool command(Command command, uint32_t now_ms, uint32_t now_us, bool ota);
    void tick(uint32_t now_ms, uint32_t now_us, bool ota);
    Snapshot snapshot() const { return snapshot_; }
private:
    void finish(const char* reason, bool interrupted, uint32_t now_ms);
    void release();
    void set_error(const char* error);
    CaptureIO& io_;
    Capture capture_;
    Snapshot snapshot_;
    BpmEstimator estimator_;
    uint32_t last_beat_ms_ = 0;
};

using CsvWrite = size_t (*)(void* context, const char* bytes, size_t size);
bool write_csv(const Capture& capture, CsvWrite write, void* context);
using ExportCheck = bool (*)(void* context);
struct ReportIdentity {
    const char* firmware_version = nullptr;
    const char* board = nullptr;
    const char* raw_csv = nullptr;
    const char* csv_crc32 = nullptr;
    uint64_t csv_bytes = 0;
};
bool report_reliable(const BpmResult& result);
bool write_session_json(const Capture& capture, const ReportIdentity& identity,
                        CsvWrite write, void* context, ExportCheck check = nullptr);
enum class PairCommit { Complete, CsvFailed, JsonFailed, CleanupFailed };
using RenameFile = bool (*)(void*, const char*, const char*);
using RemoveFile = bool (*)(void*, const char*);
PairCommit commit_session_pair(const char* csv_partial, const char* csv_path,
                               const char* json_partial, const char* json_path,
                               RenameFile rename_file, RemoveFile remove_file, void* context);
const char* state_name(State state);

}