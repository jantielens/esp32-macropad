#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/touch_manager.cpp").read_text()
globals_start = source.index("static std::atomic<uint32_t> g_lvgl_suppress_until_ms")
globals_end = source.index("TouchManager::TouchManager()")
callback_start = source.index("void TouchManager::readCallback(")
callback_end = source.index("void TouchManager::init(", callback_start)
cancel_start = source.index("void touch_manager_cancel_physical_input()")
cancel_end = source.index("// C-style interface", cancel_start)

harness = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include "touch_sample.h"
#define HAS_DISPLAY 1
#define HAS_USB_HID TEST_USB
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
enum { LV_INDEV_STATE_RELEASED, LV_INDEV_STATE_PRESSED };
struct lv_indev_t { void* user_data; };
struct lv_indev_data_t { int state = LV_INDEV_STATE_RELEASED; struct { int x = 0, y = 0; } point; };
static uint32_t clock_ms = 0;
static unsigned resets = 0, activities = 0;
static bool ready = true;
static uint32_t millis() { return clock_ms; }
static void* lv_indev_get_user_data(lv_indev_t* indev) { return indev->user_data; }
static void lv_indev_reset(lv_indev_t*, void*) { ++resets; }
static bool screen_saver_manager_input_ready() { return ready; }
static void screen_saver_manager_notify_activity(bool) { ++activities; }
#if HAS_USB_HID
static unsigned routed = 0, canceled = 0;
static uint32_t gamepad_hid_generation() { return 1; }
static uint32_t mouse_hid_generation() { return 1; }
struct MouseSurfaceTouch {
    static MouseSurfaceTouch* session() { return nullptr; }
    void cancel() {}
};
struct Router {
    bool reset_navigation = false;
    uint8_t physical_count = 0;
    void cancel() { ++canceled; }
    TouchSample update(const TouchSnapshot& snapshot, bool, uint32_t, uint32_t) {
        ++routed;
        physical_count = snapshot.count;
        TouchSample sample;
        sample.pressed = snapshot.count != 0;
        sample.horizontal = snapshot.contacts[0].horizontal;
        sample.vertical = snapshot.contacts[0].vertical;
        return sample;
    }
};
static Router g_gamepad_touch_router;
#endif
struct Driver {
    unsigned reads = 0;
    TouchSnapshot snapshot;
    TouchSnapshot readSnapshot() { ++reads; return snapshot; }
};
class TouchManager {
public:
    Driver* driver;
    static void readCallback(lv_indev_t*, lv_indev_data_t*);
};
void touch_manager_cancel_physical_input();
'''
harness += source[globals_start:globals_end]
harness += source[callback_start:callback_end]
harness += source[cancel_start:cancel_end]
harness += r'''
int main() {
    Driver driver;
    TouchManager manager{&driver};
    lv_indev_t indev{&manager};
    lv_indev_data_t output;
    auto poll = [&]() { ++clock_ms; TouchManager::readCallback(&indev, &output); };
    driver.snapshot.count = 1;
    driver.snapshot.contacts[0].horizontal = 12;
    driver.snapshot.contacts[0].vertical = 34;
    poll();
    assert(output.state == LV_INDEV_STATE_PRESSED && output.point.x == 12);
    g_lvgl_force_released = true;
    poll();
    assert(driver.reads == 2 && g_cached_physical.pressed && output.state == LV_INDEV_STATE_RELEASED);
    g_lvgl_force_released = false;
    poll();
    assert(output.state == LV_INDEV_STATE_RELEASED && g_require_release);
    driver.snapshot.count = 0;
    driver.snapshot.status = TouchReadStatus::Unchanged;
    poll();
    assert(g_require_release);
    driver.snapshot.status = TouchReadStatus::Fresh;
    poll();
    assert(!g_require_release);
    driver.snapshot.count = 1;
    poll();
    assert(output.state == LV_INDEV_STATE_PRESSED);
    driver.snapshot.status = TouchReadStatus::Error;
    poll();
    clock_ms += 100;
    poll();
    assert(output.state == LV_INDEV_STATE_RELEASED && g_require_release);
    driver.snapshot.status = TouchReadStatus::Fresh;
    poll();
    assert(output.state == LV_INDEV_STATE_RELEASED && g_require_release);
    driver.snapshot.count = 0;
    poll();
    assert(!g_require_release);
    g_synthetic_tap.state = SyntheticTapState::PendingPress;
    g_synthetic_tap.queued_at_ms = clock_ms;
    g_synthetic_tap.x = 56;
    g_synthetic_tap.y = 78;
    driver.snapshot.count = 1;
    poll();
    assert(g_synthetic_tap.state == SyntheticTapState::PendingPress && output.point.x == 12);
    driver.snapshot.count = 0;
    poll();
    assert(output.state == LV_INDEV_STATE_RELEASED);
    assert(g_synthetic_tap.state == SyntheticTapState::PendingPress);
    poll();
    assert(output.state == LV_INDEV_STATE_PRESSED && output.point.x == 56);
#if HAS_USB_HID
    assert(g_gamepad_touch_router.physical_count == 0);
#endif
    assert(g_synthetic_tap.state == SyntheticTapState::ReleaseOwed);
    g_lvgl_force_released = true;
    const unsigned before = driver.reads;
    poll();
    assert(driver.reads == before + 1 && output.state == LV_INDEV_STATE_RELEASED);
    assert(g_synthetic_tap.state == SyntheticTapState::Idle);
    assert(resets && activities);
#if HAS_USB_HID
    assert(routed && canceled);
#endif
}
'''

with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "manager.cpp"
    test_source.write_text(harness)
    for usb in (0, 1):
        executable = pathlib.Path(directory) / f"manager-{usb}"
        subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                        f"-DTEST_USB={usb}", "-I", str(root / "src/app"),
                        str(test_source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print("PASS: touch manager single-reader wake, suppression, fresh-release rearm, errors, and synthetic pairing")

header = (root / "src/app/touch_manager.h").read_text()
capabilities = header[header.index("#if HAS_DISPLAY && HAS_TOUCH\nuint8_t touch_manager_contact_capacity()"):
                      header.index("#endif // TOUCH_MANAGER_H")]
with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "capabilities.cpp"
    executable = pathlib.Path(directory) / "capabilities"
    test_source.write_text("#include <cassert>\n#include <cstdint>\n" + capabilities + r'''
#if HAS_DISPLAY && HAS_TOUCH
static uint8_t capacity = 0;
uint8_t touch_manager_contact_capacity() { return capacity; }
#endif
int main() {
    assert(touch_manager_contact_capacity() == 0);
    assert(!touch_manager_controller_multitouch());
#if HAS_DISPLAY && HAS_TOUCH
    capacity = 1;
    assert(!touch_manager_controller_multitouch());
    capacity = 5;
    assert(touch_manager_controller_multitouch() == bool(HAS_USB_HID));
#endif
}
''')
    for display in (0, 1):
        for touch in (0, 1):
            for usb in (0, 1):
                subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                                f"-DHAS_DISPLAY={display}", f"-DHAS_TOUCH={touch}", f"-DHAS_USB_HID={usb}",
                                str(test_source), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)
print("PASS: shared touch capabilities across display, touch, and USB feature combinations")

mouse_source = (root / "src/app/mouse_hid.cpp").read_text()
mouse_start = mouse_source.index("namespace {")
mouse_end = mouse_source.rindex("#endif")
mouse_harness = r'''
#include <cassert>
#include <cstdint>
#include "mouse_hid_state.h"
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
static bool usb_ready = false, ota_active = false;
static uint32_t usb_epoch = 1, ota_epoch = 0, clock_ms = 0;
static unsigned submissions = 0;
static uint32_t millis() { return ++clock_ms; }
static uint32_t usb_hid_epoch() { return usb_epoch; }
static uint32_t ota_activity_epoch() { return ota_epoch; }
static bool usb_hid_is_ready() { return usb_ready; }
static bool ota_activity_is_active() { return ota_active; }
static bool usb_hid_send_mouse_report(const MouseHidReport&, uint32_t, uint32_t) {
    ++submissions;
    return true;
}
bool mouse_hid_is_ready();
'''
mouse_harness += mouse_source[mouse_start:mouse_end]
mouse_harness += r'''
int main() {
    auto expect_stable = [](uint32_t generation) {
        for (unsigned poll = 0; poll < 100; ++poll) {
            mouse_hid_loop();
            assert(mouse_hid_generation() == generation);
        }
    };
    const uint32_t initial = mouse_hid_generation();
    expect_stable(initial);
    assert(submissions == 0);
    usb_ready = true;
    mouse_hid_loop();
    assert(mouse_hid_generation() == initial);
    mouse_hid_move(12, 34, usb_epoch);
    usb_ready = false;
    mouse_hid_loop();
    const uint32_t disconnected = mouse_hid_generation();
    assert(disconnected == initial + 1);
    expect_stable(disconnected);
    usb_ready = true;
    mouse_hid_loop();
    MouseHidReport report;
    assert(!mouse_state.next(report));
    mouse_hid_move(5, 6, usb_epoch);
    assert(mouse_state.next(report) && report.dx == 5 && report.dy == 6);
    ++usb_epoch;
    const uint32_t reenumerated = mouse_hid_generation();
    assert(reenumerated == disconnected + 1);
    expect_stable(reenumerated);
    ota_active = true;
    ++ota_epoch;
    const uint32_t paused = mouse_hid_generation();
    assert(paused == reenumerated + 1);
    expect_stable(paused);
    ota_active = false;
    ++ota_epoch;
    const uint32_t resumed = mouse_hid_generation();
    assert(resumed == paused + 1);
    expect_stable(resumed);
}
'''
with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "mouse_disconnect.cpp"
    executable = pathlib.Path(directory) / "mouse_disconnect"
    test_source.write_text(mouse_harness)
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "src/app"), str(test_source),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: mouse generations stay stable while disconnected and invalidate once per transition")