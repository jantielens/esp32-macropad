#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/screens/pad_screen.cpp").read_text()
start = source.index("void PadScreen::pollLiveData(")
end = source.index("\nbool PadScreen::containsTimeBinding", start)
poll = source[start:end]
state_source = (root / "src/app/screens/pad_screen_poll.cpp").read_text()
state_start = state_source.index("static void apply_btn_state(")
state_end = state_source.index("\nvoid PadScreen::pollBtnStateBindings", state_start)
apply_state = state_source[state_start:state_end]
event_source = (root / "src/app/screens/pad_screen_events.cpp").read_text()
feedback_start = event_source.index("void PadScreen::onWidgetPressFeedback(")
feedback_end = event_source.index("\n// Pixel-based tap flash:", feedback_start)
press_feedback = event_source[feedback_start:feedback_end]

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
enum BtnState { BTN_STATE_ENABLED, BTN_STATE_DISABLED, BTN_STATE_HIDDEN };
constexpr unsigned LV_OBJ_FLAG_HIDDEN = 1;
constexpr unsigned LV_STATE_DISABLED = 2;
struct lv_obj_t { unsigned flags = 0; unsigned states = 0; };
static bool lv_obj_has_flag(lv_obj_t* obj, unsigned flag) { return obj->flags & flag; }
static bool lv_obj_has_state(lv_obj_t* obj, unsigned state) { return obj->states & state; }
static void lv_obj_clear_flag(lv_obj_t* obj, unsigned flag) { obj->flags &= ~flag; }
static void lv_obj_add_flag(lv_obj_t* obj, unsigned flag) { obj->flags |= flag; }
static void lv_obj_clear_state(lv_obj_t* obj, unsigned state) { obj->states &= ~state; }
static void lv_obj_add_state(lv_obj_t* obj, unsigned state) { obj->states |= state; }
struct WidgetState { lv_obj_t* obj; unsigned shows = 0; unsigned hides = 0; };
struct WidgetType { void (*onShow)(WidgetState*); void (*onHide)(WidgetState*); };
struct lv_timer_t { bool deleted = false; };
struct ButtonTile {
    lv_obj_t* obj;
    const WidgetType* widget_type;
    WidgetState widget_state;
    lv_obj_t* tap_overlay = nullptr;
    lv_timer_t* tap_flash_timer = nullptr;
};
enum lv_event_code_t { LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST };
struct lv_event_t { void* user_data; void* param; };
static void* lv_event_get_user_data(lv_event_t* event) { return event->user_data; }
static void* lv_event_get_param(lv_event_t* event) { return event->param; }
static void lv_obj_remove_flag(lv_obj_t* object, unsigned flag) { lv_obj_clear_flag(object, flag); }
static unsigned flashes = 0, timer_deletes = 0, overlay_restores = 0;
static bool animations_disabled = false;
#define DISPLAY_DISABLE_ANIMATIONS animations_disabled
static lv_timer_t flash_timer;
static void lv_timer_delete(lv_timer_t* timer) {
    assert(timer && !timer->deleted);
    timer->deleted = true;
    ++timer_deletes;
}
static void restore_tap_overlay(ButtonTile*) { ++overlay_restores; }
static void do_tap_flash(ButtonTile* tile) {
    ++flashes;
    lv_obj_clear_flag(tile->tap_overlay, LV_OBJ_FLAG_HIDDEN);
    flash_timer.deleted = false;
    tile->tap_flash_timer = &flash_timer;
}
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
    static void onWidgetPressFeedback(lv_event_t* event);
    void pollBtnStateBindings() { assert(context_active); ++visibility; }
    void pollMqttBindings(bool refresh) { assert(context_active); ++live; if (refresh) ++passive; }
    void pollColorBindings() { assert(context_active); ++colors; }
    void pollNumberBindings() { assert(context_active); ++numbers; }
    void pollImageFrames() { assert(!context_active); ++images; }
};
'''
harness += poll
harness += apply_state
harness += press_feedback
harness += r'''
int main() {
    lv_obj_t button;
    lv_obj_t overlay;
    overlay.flags = LV_OBJ_FLAG_HIDDEN;
    ButtonTile feedback_tile{&button, nullptr, {&button}, &overlay};
    lv_event_code_t feedback = LV_EVENT_PRESSED;
    lv_event_t event{&feedback_tile, &feedback};
    PadScreen::onWidgetPressFeedback(&event);
    assert(!lv_obj_has_flag(&overlay, LV_OBJ_FLAG_HIDDEN));
    assert(!feedback_tile.tap_flash_timer && flashes == 0);
    feedback = LV_EVENT_RELEASED;
    PadScreen::onWidgetPressFeedback(&event);
    assert(feedback_tile.tap_flash_timer && flashes == 1);
    feedback = LV_EVENT_PRESSED;
    PadScreen::onWidgetPressFeedback(&event);
    assert(!feedback_tile.tap_flash_timer && timer_deletes == 1 && overlay_restores == 1);
    assert(!lv_obj_has_flag(&overlay, LV_OBJ_FLAG_HIDDEN));
    feedback = LV_EVENT_PRESS_LOST;
    PadScreen::onWidgetPressFeedback(&event);
    assert(lv_obj_has_flag(&overlay, LV_OBJ_FLAG_HIDDEN) && flashes == 1);
    feedback = LV_EVENT_RELEASED;
    PadScreen::onWidgetPressFeedback(&event);
    feedback = LV_EVENT_PRESS_LOST;
    PadScreen::onWidgetPressFeedback(&event);
    assert(!feedback_tile.tap_flash_timer && timer_deletes == 2 && overlay_restores == 2);
    assert(lv_obj_has_flag(&overlay, LV_OBJ_FLAG_HIDDEN) && flashes == 2);
    animations_disabled = true;
    feedback = LV_EVENT_PRESSED;
    PadScreen::onWidgetPressFeedback(&event);
    assert(lv_obj_has_flag(&overlay, LV_OBJ_FLAG_HIDDEN) && flashes == 2);
    animations_disabled = false;
    const WidgetType widget = {
        [](WidgetState* state) {
            assert(!lv_obj_has_flag(state->obj, LV_OBJ_FLAG_HIDDEN));
            assert(!lv_obj_has_state(state->obj, LV_STATE_DISABLED));
            ++state->shows;
        },
        [](WidgetState* state) { ++state->hides; }
    };
    ButtonTile tile{&button, &widget, {&button}};
    apply_btn_state(tile, BTN_STATE_ENABLED);
    assert(tile.widget_state.shows == 0);
    apply_btn_state(tile, BTN_STATE_DISABLED);
    assert(tile.widget_state.hides == 1);
    apply_btn_state(tile, BTN_STATE_ENABLED);
    assert(tile.widget_state.shows == 1);
    apply_btn_state(tile, BTN_STATE_HIDDEN);
    apply_btn_state(tile, BTN_STATE_DISABLED);
    apply_btn_state(tile, BTN_STATE_ENABLED);
    assert(tile.widget_state.shows == 2 && tile.widget_state.hides == 3);
    apply_btn_state(tile, BTN_STATE_ENABLED);
    assert(tile.widget_state.shows == 2);
    tile.widget_type = nullptr;
    apply_btn_state(tile, BTN_STATE_HIDDEN);
    apply_btn_state(tile, BTN_STATE_ENABLED);
    tile.obj = nullptr;
    apply_btn_state(tile, BTN_STATE_ENABLED);
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
print("PASS: pad refresh scheduling, widget re-enable lifecycle, and held-button overlay feedback")