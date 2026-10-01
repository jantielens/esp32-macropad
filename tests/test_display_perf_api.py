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
    assert(doc["display_perf"].as<JsonObject>().size() == 10);
    assert(doc["display_perf"]["cycle_us"].as<int>() == 400);
    assert(doc["display_perf"]["cycle_peak_us"].as<int>() == 800);
    doc.clear();
    fill(doc, false);
    assert(doc.size() == 0);
    ready = false;
    fill(doc, true);
    assert(doc["display_fps"].isNull() && doc["display_perf"].isNull());
    ready = true;
    displayManager = false;
    fill(doc, true);
    assert(doc["display_perf"].isNull());
}
'''
with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "api.cpp"
    executable = pathlib.Path(directory) / "api"
    test_source.write_text(harness)
    libraries = pathlib.Path(os.environ.get("HOME", "")) / "Arduino/libraries/ArduinoJson/src"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(libraries), "-I" + str(root / "src/app"),
                    str(test_source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: display performance API fields and MQTT exclusion")