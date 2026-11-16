#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include "voice_notes_transcription_request.h"
#include "net_activity.h"
#include "time_service.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <time.h>

namespace voice_notes_transport {
constexpr uint32_t kPsramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr uint32_t kTimeoutMs = 15000;

class ResponseStream : public Stream {
public:
    ResponseStream(uint32_t epoch, uint64_t generation) : epoch_(epoch), generation_(generation) {}
    ~ResponseStream() { heap_caps_free(data_); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* data, size_t length) override {
        if (!length || failed_ || !voice_notes_transcription_checkpoint(epoch_, generation_) ||
            size_ == SIZE_MAX || length > SIZE_MAX - size_ - 1) {
            failed_ = true;
            setWriteError();
            return 0;
        }
        const size_t needed = size_ + length + 1;
        if (needed > capacity_) {
            size_t capacity = capacity_ ? capacity_ : 1024;
            while (capacity < needed) {
                if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
                capacity *= 2;
            }
            auto* next = static_cast<char*>(heap_caps_realloc(data_, capacity, kPsramCaps));
            if (!next && capacity != needed) {
                capacity = needed;
                next = static_cast<char*>(heap_caps_realloc(data_, capacity, kPsramCaps));
            }
            if (!next) { failed_ = true; out_of_memory_ = true; setWriteError(); return 0; }
            data_ = next;
            capacity_ = capacity;
        }
        memcpy(data_ + size_, data, length);
        size_ += length;
        data_[size_] = '\0';
        net_activity_mark(NET_CH_HTTP);
        return length;
    }
    const char* data() const { return data_; }
    size_t size() const { return size_; }
    bool failed() const { return failed_; }
    bool out_of_memory() const { return out_of_memory_; }
    void clear() { heap_caps_free(data_); data_ = nullptr; size_ = capacity_ = 0; }
private:
    char* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    uint32_t epoch_;
    uint64_t generation_;
    bool failed_ = false;
    bool out_of_memory_ = false;
};

inline bool begin(HTTPClient& http, WiFiClientSecure& client, const char* url, const char* key) {
    client.useBuiltinCACertBundle();
    client.setHandshakeTimeout(kTimeoutMs / 1000);
    client.setTimeout(kTimeoutMs);
    client.setConnectionTimeout(kTimeoutMs);
    http.setReuse(false);
    http.setConnectTimeout(kTimeoutMs);
    http.setTimeout(kTimeoutMs);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char* headers[] = {"Transfer-Encoding", "Retry-After", "retry-after-ms"};
    http.collectHeaders(headers, 3);
    if (!http.begin(client, url)) return false;
    http.addHeader("api-key", key);
    return true;
}

inline void close(HTTPClient& http, WiFiClientSecure& client) {
    http.end();
    client.stop();
    delay(100);
}

inline bool parse_duration(const char* text, uint64_t multiplier, uint64_t& duration) {
    if (!text || !*text) return false;
    uint64_t value = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (UINT64_MAX - (*p - '0')) / 10) return false;
        value = value * 10 + (*p - '0');
    }
    if (value > UINT64_MAX / multiplier) return false;
    duration = value * multiplier;
    return true;
}

inline bool utc_seconds(const struct tm& value, uint64_t& seconds) {
    int year = value.tm_year + 1900;
    const unsigned month = static_cast<unsigned>(value.tm_mon + 1);
    const unsigned day = static_cast<unsigned>(value.tm_mday);
    if (year < 1970 || month < 1 || month > 12 || day < 1 ||
        value.tm_hour < 0 || value.tm_hour > 23 || value.tm_min < 0 ||
        value.tm_min > 59 || value.tm_sec < 0 || value.tm_sec > 59) return false;
    static const unsigned days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > days_per_month[month - 1] + (month == 2 && leap ? 1U : 0U)) return false;
    year -= month <= 2;
    const int era = year / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned adjusted_month = month > 2 ? month - 3 : month + 9;
    const unsigned day_of_year = (153 * adjusted_month + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const uint64_t days = static_cast<uint64_t>(era * 146097 + static_cast<int>(day_of_era) - 719468);
    seconds = days * 86400 + value.tm_hour * 3600 + value.tm_min * 60 + value.tm_sec;
    return true;
}

inline uint64_t retry_after_ms(HTTPClient& http) {
    uint64_t seconds = 0, milliseconds = 0;
    const String retry = http.header("Retry-After");
    if (!parse_duration(retry.c_str(), 1000, seconds) && time_service_ready()) {
        struct tm parsed = {};
        char* end = strptime(retry.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &parsed);
        if (end && !*end) {
            uint64_t deadline = 0;
            const time_t now = time(nullptr);
            if (now >= 0 && utc_seconds(parsed, deadline) && deadline > static_cast<uint64_t>(now) &&
                deadline - static_cast<uint64_t>(now) <= UINT64_MAX / 1000)
                seconds = (deadline - static_cast<uint64_t>(now)) * 1000;
        }
    }
    parse_duration(http.header("retry-after-ms").c_str(), 1, milliseconds);
    return seconds > milliseconds ? seconds : milliseconds;
}
}
#endif
