// ============================================================================
// Test override: Arduino.h — minimal String class + millis() for session test
// ============================================================================
#ifndef ARDUINO_H
#define ARDUINO_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <string>

inline unsigned long millis() {
    static unsigned long t = 0;
    return ++t;  // monotonic, value irrelevant for these tests
}

// Minimal Arduino-compatible String backed by std::string.
class String {
public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(int v) { char b[16]; snprintf(b, sizeof(b), "%d", v); s_ = b; }
    String(unsigned v) { char b[16]; snprintf(b, sizeof(b), "%u", v); s_ = b; }

    const char* c_str() const { return s_.c_str(); }
    size_t length() const { return s_.length(); }
    bool endsWith(const char* suf) const {
        size_t n = strlen(suf);
        return s_.length() >= n && s_.compare(s_.length() - n, n, suf) == 0;
    }
    void remove(size_t pos) { if (pos < s_.size()) s_.erase(pos); }
    String operator+(const String& o) const { return String(s_ + o.s_); }
    String operator+(const char* o) const { return String(s_ + o); }
    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(const char* o) { s_ += o; return *this; }
    bool operator==(const char* o) const { return s_ == o; }

    // ArduinoJson serializer requirements:
    size_t write(uint8_t c) { s_.push_back((char)c); return 1; }
    size_t write(const uint8_t* buf, size_t n) {
        if (buf && n) s_.append(reinterpret_cast<const char*>(buf), n);
        return n;
    }

private:
    std::string s_;
};

inline String operator+(const char* lhs, const String& rhs) {
    return String(lhs) + rhs;
}

// glibc lacks strlcpy; provide BSD-style truncating copy.
extern "C" inline size_t strlcpy(char* dst, const char* src, size_t dst_size) {
    size_t src_len = src ? strlen(src) : 0;
    if (dst_size > 0) {
        size_t copy_len = (src_len >= dst_size) ? dst_size - 1 : src_len;
        if (copy_len && src) memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}

#endif // ARDUINO_H
