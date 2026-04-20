#pragma once

// Shared numeric rocker zone computation — included by both
// numericrocker_widget.cpp and pad_screen_events.cpp.

// Zone computation constants
#define NR_OUTER_PCT   12   // target 12% of span for outer zones
#define NR_INNER_PCT   15   // target 15% of span for inner zones
#define NR_ZONE_MIN_PX 40   // minimum zone width/height in pixels
#define NR_ZONE_MAX_PX 80   // maximum zone width/height in pixels

// Computed zone boundaries in pixels from start of tile span.
struct NRZoneLayout {
    int outer_end;   // end of outer-dec zone (= start of inner-dec)
    int inner_end;   // end of inner-dec zone (= start of dead zone)
    int inner2_start; // start of inner-inc zone (= end of dead zone)
    int outer2_start; // start of outer-inc zone (= end of inner-inc)
    // outer-inc runs from outer2_start to span
};

static inline int nr_clamp(int val, int lo, int hi) {
    return val < lo ? lo : (val > hi ? hi : val);
}

// Compute zone boundaries from the tile span (width for horizontal, height for vertical).
// When a step is 0, its zone is disabled and its percentage is given to the other zone type.
// The center dead zone gets at least 10% of span, or whatever remains after clamping.
static inline NRZoneLayout nr_compute_zones(int span, float small_step, float large_step) {
    bool has_outer = (large_step > 0);
    bool has_inner = (small_step > 0);
    int total_pct = (has_outer ? NR_OUTER_PCT : 0) + (has_inner ? NR_INNER_PCT : 0);

    // When one zone is disabled, the other gets the full combined percentage
    int outer_target = has_outer ? (has_inner ? NR_OUTER_PCT : total_pct) : 0;
    int inner_target = has_inner ? (has_outer ? NR_INNER_PCT : total_pct) : 0;

    int outer_px = has_outer ? nr_clamp(span * outer_target / 100, NR_ZONE_MIN_PX, NR_ZONE_MAX_PX) : 0;
    int inner_px = has_inner ? nr_clamp(span * inner_target / 100, NR_ZONE_MIN_PX, NR_ZONE_MAX_PX) : 0;

    // Ensure zones don't exceed available space (leave at least 10% center)
    int min_center = span / 10;
    int total_sides = 2 * (outer_px + inner_px);
    if (total_sides + min_center > span) {
        int avail = span - min_center;
        if (avail < 4) avail = 4;
        int denom = 2 * (outer_target + inner_target);
        if (denom < 1) denom = 1;
        outer_px = has_outer ? avail * outer_target / denom : 0;
        inner_px = has_inner ? avail * inner_target / denom : 0;
        if (has_outer && outer_px < 1) outer_px = 1;
        if (has_inner && inner_px < 1) inner_px = 1;
    }

    NRZoneLayout z;
    z.outer_end    = outer_px;
    z.inner_end    = outer_px + inner_px;
    z.inner2_start = span - outer_px - inner_px;
    z.outer2_start = span - outer_px;
    return z;
}
