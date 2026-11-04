#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

static constexpr size_t UPDATE_SIZE_UNKNOWN = static_cast<size_t>(-1);
static constexpr int U_FLASH = 0;

class TestFirmwareUpdate {
public:
    bool begin_ok = true;
    bool write_ok = true;
    bool end_ok = true;
    unsigned aborts = 0;
    unsigned writes = 0;
    std::function<void()> on_abort;

    bool begin(size_t, int) { return begin_ok; }
    size_t write(uint8_t*, size_t length) {
        ++writes;
        return write_ok ? length : 0;
    }
    bool end(bool) { return end_ok; }
    void abort() {
        if (on_abort) on_abort();
        ++aborts;
    }
    const char* errorString() const { return "test failure"; }
};

inline TestFirmwareUpdate Update;