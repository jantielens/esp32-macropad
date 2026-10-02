#include "mouse_hid.h"

#if HAS_USB_HID

#include "mouse_hid_state.h"
#include "ota_activity.h"
#include "usb_hid.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

namespace {
MouseHidState mouse_state;
portMUX_TYPE mouse_mutex = portMUX_INITIALIZER_UNLOCKED;
uint32_t mouse_epoch = 0;
uint32_t mouse_ota_epoch = 0;

void sync_mouse_epoch() {
    const uint32_t live_epoch = usb_hid_epoch();
    const uint32_t live_ota_epoch = ota_activity_epoch();
    if (mouse_epoch != live_epoch || mouse_ota_epoch != live_ota_epoch) {
        mouse_state.reset();
        mouse_epoch = live_epoch;
        mouse_ota_epoch = live_ota_epoch;
    }
}
}

bool mouse_hid_is_ready() {
    return usb_hid_is_ready() && !ota_activity_is_active();
}

void mouse_hid_move(int dx, int dy, uint32_t epoch) {
    if (!mouse_hid_is_ready()) return;
    portENTER_CRITICAL(&mouse_mutex);
    sync_mouse_epoch();
    if (epoch == mouse_epoch) mouse_state.move(dx, dy);
    portEXIT_CRITICAL(&mouse_mutex);
}

void mouse_hid_scroll(int wheel, int pan, uint32_t epoch) {
    if (!mouse_hid_is_ready()) return;
    portENTER_CRITICAL(&mouse_mutex);
    sync_mouse_epoch();
    if (epoch == mouse_epoch) mouse_state.scroll(wheel, pan);
    portEXIT_CRITICAL(&mouse_mutex);
}

void mouse_hid_begin_touch() {
    portENTER_CRITICAL(&mouse_mutex);
    sync_mouse_epoch();
    mouse_state.stop_scroll();
    portEXIT_CRITICAL(&mouse_mutex);
}

void mouse_hid_start_scroll_inertia(float velocity, float inertia, bool horizontal, uint32_t epoch) {
    if (!mouse_hid_is_ready()) return;
    portENTER_CRITICAL(&mouse_mutex);
    sync_mouse_epoch();
    if (epoch == mouse_epoch) mouse_state.start_inertia(velocity, inertia, horizontal, millis());
    portEXIT_CRITICAL(&mouse_mutex);
}

bool mouse_hid_click(uint32_t epoch, uint8_t buttons) {
    if (!mouse_hid_is_ready()) return false;
    portENTER_CRITICAL(&mouse_mutex);
    sync_mouse_epoch();
    const bool accepted = epoch == mouse_epoch && mouse_state.click(buttons);
    portEXIT_CRITICAL(&mouse_mutex);
    return accepted;
}

void mouse_hid_cancel() {
    portENTER_CRITICAL(&mouse_mutex);
    mouse_state.reset();
    portEXIT_CRITICAL(&mouse_mutex);
}

void mouse_hid_loop() {
    MouseHidState snapshot;
    portENTER_CRITICAL(&mouse_mutex);
    sync_mouse_epoch();
    if (!mouse_hid_is_ready()) mouse_state.reset();
    snapshot = mouse_state;
    portEXIT_CRITICAL(&mouse_mutex);
    const MouseHidInertiaTick coast = snapshot.inertia_tick(millis());
    MouseHidReport report;
    portENTER_CRITICAL(&mouse_mutex);
    sync_mouse_epoch();
    if (!mouse_hid_is_ready()) mouse_state.reset();
    else mouse_state.apply_inertia(coast);
    const uint32_t report_epoch = mouse_epoch;
    const uint32_t report_ota_epoch = mouse_ota_epoch;
    const bool pending = mouse_state.next(report);
    portEXIT_CRITICAL(&mouse_mutex);
    if (!pending || !usb_hid_is_ready()) return;
    if (!usb_hid_send_mouse_report(report, report_epoch, report_ota_epoch)) return;
    portENTER_CRITICAL(&mouse_mutex);
    sync_mouse_epoch();
    if (report_epoch == mouse_epoch && report_ota_epoch == mouse_ota_epoch) mouse_state.acknowledge(report);
    portEXIT_CRITICAL(&mouse_mutex);
}

#endif