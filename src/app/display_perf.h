#ifndef DISPLAY_PERF_H
#define DISPLAY_PERF_H

#include <stdint.h>

#if HAS_DISPLAY_DIAGNOSTICS
struct DisplayFlushStats {
    uint32_t calls = 0;
    uint32_t pixels = 0;
    uint32_t total_us = 0;
    uint32_t peak_us = 0;

    void add(uint32_t pixel_count, uint32_t duration_us) {
        ++calls;
        pixels += pixel_count;
        total_us += duration_us;
        if (duration_us > peak_us) peak_us = duration_us;
    }
};

enum class DisplayInvalidationSource : uint8_t {
    Other, Deferred, ScreenSwitch, Timer, ScreenUpdate, Count
};

struct DisplayGlyphStats {
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t evictions = 0;
    uint64_t failures = 0;
    uint64_t miss_us = 0;

    void add(const DisplayGlyphStats& sample) {
        hits += sample.hits;
        misses += sample.misses;
        evictions += sample.evictions;
        failures += sample.failures;
        miss_us += sample.miss_us;
    }

    DisplayGlyphStats since(const DisplayGlyphStats& previous) const {
        return {hits - previous.hits, misses - previous.misses,
                evictions - previous.evictions, failures - previous.failures,
                miss_us - previous.miss_us};
    }
};

struct DisplayInvalidationStats {
    uint64_t calls[static_cast<unsigned>(DisplayInvalidationSource::Count)] = {};
    uint64_t requested_pixels = 0;

    void add(uint32_t width, uint32_t height, DisplayInvalidationSource source) {
        ++calls[static_cast<unsigned>(source)];
        requested_pixels += uint64_t(width) * height;
    }

    void add(const DisplayInvalidationStats& sample) {
        for (unsigned i = 0; i < static_cast<unsigned>(DisplayInvalidationSource::Count); ++i)
            calls[i] += sample.calls[i];
        requested_pixels += sample.requested_pixels;
    }
};

struct DisplayRefreshStats {
    uint32_t refresh_calls = 0;
    uint32_t render_calls = 0;
    uint32_t refresh_us = 0;
    uint32_t render_us = 0;
    uint32_t render_flush_us = 0;
    uint64_t render_glyph_miss_us = 0;
    DisplayInvalidationStats invalidations;
};

struct DisplayRefreshTrace {
    DisplayRefreshStats stats;
    uint64_t refresh_start_us = 0;
    uint64_t render_start_us = 0;
    uint32_t flush_start_us = 0;
    uint64_t glyph_miss_start_us = 0;
    bool refreshing = false;
    bool rendering = false;

    void beginRefresh(uint64_t now_us) {
        refresh_start_us = now_us;
        refreshing = true;
    }

    void endRefresh(uint64_t now_us) {
        if (!refreshing) return;
        stats.refresh_us += static_cast<uint32_t>(now_us - refresh_start_us);
        ++stats.refresh_calls;
        refreshing = false;
    }

    void beginRender(uint64_t now_us, uint32_t flush_us, uint64_t glyph_miss_us = 0) {
        render_start_us = now_us;
        flush_start_us = flush_us;
        glyph_miss_start_us = glyph_miss_us;
        rendering = true;
    }

    void endRender(uint64_t now_us, uint32_t flush_us, uint64_t glyph_miss_us = 0) {
        if (!rendering) return;
        stats.render_us += static_cast<uint32_t>(now_us - render_start_us);
        stats.render_flush_us += flush_us - flush_start_us;
        stats.render_glyph_miss_us += glyph_miss_us - glyph_miss_start_us;
        ++stats.render_calls;
        rendering = false;
    }
};

struct DisplayRenderSample {
    uint32_t completed_ms = 0;
    uint32_t lock_wait_us = 0;
    uint32_t deferred_us = 0;
    uint32_t screen_switch_us = 0;
    uint32_t lv_timer_us = 0;
    uint32_t lv_timer_flush_us = 0;
    uint32_t data_stream_us = 0;
    uint32_t screen_update_us = 0;
    uint32_t cycle_us = 0;
    DisplayFlushStats flush;
    bool screen_switched = false;
    DisplayRefreshStats refresh;
    DisplayGlyphStats glyphs;
};

struct DisplayRenderHistory {
    uint32_t cycles = 0;
    DisplayRenderSample last;
    DisplayRenderSample slowest;
    uint32_t navigations = 0;
    DisplayRenderSample last_navigation;
    DisplayInvalidationStats invalidations;
    DisplayGlyphStats glyphs;

    void add(const DisplayRenderSample& sample) {
        invalidations.add(sample.refresh.invalidations);
        glyphs.add(sample.glyphs);
        if (!sample.flush.calls) return;
        ++cycles;
        last = sample;
        if (cycles == 1 || sample.cycle_us > slowest.cycle_us) slowest = sample;
        if (sample.screen_switched) {
            ++navigations;
            last_navigation = sample;
        }
    }
};

struct DisplayPresentationSample {
    uint32_t completed_ms = 0;
    uint32_t total_us = 0;
    uint32_t dirty_to_ready_us = 0;
    uint32_t coalesce_us = 0;
    uint32_t framebuffer_wait_us = 0;
    uint32_t compare_us = 0;
    uint32_t copy_us = 0;
    uint32_t upload_us = 0;
    uint32_t controller_us = 0;
    uint32_t hrdy_wait_us = 0;
    uint32_t uploaded_bytes = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t waveform = 0;
    bool full = false;
    bool success = false;
};

struct DisplayPresentationStats {
    uint32_t attempts = 0;
    uint32_t failures = 0;
    uint32_t unchanged_skips = 0;
    uint32_t ota_deferrals = 0;
    uint32_t full_updates = 0;
    uint32_t partial_updates = 0;
    uint64_t uploaded_bytes = 0;
    uint64_t flush_calls = 0;
    uint64_t flush_pixels = 0;
    uint64_t conversion_us = 0;
    uint64_t yield_us = 0;
    uint64_t framebuffer_wait_us = 0;
    DisplayPresentationSample last;
    DisplayPresentationSample slowest;

    void add(const DisplayPresentationSample& sample) {
        ++attempts;
        if (!sample.success) ++failures;
        else if (sample.full) ++full_updates;
        else ++partial_updates;
        uploaded_bytes += sample.uploaded_bytes;
        last = sample;
        if (attempts == 1 || sample.total_us > slowest.total_us) slowest = sample;
    }
};

#endif // HAS_DISPLAY_DIAGNOSTICS

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
#if HAS_DISPLAY_DIAGNOSTICS
    DisplayRenderHistory render;
#endif
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
#if HAS_DISPLAY_DIAGNOSTICS
    DisplayRenderHistory render;
#endif

    void reset(uint32_t now_ms) {
        start_ms = now_ms;
        frames = 0;
        lv_timer = {};
        present = {};
        data_stream = {};
        screen_update = {};
        cycle = {};
    }

    bool publish(uint32_t now_ms, DisplayPerfStats& stats) {
        const uint32_t elapsed_ms = now_ms - start_ms;
        if (elapsed_ms < 1000) return false;
        const uint64_t fps = (static_cast<uint64_t>(frames) * 1000 + elapsed_ms / 2) / elapsed_ms;
        stats = {
            static_cast<uint16_t>(fps > UINT16_MAX ? UINT16_MAX : fps),
            lv_timer.mean(), present.mean(), data_stream.mean(), screen_update.mean(), cycle.mean(),
            lv_timer.peak_us, present.peak_us, data_stream.peak_us, screen_update.peak_us, cycle.peak_us
#if HAS_DISPLAY_DIAGNOSTICS
            , render
#endif
        };
        reset(now_ms);
        return true;
    }
};

#endif