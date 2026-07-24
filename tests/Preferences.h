#ifndef TEST_PREFERENCES_H
#define TEST_PREFERENCES_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Preferences {
public:
    size_t getBytesLength(const char *) const { return length_; }

    size_t getBytes(const char *, void *output, size_t length) const {
        if (length != length_) return 0;
        memcpy(output, storage_, length);
        return length;
    }

    size_t putBytes(const char *, const void *input, size_t length) {
        if (length > sizeof(storage_)) return 0;
        memcpy(storage_, input, length);
        length_ = length;
        return length;
    }

private:
    uint8_t storage_[1024] = {};
    size_t length_ = 0;
};

#endif
