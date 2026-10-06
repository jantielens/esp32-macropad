#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr size_t REMOTE_LOG_LINE_BYTES = 288;
constexpr size_t REMOTE_LOG_RESPONSE_RECORDS = 32;

struct RemoteLogRecord {
    uint32_t sequence;
    char line[REMOTE_LOG_LINE_BYTES];
};

struct RemoteLogRange {
    uint32_t oldest = 0;
    uint32_t newest = 0;
    uint32_t cursor = 0;
    uint32_t missed = 0;
    size_t count = 0;
    bool reset = false;
    bool has_more = false;
};

class RemoteLogStore {
public:
    void init(RemoteLogRecord* records, size_t capacity, uint32_t sequence = 0) {
        records_ = records;
        capacity_ = capacity;
        sequence_ = sequence;
        count_ = head_ = 0;
    }

    void append(const char* line) {
        if (!capacity_) return;
        RemoteLogRecord& record = records_[head_];
        record.sequence = ++sequence_;
        const size_t length = strnlen(line, sizeof(record.line) - 1);
        memcpy(record.line, line, length);
        record.line[length] = '\0';
        head_ = (head_ + 1) % capacity_;
        if (count_ < capacity_) ++count_;
    }

    RemoteLogRange range(bool has_after, uint32_t after, size_t limit) const {
        RemoteLogRange result;
        result.newest = sequence_;
        result.oldest = count_ ? sequence_ - uint32_t(count_) + 1 : 0;
        result.cursor = has_after ? after : sequence_;
        if (!count_) return result;
        uint32_t remaining = has_after ? sequence_ - after : uint32_t(count_);
        if (has_after && remaining > INT32_MAX) {
            result.reset = true;
            remaining = uint32_t(count_);
        }
        if (remaining > count_) {
            result.missed = remaining - uint32_t(count_);
            remaining = uint32_t(count_);
        }
        result.cursor = sequence_ - remaining;
        const size_t cap = limit < REMOTE_LOG_RESPONSE_RECORDS ? limit : REMOTE_LOG_RESPONSE_RECORDS;
        result.count = remaining < cap ? remaining : cap;
        result.has_more = remaining > result.count;
        return result;
    }

    bool get(uint32_t sequence, RemoteLogRecord& output) const {
        const uint32_t distance = sequence_ - sequence;
        if (distance >= count_) return false;
        const size_t index = (head_ + capacity_ - 1 - distance) % capacity_;
        output = records_[index];
        return output.sequence == sequence;
    }

    size_t count() const { return count_; }

private:
    RemoteLogRecord* records_ = nullptr;
    size_t capacity_ = 0;
    size_t count_ = 0;
    size_t head_ = 0;
    uint32_t sequence_ = 0;
};

struct RemoteLogSnapshot {
    RemoteLogRange range;
    uint32_t boot_id;
    uint32_t dropped;
    size_t capacity;
    size_t count;
    bool boot_complete;
    bool boot_truncated;
    RemoteLogRecord records[REMOTE_LOG_RESPONSE_RECORDS];
    alignas(max_align_t) mutable uint8_t json_storage[8192];
};

void remote_log_init();
bool remote_log_available();
void remote_log_append(const char* line);
void remote_log_finish_boot();
const RemoteLogSnapshot* remote_log_snapshot(bool boot, bool has_after, uint32_t after, size_t limit);
void remote_log_release_snapshot();