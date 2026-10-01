#ifndef DISPLAY_PERF_H
#define DISPLAY_PERF_H

#include <stdint.h>

struct DisplayPerfStats {
    uint16_t fps;
    uint32_t lv_timer_us;
    uint32_t present_us;
    uint32_t data_stream_us;
    uint32_t screen_update_us;
    uint32_t cycle_us;
    uint32_t lv_timer_peak_us;
    uint32_t present_peak_us;
    uint32_t data_stream_peak_us;
    uint32_t screen_update_peak_us;
    uint32_t cycle_peak_us;
};

struct DisplayTimingWindow {
    uint64_t total_us = 0;
    uint32_t count = 0;
    uint32_t peak_us = 0;

    void add(uint32_t duration_us) {
        total_us += duration_us;
        ++count;
        if (duration_us > peak_us) peak_us = duration_us;
    }

    uint32_t mean() const {
        return count ? static_cast<uint32_t>(total_us / count) : 0;
    }
};

struct DisplayPerfWindow {
    uint32_t start_ms = 0;
    uint32_t frames = 0;
    DisplayTimingWindow lv_timer;
    DisplayTimingWindow present;
    DisplayTimingWindow data_stream;
    DisplayTimingWindow screen_update;
    DisplayTimingWindow cycle;

    void reset(uint32_t now_ms) {
        *this = {};
        start_ms = now_ms;
    }

    bool publish(uint32_t now_ms, DisplayPerfStats& stats) {
        const uint32_t elapsed_ms = now_ms - start_ms;
        if (elapsed_ms < 1000) return false;
        const uint64_t fps = (static_cast<uint64_t>(frames) * 1000 + elapsed_ms / 2) / elapsed_ms;
        stats = {
            static_cast<uint16_t>(fps > UINT16_MAX ? UINT16_MAX : fps),
            lv_timer.mean(), present.mean(), data_stream.mean(), screen_update.mean(), cycle.mean(),
            lv_timer.peak_us, present.peak_us, data_stream.peak_us, screen_update.peak_us, cycle.peak_us
        };
        reset(now_ms);
        return true;
    }
};

#endif