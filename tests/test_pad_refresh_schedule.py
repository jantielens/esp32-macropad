#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/screens/pad_screen.cpp").read_text()
start = source.index("void PadScreen::pollLiveData(")
end = source.index("\nbool PadScreen::containsTimeBinding", start)
poll = source[start:end]

harness = r'''
#include <cassert>
#include <cstdint>
#include <sys/time.h>
#define HAS_MQTT 1
#define HAS_IMAGE_LIBRARY 1
#define HAS_IMAGE_FETCH 1
#define millis test_millis
#define gettimeofday test_gettimeofday
static uint64_t now_ms = 1000;
static bool synced = false;
static bool minute_refresh = true;
static uint32_t interval_ms = 100;
[[maybe_unused]] static uint32_t test_millis() { return static_cast<uint32_t>(now_ms); }
[[maybe_unused]] static bool time_binding_is_synced() { return synced; }
[[maybe_unused]] static int test_gettimeofday(timeval* value, void*) {
    value->tv_sec = now_ms / 1000;
    value->tv_usec = now_ms % 1000 * 1000;
    return 0;
}
struct EpaperRefreshSettings {
    uint32_t epaper_binding_refresh_interval_ms;
    bool epaper_refresh_clock_on_minute_boundary;
};
[[maybe_unused]] static EpaperRefreshSettings config_manager_get_epaper_refresh_settings() {
    return {interval_ms, minute_refresh};
}
static bool context_active = false;
static void pad_binding_set_bindings(const void* bindings, uint8_t) { context_active = bindings; }
static void mqtt_sub_store_clear_dirty() { assert(context_active); }
struct PadScreen {
    uint64_t lastPassiveBindingSlot = UINT64_MAX;
    uint64_t lastTimeBindingMinute = UINT64_MAX;
    bool clockWasSynced = false;
    bool hasTimeBinding = true;
    const void* pageBindings = this;
    uint8_t pageBindingCount = 1;
    int visibility = 0;
    int live = 0;
    int passive = 0;
    int colors = 0;
    int numbers = 0;
    int images = 0;
    void pollLiveData(bool force = false);
    void pollBtnStateBindings() { assert(context_active); ++visibility; }
    void pollMqttBindings(bool refresh) { assert(context_active); ++live; if (refresh) ++passive; }
    void pollColorBindings() { assert(context_active); ++colors; }
    void pollNumberBindings() { assert(context_active); ++numbers; }
    void pollImageFrames() { assert(!context_active); ++images; }
};
'''
harness += poll
harness += r'''
int main() {
    PadScreen pad;
    pad.pollLiveData();
    assert(pad.passive == 1 && pad.live == 1 && !context_active);
    now_ms = 1020;
    pad.pollLiveData();
#if HAS_LVGL_EPAPER
    assert(pad.live == 1 && pad.images == 1 && pad.visibility == 1);
    assert(pad.passive == 1);
#else
    assert(pad.live == 2 && pad.images == 2 && pad.visibility == 2);
    assert(pad.passive == (DISPLAY_BINDING_REFRESH_INTERVAL_MS ? 1 : 2));
#endif
    const int previous = pad.passive;
    pad.pollLiveData(true);
    assert(pad.passive == previous + 1);
    now_ms = 1100;
    pad.pollLiveData();
    assert(pad.passive == previous + 2);
    const int before_sync = pad.passive;
    synced = true;
    now_ms = 1101;
    pad.pollLiveData();
    assert(pad.passive == before_sync + 1);
    interval_ms = 60000;
    now_ms = 59999;
    pad.pollLiveData();
    const int before_minute = pad.passive;
    now_ms = 60000;
    pad.pollLiveData();
    assert(pad.passive == before_minute + 1);
    pad.lastPassiveBindingSlot = UINT64_MAX;
    const int before_show = pad.passive;
    pad.pollLiveData();
    assert(pad.passive == before_show + 1);
    assert(pad.colors == pad.passive && pad.numbers == pad.passive);
    assert(!context_active);
}
'''

with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "schedule.cpp"
    test_source.write_text(harness)
    for epaper, interval in ((0, 0), (0, 100), (1, 0)):
        executable = pathlib.Path(directory) / f"schedule-{epaper}-{interval}"
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-DHAS_LVGL_EPAPER={epaper}",
                        f"-DDISPLAY_BINDING_REFRESH_INTERVAL_MS={interval}",
                        str(test_source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print("PASS: LCD and e-paper pad refresh scheduling")