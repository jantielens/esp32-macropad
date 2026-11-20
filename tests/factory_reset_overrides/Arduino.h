#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

class String {
public:
    String() = default;
    String(const char* value) : value_(value ? value : "") {}

    bool endsWith(const char* suffix) const {
        const std::string suffix_value(suffix ? suffix : "");
        return value_.size() >= suffix_value.size() &&
            value_.compare(value_.size() - suffix_value.size(), suffix_value.size(), suffix_value) == 0;
    }
    String& operator+=(const char* value) {
        value_ += value ? value : "";
        return *this;
    }
    const char* c_str() const { return value_.c_str(); }
    size_t length() const { return value_.size(); }
    bool concat(const char* value) {
        value_ += value;
        return true;
    }

private:
    std::string value_;
};

class EspTestDouble {
public:
    uint64_t getEfuseMac() const { return 0; }
};

extern EspTestDouble ESP;

unsigned long millis();