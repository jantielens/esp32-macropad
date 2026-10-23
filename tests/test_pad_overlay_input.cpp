#include "../src/app/pad_overlay.h"
#include <lvgl.h>
#include <core/lv_obj_event_private.h>

#include <cassert>

struct LvglInputFixture {
    lv_display_t* display = nullptr;
    lv_indev_t* indev = nullptr;
    lv_obj_t* main_button = nullptr;
    lv_obj_t* overlay_button = nullptr;
    lv_obj_t* shield = nullptr;
    PadOverlayRect overlay_rect{50, 0, 149, 99};
    BtnState overlay_state = BTN_STATE_ENABLED;
    lv_point_t point{75, 50};
    bool pressed = false;
    unsigned main_actions = 0;
    uint8_t buffer[200 * 100 * 4] = {};
};

static void read_pointer(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* fixture = static_cast<LvglInputFixture*>(lv_indev_get_user_data(indev));
    data->point = fixture->point;
    data->state = fixture->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void main_button_clicked(lv_event_t* event) {
    auto* fixture = static_cast<LvglInputFixture*>(lv_event_get_user_data(event));
    if (!pad_overlay_blocks_point(fixture->overlay_state, fixture->overlay_rect,
                                  fixture->point.x, fixture->point.y)) {
        fixture->main_actions++;
    }
}

static void overlay_shield_hit_test(lv_event_t* event) {
    auto* fixture = static_cast<LvglInputFixture*>(lv_event_get_user_data(event));
    lv_hit_test_info_t* hit = lv_event_get_hit_test_info(event);
    if (!fixture || !hit || !hit->point) return;
    hit->res = fixture->overlay_state == BTN_STATE_DISABLED &&
        pad_overlay_blocks_point(fixture->overlay_state, fixture->overlay_rect,
                                 hit->point->x, hit->point->y);
}

static void read_pointer_state(LvglInputFixture& fixture, bool pressed) {
    fixture.pressed = pressed;
    lv_tick_inc(10);
    lv_indev_read(fixture.indev);
}

int main() {
    const PadOverlayRect main_button = {0, 0, 99, 99};
    const PadOverlayRect overlay = {50, 0, 149, 99};
    const PadOverlayRect separate = {150, 0, 190, 99};

    assert(pad_overlay_resolve_state("enabled", true, true, true) == BTN_STATE_ENABLED);
    assert(pad_overlay_resolve_state("disabled", true, true, true) == BTN_STATE_DISABLED);
    assert(pad_overlay_resolve_state("hidden", true, true, true) == BTN_STATE_HIDDEN);
    assert(pad_overlay_resolve_state("", false, false, true) == BTN_STATE_ENABLED);
    assert(pad_overlay_resolve_state("", false, true, true) == BTN_STATE_HIDDEN);
    assert(pad_overlay_resolve_state("invalid", true, true, true) == BTN_STATE_HIDDEN);
    assert(pad_overlay_resolve_state(nullptr, false, true, false) == BTN_STATE_ENABLED);

    assert(pad_overlay_blocks_point(BTN_STATE_ENABLED, overlay, 50, 40));
    assert(pad_overlay_blocks_point(BTN_STATE_DISABLED, overlay, 149, 99));
    assert(!pad_overlay_blocks_point(BTN_STATE_HIDDEN, overlay, 75, 30));
    assert(!pad_overlay_blocks_point(BTN_STATE_ENABLED, overlay, 49, 40));

    assert(pad_overlay_covers_button(BTN_STATE_ENABLED, overlay, main_button));
    assert(pad_overlay_covers_button(BTN_STATE_DISABLED, overlay, main_button));
    assert(!pad_overlay_covers_button(BTN_STATE_HIDDEN, overlay, main_button));
    assert(!pad_overlay_covers_button(BTN_STATE_ENABLED, overlay, separate));

    static bool initialized = false;
    if (!initialized) { lv_init(); initialized = true; }
    LvglInputFixture fixture;
    fixture.display = lv_display_create(200, 100);
    assert(fixture.display);
    lv_display_set_default(fixture.display);
    lv_display_set_buffers(fixture.display, fixture.buffer, nullptr, sizeof(fixture.buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(fixture.display,
        [](lv_display_t* display, const lv_area_t*, uint8_t*) { lv_display_flush_ready(display); });
    fixture.indev = lv_indev_create();
    assert(fixture.indev);
    lv_indev_set_type(fixture.indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(fixture.indev, fixture.display);
    lv_indev_set_user_data(fixture.indev, &fixture);
    lv_indev_set_read_cb(fixture.indev, read_pointer);
    fixture.main_button = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(fixture.main_button, 0, 0);
    lv_obj_set_size(fixture.main_button, 200, 100);
    lv_obj_set_scrollable(fixture.main_button, false);
    lv_obj_set_clickable(fixture.main_button, true);
    lv_obj_update_layout(fixture.main_button);
    lv_obj_add_event_cb(fixture.main_button, main_button_clicked,
                        LV_EVENT_CLICKED, &fixture);
    fixture.overlay_button = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(fixture.overlay_button, 50, 0);
    lv_obj_set_size(fixture.overlay_button, 100, 100);
    lv_obj_set_scrollable(fixture.overlay_button, false);
    lv_obj_set_clickable(fixture.overlay_button, true);
    lv_obj_update_layout(fixture.overlay_button);
    lv_obj_move_foreground(fixture.overlay_button);
    fixture.shield = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(fixture.shield);
    lv_obj_set_pos(fixture.shield, 0, 0);
    lv_obj_set_size(fixture.shield, 200, 100);
    lv_obj_set_style_bg_opa(fixture.shield, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollable(fixture.shield, false);
    lv_obj_set_clickable(fixture.shield, true);
    lv_obj_set_adv_hittest(fixture.shield, true);
    lv_obj_add_event_cb(fixture.shield, overlay_shield_hit_test, LV_EVENT_HIT_TEST, &fixture);
    lv_obj_move_foreground(fixture.shield);

    fixture.overlay_state = BTN_STATE_DISABLED;
    lv_obj_add_state(fixture.overlay_button, LV_STATE_DISABLED);
    read_pointer_state(fixture, true);
    read_pointer_state(fixture, false);
    assert(fixture.main_actions == 0);

    fixture.overlay_state = BTN_STATE_HIDDEN;
    lv_obj_set_hidden(fixture.overlay_button, true);
    lv_obj_clear_state(fixture.overlay_button, LV_STATE_DISABLED);
    lv_indev_reset(fixture.indev, nullptr);
    fixture.point = {25, 50};
    read_pointer_state(fixture, true);
    read_pointer_state(fixture, false);
    assert(fixture.main_actions == 1);

    fixture.overlay_state = BTN_STATE_ENABLED;
    lv_obj_set_hidden(fixture.overlay_button, false);
    lv_obj_clear_state(fixture.overlay_button, LV_STATE_DISABLED);
    fixture.point = {75, 50};
    read_pointer_state(fixture, true);
    read_pointer_state(fixture, false);
    assert(fixture.main_actions == 1);

    fixture.point = {25, 50};
    read_pointer_state(fixture, true);
    fixture.point = {75, 50};
    fixture.overlay_state = BTN_STATE_ENABLED;
    lv_indev_reset(fixture.indev, nullptr);
    read_pointer_state(fixture, false);
    assert(fixture.main_actions == 1);

    lv_indev_delete(fixture.indev);
    lv_obj_delete(fixture.shield);
    lv_obj_delete(fixture.main_button);
    lv_obj_delete(fixture.overlay_button);
    lv_display_delete(fixture.display);
    return 0;
}