#pragma once

#include <stdint.h>

struct MouseTraceDelayedAck {
    uint16_t target_us = 0;
    uint32_t elapsed_us = 0;
    uint32_t read_us = 0;
    uint8_t status = 0;
    uint8_t status_errors = 0;
    uint8_t report_errors = 0;
    uint8_t report[8]{};
    bool report_valid = false;
};

struct MouseTraceAckSchedule {
    static constexpr uint16_t delays_us[] = {1000, 3000, 8000};
    static constexpr uint8_t delay_count = sizeof(delays_us) / sizeof(delays_us[0]);
    static constexpr uint32_t spacing_us = 100000;
    uint32_t last_at_us = 0;
    uint8_t next_delay = 0;
    bool started = false;

    uint16_t take_delay_us(uint32_t now_us, bool active) {
        if (!active) {
            *this = {};
            return 0;
        }
        if (started && now_us - last_at_us < spacing_us) return 0;
        started = true;
        last_at_us = now_us;
        const uint16_t delay_us = delays_us[next_delay];
        next_delay = (next_delay + 1) % delay_count;
        return delay_us;
    }
};

struct MouseTraceCadence {
    uint32_t count = 0;
    uint32_t last_at = 0;
    uint32_t max_gap = 0;

    void reset(uint32_t now) {
        *this = {};
        last_at = now;
    }

    void observe(uint32_t now) {
        const uint32_t gap = now - last_at;
        if (gap > max_gap) max_gap = gap;
        last_at = now;
        ++count;
    }
};

struct MouseTraceAckStats {
    uint32_t cleared = 0;
    uint32_t still_ready = 0;
    uint32_t errors = 0;
    uint32_t max_probe_us = 0;

    void observe(bool checked, uint8_t status, uint8_t read_errors, uint32_t probe_us) {
        if (!checked) return;
        if (probe_us > max_probe_us) max_probe_us = probe_us;
        if (read_errors) ++errors;
        else if (status & 0x80) ++still_ready;
        else ++cleared;
    }
};

struct MouseTraceInputStats {
    MouseTraceCadence callbacks;
    MouseTraceCadence before_change;
    uint32_t started = 0;
    uint32_t first_change_ms = 0;
    uint32_t first_output_ms = 0;
    uint32_t changed_without_output = 0;
    int last_x = 0;
    int last_y = 0;
    bool change_seen = false;
    bool output_seen = false;

    void reset(uint32_t now, int x, int y) {
        *this = {};
        started = now;
        last_x = x;
        last_y = y;
        callbacks.reset(now);
        before_change.reset(now);
    }

    void observe(uint32_t now, int x, int y, int dx, int dy) {
        callbacks.observe(now);
        if (!change_seen) before_change.observe(now);
        const bool changed = x != last_x || y != last_y;
        if (changed && !change_seen) {
            change_seen = true;
            first_change_ms = now - started;
        }
        if ((dx || dy) && !output_seen) {
            output_seen = true;
            first_output_ms = now - started;
        }
        if (changed && !dx && !dy) ++changed_without_output;
        last_x = x;
        last_y = y;
    }
};