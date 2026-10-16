#!/usr/bin/env python3
import os
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/device_telemetry.cpp").read_text()
start = source.index("\tif (include_api_only_fields)", source.index("// Display FPS"))
end = source.index("// WiFi stats", start)
block = source[start:end]
harness = r'''
#include <ArduinoJson.h>
#include "display_perf.h"
#include <cassert>
#define HAS_DISPLAY 1
static bool displayManager = true;
static bool ready = true;
static DisplayPerfStats snapshot = {};
static bool display_manager_get_perf_stats(DisplayPerfStats* out) {
    *out = snapshot;
    return ready;
}
#if HAS_DISPLAY_DIAGNOSTICS
static bool presentation_ready = true;
static DisplayPresentationStats presentation = {};
static bool display_manager_get_presentation_stats(DisplayPresentationStats* out) {
    *out = presentation;
    return presentation_ready;
}
#endif
static void fill(JsonDocument& doc, bool include_api_only_fields) {
'''
harness += block
harness += r'''
}
int main() {
    JsonDocument doc;
    snapshot.fps = 42;
    snapshot.cycle_us = 400;
    snapshot.cycle_peak_us = 800;
    fill(doc, true);
    assert(!doc.overflowed());
    assert(doc["display_fps"].as<int>() == 42);
    assert(doc["display_perf"]["last_render"].isNull());
    assert(doc["display_perf"]["last_navigation_render"].isNull());
    assert(doc["display_presentation"]["last"].isNull());
    assert(doc["display_perf"]["cycle_us"].as<int>() == 400);
    assert(doc["display_perf"]["cycle_peak_us"].as<int>() == 800);
#if HAS_DISPLAY_DIAGNOSTICS
    DisplayRenderSample render;
    render.cycle_us = 6000000;
    render.lv_timer_us = 5000000;
    render.lv_timer_flush_us = 4000000;
    render.screen_switched = true;
    render.refresh.refresh_us = 4700000;
    render.refresh.render_us = 4400000;
    render.refresh.render_flush_us = 4000000;
    render.refresh.render_glyph_miss_us = 200000;
    render.glyphs = {uint64_t(1) << 34, 50, 30, 2, 250000};
    render.refresh.invalidations.add(100, 100, DisplayInvalidationSource::Timer);
    render.flush.add(7488, 3000);
    snapshot.render.add(render);
    DisplayRenderSample idle;
    idle.refresh.invalidations.add(20, 10, DisplayInvalidationSource::ScreenUpdate);
    idle.glyphs = {3, 2, 1, 0, 500};
    snapshot.render.add(idle);
    presentation.flush_pixels = uint64_t(1) << 34;
    DisplayPresentationSample update;
    update.success = false;
    update.upload_us = 30000;
    presentation.add(update);
    fill(doc, true);
    assert(doc["display_perf"]["last_render"]["flush_pixels"].as<int>() == 7488);
    assert(doc["display_perf"]["last_render"]["lv_timer_other_us"].as<int>() == 1000000);
    assert(doc["display_perf"]["slowest_render"]["cycle_us"].as<int>() == 6000000);
    assert(doc["display_perf"]["last_render"]["render_other_us"].as<int>() == 400000);
    assert(doc["display_perf"]["last_render"]["refresh_overhead_us"].as<int>() == 300000);
    assert(doc["display_perf"]["last_render"]["timer_other_us"].as<int>() == 300000);
    assert(doc["display_perf"]["font_cache"]["hits"].as<uint64_t>() == (uint64_t(1) << 34) + 3);
    assert(doc["display_perf"]["font_cache"]["misses"].as<int>() == 52);
    assert(doc["display_perf"]["font_cache"]["evictions"].as<int>() == 31);
    assert(doc["display_perf"]["font_cache"]["failures"].as<int>() == 2);
    assert(doc["display_perf"]["font_cache"]["miss_us"].as<int>() == 250500);
    assert(doc["display_perf"]["last_navigation_render"]["font_cache"]["misses"].as<int>() == 50);
    assert(doc["display_perf"]["last_navigation_render"]["render_glyph_miss_us"].as<int>() == 200000);
    assert(doc["display_perf"]["last_navigation_render"]["screen_switched"].as<bool>());
    assert(doc["display_perf"]["invalidations"]["screen_update"].as<int>() == 1);
    assert(doc["display_perf"]["invalidations"]["requested_pixels"].as<int>() == 10200);
    assert(doc["display_perf"]["last_navigation_render"]["invalidations"]["requested_pixels"].as<int>() == 10000);
    assert(doc["display_presentation"]["flush_pixels"].as<uint64_t>() == (uint64_t(1) << 34));
    assert(doc["display_presentation"]["failures"].as<int>() == 1);
    assert(doc["display_presentation"]["last"]["upload_us"].as<int>() == 30000);
    assert(!doc["display_presentation"]["last"]["success"].as<bool>());
    assert(!doc.overflowed());
    presentation_ready = false;
    fill(doc, true);
    assert(doc["display_presentation"].isNull());
#else
    assert(doc["display_presentation"].isUnbound());
    assert(doc["display_perf"]["rendered_cycles"].isUnbound());
    assert(doc["display_perf"]["navigation_renders"].isUnbound());
    assert(doc["display_perf"]["font_cache"].isUnbound());
    assert(doc["display_perf"]["invalidations"].isUnbound());
    assert(doc["display_perf"]["last_render"].isUnbound());
    assert(doc["display_perf"]["slowest_render"].isUnbound());
    assert(doc["display_perf"]["last_navigation_render"].isUnbound());
#endif
    doc.clear();
    fill(doc, false);
    assert(doc.size() == 0);
    ready = false;
    fill(doc, true);
    assert(doc["display_fps"].isNull() && doc["display_perf"].isNull());
#if !HAS_DISPLAY_DIAGNOSTICS
    assert(doc["display_presentation"].isUnbound());
#endif
    ready = true;
    displayManager = false;
    fill(doc, true);
    assert(doc["display_perf"].isNull());
    assert(doc["display_presentation"].isNull());
#if !HAS_DISPLAY_DIAGNOSTICS
    assert(doc["display_presentation"].isUnbound());
#endif
}
'''
with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "api.cpp"
    executable = pathlib.Path(directory) / "api"
    test_source.write_text(harness)
    libraries = pathlib.Path(os.environ.get("HOME", "")) / "Arduino/libraries/ArduinoJson/src"
    for diagnostics in (0, 1):
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-DHAS_DISPLAY_DIAGNOSTICS={diagnostics}",
                        "-I" + str(libraries), "-I" + str(root / "src/app"),
                        str(test_source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print("PASS: display performance API fields, diagnostic gates, and MQTT exclusion")