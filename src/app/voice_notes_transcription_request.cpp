#include "board_config.h"

#if HAS_VOICE_NOTES
#include "voice_notes_transcription_request.h"
#include "bounded_psram_json_allocator.h"
#include "net_activity.h"
#include "time_service.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <limits.h>
#include <string.h>
#include <time.h>

namespace {
constexpr uint32_t kPsramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr uint32_t kTimeoutMs = 15000;
constexpr char kBoundary[] = "esp32-voice-note-boundary";
constexpr char kPrefix[] =
    "--esp32-voice-note-boundary\r\n"
    "Content-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
    "--esp32-voice-note-boundary\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n";
constexpr char kSuffix[] = "\r\n--esp32-voice-note-boundary--\r\n";

class MultipartStream : public Stream {
public:
    MultipartStream(File& file, const char* prefix, size_t prefix_size, uint32_t epoch, uint64_t generation)
        : file_(file), prefix_(prefix), prefix_size_(prefix_size),
          file_size_(file.size()), epoch_(epoch), generation_(generation) {}

    size_t size() const { return prefix_size_ + file_size_ + sizeof(kSuffix) - 1; }
    bool failed() const { return failed_; }
    int available() override {
        if (failed_ || !voice_notes_transcription_checkpoint(epoch_, generation_)) {
            failed_ = true;
            return -1;
        }
        const size_t remaining = size() - position_;
        return remaining ? static_cast<int>(remaining) : -1;
    }
    int read() override {
        uint8_t c;
        return readBytes(reinterpret_cast<char*>(&c), 1) == 1 ? c : -1;
    }
    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
    size_t readBytes(char* buffer, size_t length) override {
        if (available() < 0) return 0;
        size_t used = 0;
        while (used < length && position_ < size()) {
            if (position_ < prefix_size_) {
                const size_t count = min(length - used, prefix_size_ - position_);
                memcpy(buffer + used, prefix_ + position_, count);
                position_ += count;
                used += count;
            } else if (position_ < prefix_size_ + file_size_) {
                const size_t count = min(length - used, prefix_size_ + file_size_ - position_);
                const size_t read = file_.read(reinterpret_cast<uint8_t*>(buffer + used), count);
                position_ += read;
                used += read;
                if (read) net_activity_mark(NET_CH_HTTP);
                if (read != count) { failed_ = true; break; }
            } else {
                const size_t offset = position_ - prefix_size_ - file_size_;
                const size_t count = min(length - used, sizeof(kSuffix) - 1 - offset);
                memcpy(buffer + used, kSuffix + offset, count);
                position_ += count;
                used += count;
            }
        }
        return used;
    }
private:
    File& file_;
    const char* prefix_;
    size_t prefix_size_;
    size_t file_size_;
    uint32_t epoch_;
    uint64_t generation_;
    size_t position_ = 0;
    bool failed_ = false;
};

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
            if (!next) { failed_ = true; setWriteError(); return 0; }
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
    void clear() { heap_caps_free(data_); data_ = nullptr; size_ = capacity_ = 0; }
private:
    char* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    uint32_t epoch_;
    uint64_t generation_;
    bool failed_ = false;
};

bool append_encoded(String& url, const char* input) {
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(input); *p; ++p) {
        const unsigned char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            if (!url.concat(static_cast<char>(c))) return false;
        } else {
            char encoded[] = {'%', hex[c >> 4], hex[c & 15], '\0'};
            if (!url.concat(encoded)) return false;
        }
    }
    return true;
}

bool parse_duration(const char* text, uint64_t multiplier, uint64_t& duration) {
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

bool utc_seconds(const struct tm& value, uint64_t& seconds) {
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

uint64_t retry_after_ms(HTTPClient& http) {
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

bool voice_notes_transcription_request(const VoiceNotesTranscriptionConfig& config, File& recording,
                                       uint32_t ota_epoch, uint64_t generation,
                                       VoiceNotesRequestResult& result) {
    result = {};
    const size_t prefix_capacity = sizeof(kPrefix) + strlen(config.model);
    auto* prefix = static_cast<char*>(heap_caps_malloc(prefix_capacity, kPsramCaps));
    if (!prefix) { result.error = "Request allocation failed"; return false; }
    const int prefix_length = snprintf(prefix, prefix_capacity, kPrefix, config.model);
    if (prefix_length < 0 || static_cast<size_t>(prefix_length) >= prefix_capacity ||
        recording.size() > static_cast<size_t>(INT_MAX) - static_cast<size_t>(prefix_length) - sizeof(kSuffix)) {
        heap_caps_free(prefix);
        result.error = "Recording is too large for HTTP transport";
        result.outcome = VoiceNotesRequestOutcome::NoteFailure;
        return false;
    }
    MultipartStream upload(recording, prefix, static_cast<size_t>(prefix_length), ota_epoch, generation);
    String url;
    if (!url.reserve(strlen(config.base_url) + strlen(config.model) * 3 + 80) ||
        !url.concat(config.base_url) || !url.concat("openai/deployments/") ||
        !append_encoded(url, config.model) ||
        !url.concat("/audio/transcriptions?api-version=2025-03-01-preview")) {
        heap_caps_free(prefix);
        result.error = "Request URL allocation failed";
        return false;
    }
    WiFiClientSecure client;
    client.useBuiltinCACertBundle();
    client.setHandshakeTimeout(kTimeoutMs / 1000);
    client.setTimeout(kTimeoutMs);
    client.setConnectionTimeout(kTimeoutMs);
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(kTimeoutMs);
    http.setTimeout(kTimeoutMs);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char* response_headers[] = {"Transfer-Encoding", "Retry-After", "retry-after-ms"};
    http.collectHeaders(response_headers, 3);
    if (!http.begin(client, url)) {
        heap_caps_free(prefix);
        result.error = "Invalid transcription endpoint";
        return false;
    }
    http.addHeader("api-key", config.api_key);
    http.addHeader("Content-Type", String("multipart/form-data; boundary=") + kBoundary);
    const auto close = [&http, &client]() { http.end(); client.stop(); delay(100); };
    result.status = http.sendRequest("POST", &upload, upload.size());
    net_activity_mark(NET_CH_HTTP);
    heap_caps_free(prefix);
    if (!voice_notes_transcription_checkpoint(ota_epoch, generation)) {
        close();
        result.error = "Recording or update interrupted transcription";
        result.outcome = VoiceNotesRequestOutcome::Interrupted;
        return false;
    }
    if (upload.failed()) {
        close();
        result.error = "Recording upload failed";
        return false;
    }
    if (result.status != HTTP_CODE_OK) {
        if (result.status == 429) result.cooldown_ms = retry_after_ms(http);
        close();
        switch (result.status) {
            case 401: case 403:
                result.outcome = VoiceNotesRequestOutcome::SetupFailure;
                result.error = "Azure access denied; check the API key and resource access in Voice Notes Settings";
                break;
            case 404:
                result.outcome = VoiceNotesRequestOutcome::SetupFailure;
                result.error = "Azure deployment not found; check the resource endpoint and deployment in Voice Notes Settings";
                break;
            case 413:
                result.outcome = VoiceNotesRequestOutcome::NoteFailure;
                result.error = "Recording exceeds the transcription service upload limit";
                break;
            case 429:
                result.outcome = VoiceNotesRequestOutcome::RateLimited;
                result.error = "Azure rate or quota limit reached; check quota or retry later";
                break;
            default:
                if (result.status == 400 || result.status == 415 || result.status == 422)
                    result.outcome = VoiceNotesRequestOutcome::NoteFailure;
                result.error = result.status > 0 ? "Transcription service rejected request; check the serial log for HTTP status"
                                                : "Transcription connection failed; check connectivity and retry";
                break;
        }
        return false;
    }
    if (http.getSize() < 0 && !http.header("Transfer-Encoding").equalsIgnoreCase("chunked")) {
        close();
        result.error = "Unsupported transcription response framing";
        return false;
    }
    ResponseStream body(ota_epoch, generation);
    const int received = http.writeToStream(&body);
    close();
    if (!voice_notes_transcription_checkpoint(ota_epoch, generation)) {
        result.outcome = VoiceNotesRequestOutcome::Interrupted;
        result.error = "Recording or update interrupted transcription";
        return false;
    }
    if (received < 0 || body.failed() || !body.data()) {
        result.error = "Transcription response could not be read";
        return false;
    }
    BoundedPsramJsonAllocator allocator(heap_caps_get_free_size(kPsramCaps));
    JsonDocument document(&allocator);
    const DeserializationError parse_error = deserializeJson(document, body.data(), body.size());
    body.clear();
    if (parse_error || document.overflowed() || !document["text"].is<const char*>()) {
        result.error = "Invalid transcription response";
        return false;
    }
    const char* text = document["text"];
    const size_t length = strlen(text);
    if (!length) { result.outcome = VoiceNotesRequestOutcome::NoteFailure; result.error = "No speech detected"; return false; }
    result.text = static_cast<char*>(heap_caps_malloc(length + 1, kPsramCaps));
    if (!result.text) { result.error = "Transcript allocation failed"; return false; }
    memcpy(result.text, text, length + 1);
    result.outcome = VoiceNotesRequestOutcome::Success;
    return true;
}
#endif
