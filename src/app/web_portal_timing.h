#pragma once

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

class WebPortalTiming {
public:
    WebPortalTiming() : started_us_(micros()), phase_us_(started_us_) {}

    void mark(const char *name) {
        const uint32_t now_us = micros();
        append(name, now_us - phase_us_, kTotalReserve);
        phase_us_ = now_us;
    }

    template <typename Response>
    void attach(Response *response, const char *total_name = "total") {
        if (!response) return;
        append(total_name, static_cast<uint32_t>(micros()) - started_us_, 0);
        response->addHeader("Server-Timing", header_);
    }

private:
    static constexpr size_t kHeaderCapacity = 384;
    static constexpr size_t kTotalReserve = 48;
    uint32_t started_us_;
    uint32_t phase_us_;
    size_t length_ = 0;
    char header_[kHeaderCapacity] = {};

    void append(const char *name, uint32_t duration_us, size_t reserve) {
        if (!name || !*name) return;
        if (length_ + reserve >= sizeof(header_)) return;
        for (const char *cursor = name; *cursor; ++cursor) {
            const char character = *cursor;
            if (!((character >= 'a' && character <= 'z') ||
                  (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') ||
                  character == '_' || character == '-')) return;
        }
        const size_t available = sizeof(header_) - length_ - reserve;
        const int written = snprintf(header_ + length_, available, "%s%s;dur=%lu.%03lu",
            length_ ? ", " : "", name,
            static_cast<unsigned long>(duration_us / 1000),
            static_cast<unsigned long>(duration_us % 1000));
        if (written < 0 || static_cast<size_t>(written) >= available) {
            header_[length_] = '\0';
            return;
        }
        length_ += static_cast<size_t>(written);
    }
};