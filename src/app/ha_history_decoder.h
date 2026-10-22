#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

class HaHistoryDecoder {
public:
    static constexpr size_t MAX_RECORD_BYTES = 4096;
    static constexpr size_t MAX_BODY_BYTES = 2 * 1024 * 1024;
    static constexpr uint32_t MAX_RECORDS = 100000;

    enum class Error : uint8_t {
        None,
        InvalidJson,
        RecordTooLarge,
        BodyLimit,
        RecordLimit,
        CallbackFailed,
        Incomplete
    };

    using RecordCallback = bool (*)(const char* json, size_t size, void* context);

    bool feed(const uint8_t* data, size_t size, RecordCallback callback, void* context);
    bool finish();
    Error error() const { return error_; }
    size_t bytes_seen() const { return bytes_seen_; }
    uint32_t records_seen() const { return records_seen_; }
    size_t peak_record_bytes() const { return peak_record_bytes_; }

private:
    enum class State : uint8_t {
        RootStart,
        RootFirst,
        EntityArrayStart,
        EntityFirst,
        Record,
        EntityNext,
        RootEnd,
        Done,
        Failed
    };

    bool consume(uint8_t byte, RecordCallback callback, void* context);
    bool fail(Error error);

    State state_ = State::RootStart;
    Error error_ = Error::None;
    char record_[MAX_RECORD_BYTES + 1] = {};
    size_t record_size_ = 0;
    size_t bytes_seen_ = 0;
    size_t peak_record_bytes_ = 0;
    uint32_t records_seen_ = 0;
    uint16_t object_depth_ = 0;
    bool in_string_ = false;
    bool escaped_ = false;
};

// Parse the fields needed from one REST history state. The caller owns and
// bounds the JsonDocument allocator. Nonnumeric/unknown states return NAN.
bool ha_history_parse_record(const char* json, size_t size, JsonDocument& document,
                             uint64_t* timestamp_ms, float* value);
