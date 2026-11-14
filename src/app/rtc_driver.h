#pragma once

#include <time.h>

enum class RtcReadResult {
    Valid,
    InvalidTime,
    IoError
};

class RtcDriver {
public:
    virtual ~RtcDriver() = default;
    virtual bool begin() = 0;
    virtual RtcReadResult readUtc(time_t& epoch) = 0;
    virtual bool writeUtc(time_t epoch) = 0;
};

// Static driver lifetime; the caller does not own the returned instance.
RtcDriver* rtc_driver_create();
