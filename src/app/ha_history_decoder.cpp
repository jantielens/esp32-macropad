#include "ha_history_decoder.h"

#include "ha_stats_resample.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
bool json_whitespace(uint8_t byte) {
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}
}

bool HaHistoryDecoder::fail(Error error) {
    error_ = error;
    state_ = State::Failed;
    return false;
}

bool HaHistoryDecoder::consume(uint8_t byte, RecordCallback callback, void* context) {
    auto start_record = [&]() {
        record_size_ = 0;
        object_depth_ = 1;
        in_string_ = false;
        escaped_ = false;
        record_[record_size_++] = '{';
        state_ = State::Record;
    };

    switch (state_) {
        case State::RootStart:
            if (json_whitespace(byte)) return true;
            if (byte != '[') return fail(Error::InvalidJson);
            state_ = State::RootFirst;
            return true;
        case State::RootFirst:
            if (json_whitespace(byte)) return true;
            if (byte == ']') {
                state_ = State::Done;
                return true;
            }
            if (byte != '[') return fail(Error::InvalidJson);
            state_ = State::EntityFirst;
            return true;
        case State::EntityFirst:
            if (json_whitespace(byte)) return true;
            if (byte == ']') {
                state_ = State::RootEnd;
                return true;
            }
            if (byte != '{') return fail(Error::InvalidJson);
            start_record();
            return true;
        case State::Record:
            if (record_size_ >= MAX_RECORD_BYTES) return fail(Error::RecordTooLarge);
            record_[record_size_++] = (char)byte;
            if (in_string_) {
                if (escaped_) escaped_ = false;
                else if (byte == '\\') escaped_ = true;
                else if (byte == '"') in_string_ = false;
                return true;
            }
            if (byte == '"') {
                in_string_ = true;
            } else if (byte == '{') {
                object_depth_++;
            } else if (byte == '}') {
                if (--object_depth_ == 0) {
                    record_[record_size_] = '\0';
                    if (record_size_ > peak_record_bytes_) peak_record_bytes_ = record_size_;
                    if (records_seen_ >= MAX_RECORDS) return fail(Error::RecordLimit);
                    if (!callback || !callback(record_, record_size_, context))
                        return fail(Error::CallbackFailed);
                    records_seen_++;
                    state_ = State::EntityNext;
                }
            }
            return true;
        case State::EntityNext:
            if (json_whitespace(byte)) return true;
            if (byte == ',') {
                state_ = State::EntityArrayStart;
                return true;
            }
            if (byte == ']') {
                state_ = State::RootEnd;
                return true;
            }
            return fail(Error::InvalidJson);
        case State::EntityArrayStart:
            if (json_whitespace(byte)) return true;
            if (byte != '{') return fail(Error::InvalidJson);
            start_record();
            return true;
        case State::RootEnd:
            if (json_whitespace(byte)) return true;
            if (byte != ']') return fail(Error::InvalidJson);
            state_ = State::Done;
            return true;
        case State::Done:
            return json_whitespace(byte) || fail(Error::InvalidJson);
        case State::Failed:
            return false;
    }
    return fail(Error::InvalidJson);
}

bool HaHistoryDecoder::feed(const uint8_t* data, size_t size,
                           RecordCallback callback, void* context) {
    if (state_ == State::Failed || (!data && size)) return false;
    if (size > MAX_BODY_BYTES - bytes_seen_) return fail(Error::BodyLimit);
    bytes_seen_ += size;
    for (size_t i = 0; i < size; i++) {
        if (!consume(data[i], callback, context)) return false;
    }
    return true;
}

bool HaHistoryDecoder::finish() {
    if (state_ == State::Failed) return false;
    if (state_ != State::Done) return fail(Error::Incomplete);
    return true;
}

bool ha_history_parse_record(const char* json, size_t size, JsonDocument& document,
                             uint64_t* timestamp_ms, float* value) {
    if (!json || !timestamp_ms || !value) return false;

    StaticJsonDocument<128> filter;
    filter["state"] = true;
    filter["last_changed"] = true;
    filter["last_updated"] = true;
    document.clear();
    if (deserializeJson(document, json, size,
                        DeserializationOption::Filter(filter),
                        DeserializationOption::NestingLimit(8))) return false;

    const char* timestamp = document["last_changed"].as<const char*>();
    if (!timestamp) timestamp = document["last_updated"].as<const char*>();
    if (!timestamp) return false;
    const uint64_t parsed_timestamp = ha_stats_parse_iso8601_ms(timestamp);
    if (!parsed_timestamp) return false;

    const char* state = document["state"].as<const char*>();
    if (!state) return false;
    char* end = nullptr;
    errno = 0;
    const float parsed_value = strtof(state, &end);
    *value = NAN;
    if (end != state && *end == '\0' && errno != ERANGE && isfinite(parsed_value))
        *value = parsed_value;
    *timestamp_ms = parsed_timestamp;
    return true;
}
