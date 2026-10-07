#include "gamepad_hid.h"

#if HAS_USB_HID
#include "action_continuation.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "usb_hid.h"
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>

namespace {
GamepadHidState gamepad_state;
portMUX_TYPE gamepad_mutex = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> gamepad_generation{1};
uint32_t gamepad_usb_epoch = 0;
uint32_t gamepad_ota_epoch = 0;
bool gamepad_was_ready = false;

void gamepad_reset_locked() {
    gamepad_state.reset();
    gamepad_generation.fetch_add(1);
}

void gamepad_sync_locked() {
    const uint32_t usb_epoch = usb_hid_epoch();
    const uint32_t ota_epoch = ota_activity_epoch();
    const bool ready = gamepad_hid_is_ready();
    if (usb_epoch != gamepad_usb_epoch || ota_epoch != gamepad_ota_epoch ||
        (!ready && gamepad_was_ready)) {
        gamepad_reset_locked();
        gamepad_usb_epoch = usb_epoch;
        gamepad_ota_epoch = ota_epoch;
    }
    gamepad_was_ready = ready;
}
}

bool gamepad_hid_is_ready() {
    return usb_hid_is_ready() && usb_hid_gamepad_enabled() && !ota_activity_is_active();
}

uint32_t gamepad_hid_generation() {
    portENTER_CRITICAL(&gamepad_mutex);
    gamepad_sync_locked();
    const uint32_t generation = gamepad_generation.load();
    portEXIT_CRITICAL(&gamepad_mutex);
    return generation;
}

uint32_t gamepad_hid_submission_generation() { return gamepad_generation.load(); }

bool gamepad_hid_hold(GamepadControl control, bool down) {
    portENTER_CRITICAL(&gamepad_mutex);
    gamepad_sync_locked();
    const bool accepted = gamepad_hid_is_ready() && gamepad_state.hold(control, down);
    portEXIT_CRITICAL(&gamepad_mutex);
    return accepted;
}

bool gamepad_hid_tap(GamepadControl control, uint32_t token) {
    portENTER_CRITICAL(&gamepad_mutex);
    gamepad_sync_locked();
    const bool accepted = gamepad_hid_is_ready() && gamepad_state.tap(control, token);
    portEXIT_CRITICAL(&gamepad_mutex);
    return accepted;
}

uint32_t gamepad_hid_acquire(GamepadControl control, uint32_t generation) {
    portENTER_CRITICAL(&gamepad_mutex);
    gamepad_sync_locked();
    const uint32_t owner = gamepad_hid_is_ready() && generation == gamepad_generation.load()
        ? gamepad_state.acquire(control) : 0;
    portEXIT_CRITICAL(&gamepad_mutex);
    return owner;
}

uint32_t gamepad_hid_acquire_stick(uint8_t stick, uint32_t generation) {
    portENTER_CRITICAL(&gamepad_mutex);
    gamepad_sync_locked();
    const uint32_t owner = gamepad_hid_is_ready() && generation == gamepad_generation.load()
        ? gamepad_state.acquire_stick(stick) : 0;
    portEXIT_CRITICAL(&gamepad_mutex);
    return owner;
}

bool gamepad_hid_move(uint32_t owner, int16_t horizontal, int16_t vertical, uint32_t generation) {
    portENTER_CRITICAL(&gamepad_mutex);
    gamepad_sync_locked();
    const bool accepted = gamepad_hid_is_ready() && generation == gamepad_generation.load()
        && gamepad_state.move_stick(owner, horizontal, vertical);
    portEXIT_CRITICAL(&gamepad_mutex);
    return accepted;
}

void gamepad_hid_release(uint32_t owner, uint32_t generation) {
    portENTER_CRITICAL(&gamepad_mutex);
    gamepad_sync_locked();
    if (generation == gamepad_generation.load()) gamepad_state.release(owner);
    portEXIT_CRITICAL(&gamepad_mutex);
}

void gamepad_hid_cancel() {
    portENTER_CRITICAL(&gamepad_mutex);
    gamepad_reset_locked();
    portEXIT_CRITICAL(&gamepad_mutex);
}

void gamepad_hid_loop() {
    GamepadReport report;
    portENTER_CRITICAL(&gamepad_mutex);
    gamepad_sync_locked();
    gamepad_state.tick(millis());
    const uint32_t usb_epoch = gamepad_usb_epoch;
    const uint32_t ota_epoch = gamepad_ota_epoch;
    const uint32_t generation = gamepad_generation.load();
    const bool pending = gamepad_state.next(report);
    const bool ready = gamepad_was_ready;
    portEXIT_CRITICAL(&gamepad_mutex);

    static uint32_t logged_generation = 0;
    static bool logged_ready = false;
    if (generation != logged_generation || ready != logged_ready) {
        if (ready != logged_ready) LOGI("GamepadHID", "Ready=%u", unsigned(ready));
        LOGT("GamepadHID", "Ready=%u generation=%lu usb_epoch=%lu ota_epoch=%lu",
             unsigned(ready), (unsigned long)generation, (unsigned long)usb_epoch, (unsigned long)ota_epoch);
        logged_generation = generation;
        logged_ready = ready;
    }
    if (pending && usb_hid_send_gamepad_report(report, usb_epoch, ota_epoch, generation)) {
        portENTER_CRITICAL(&gamepad_mutex);
        gamepad_sync_locked();
        const bool current = generation == gamepad_generation.load();
        if (current) gamepad_state.acknowledge(report, millis());
        portEXIT_CRITICAL(&gamepad_mutex);
        static GamepadReport logged_report;
        static uint32_t last_report_log = 0;
        static bool report_logged = false;
        const uint32_t now = millis();
        const bool discrete_changed = report.buttons != logged_report.buttons || report.hat != logged_report.hat ||
            report.left_trigger != logged_report.left_trigger || report.right_trigger != logged_report.right_trigger;
        const bool centered = ((logged_report.left_x || logged_report.left_y) && !report.left_x && !report.left_y) ||
            ((logged_report.right_x || logged_report.right_y) && !report.right_x && !report.right_y);
        const bool deflected = ((!logged_report.left_x && !logged_report.left_y) && (report.left_x || report.left_y)) ||
            ((!logged_report.right_x && !logged_report.right_y) && (report.right_x || report.right_y));
        if (current && (!report_logged || logged_report.generation != report.generation || discrete_changed || centered || deflected || now - last_report_log >= 200)) {
            LOGT("GamepadHID", "Submitted generation=%lu seq=%lu left=%d,%d right=%d,%d triggers=%u,%u hat=%u buttons=0x%04x",
                 (unsigned long)generation, (unsigned long)report.sequence,
                 int(report.left_x), int(report.left_y), int(report.right_x), int(report.right_y),
                 unsigned(report.left_trigger), unsigned(report.right_trigger), unsigned(report.hat), unsigned(report.buttons));
            logged_report = report;
            last_report_log = now;
            report_logged = true;
        }
    }

    uint32_t token;
    bool success;
    for (;;) {
        portENTER_CRITICAL(&gamepad_mutex);
        const bool completed = gamepad_state.take_completion(token, success);
        portEXIT_CRITICAL(&gamepad_mutex);
        if (!completed) break;
#if HAS_DISPLAY || HAS_BUTTON
        action_continuation_complete(token, success);
#endif
    }
}
#endif