#pragma once

#include <stddef.h>
#include <stdint.h>

class JsonKeyValidator {
public:
    bool accept(uint8_t character) {
        if (character == 0) return false;
        if (closed_string_) {
            if (character == ' ' || character == '\t' || character == '\r' || character == '\n')
                return true;
            closed_string_ = false;
            if (character == ':' && nul_in_string_) return false;
        }
        if (!in_string_) {
            if (character == '"') {
                in_string_ = true;
                nul_in_string_ = false;
            }
            return true;
        }
        if (unicode_digits_) {
            unicode_zero_ = unicode_zero_ && character == '0';
            if (--unicode_digits_ == 0 && unicode_zero_) nul_in_string_ = true;
            return true;
        }
        if (escaped_) {
            escaped_ = false;
            if (character == 'u') {
                unicode_digits_ = 4;
                unicode_zero_ = true;
            }
        } else if (character == '\\') {
            escaped_ = true;
        } else if (character == '"') {
            in_string_ = false;
            closed_string_ = true;
        }
        return true;
    }

private:
    bool in_string_ = false;
    bool escaped_ = false;
    bool closed_string_ = false;
    bool nul_in_string_ = false;
    bool unicode_zero_ = false;
    uint8_t unicode_digits_ = 0;
};

inline bool json_keys_valid(const uint8_t* data, size_t size) {
    JsonKeyValidator validator;
    for (size_t index = 0; index < size; ++index)
        if (!validator.accept(data[index])) return false;
    return true;
}

enum class JsonKeyScanResult { Valid, InvalidNul, ReadFailure };

template<typename Stream>
JsonKeyScanResult json_stream_scan_keys(Stream& stream) {
    JsonKeyValidator validator;
    uint8_t buffer[256];
    size_t remaining = stream.size();
    while (remaining) {
        const size_t count = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t read = stream.read(buffer, count);
        if (!read || read > count) return JsonKeyScanResult::ReadFailure;
        for (size_t index = 0; index < read; ++index)
            if (!validator.accept(buffer[index])) return JsonKeyScanResult::InvalidNul;
        remaining -= read;
    }
    return stream.seek(0) ? JsonKeyScanResult::Valid : JsonKeyScanResult::ReadFailure;
}
