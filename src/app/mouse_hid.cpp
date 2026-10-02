#include "mouse_hid.h"

#if HAS_USB_HID

#include "mouse_hid_state.h"
#include "mouse_hid_trace.h"
#include "ota_activity.h"
#include "usb_hid.h"
#include "log_manager.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

namespace {
MouseHidState mouse_state;
portMUX_TYPE mouse_mutex = portMUX_INITIALIZER_UNLOCKED;
uint32_t mouse_epoch = 0;
uint32_t mouse_ota_epoch = 0;

enum class MouseTraceKind : uint8_t { Press, Input, Release, Cancel, UsbSent, UsbRetry, Touch };
struct MouseTraceSample {
    uint32_t at;
    int x;
    int y;
    int dx;
    int dy;
    MouseTraceKind kind;
    uint32_t wait_us = 0;
    uint32_t read_us = 0;
    uint8_t status = 0;
    uint8_t errors = 0;
    uint8_t report[8]{};
    uint8_t post_ack_status = 0;
    uint8_t post_ack_errors = 0;
    uint32_t ack_probe_us = 0;
    bool report_valid = false;
    bool post_ack_checked = false;
    MouseTraceDelayedAck delayed_ack;
};
constexpr uint16_t mouse_trace_capacity = 256;
MouseTraceSample mouse_trace[mouse_trace_capacity];
uint16_t mouse_trace_count = 0;
uint8_t mouse_trace_gestures = 0;
uint32_t mouse_trace_started = 0;
int mouse_trace_last_x = 0;
int mouse_trace_last_y = 0;
bool mouse_trace_active = false;
bool mouse_trace_pending_dump = false;
bool mouse_trace_dumping = false;
bool mouse_trace_truncated = false;
MouseTraceInputStats mouse_trace_input_stats;
MouseTraceCadence mouse_trace_touch_polls;
MouseTraceCadence mouse_trace_initial_touch_polls;
MouseTraceCadence mouse_trace_usb_loops;
MouseTraceAckStats mouse_trace_ack_stats;
MouseTraceAckStats mouse_trace_initial_ack_stats;
MouseTraceAckStats mouse_trace_delayed_ack_stats[MouseTraceAckSchedule::delay_count];
MouseTraceAckStats mouse_trace_initial_delayed_ack_stats[MouseTraceAckSchedule::delay_count];
MouseTraceTouchSample mouse_trace_latest_touch{};
bool mouse_trace_touch_available = false;
bool mouse_trace_raw_change_seen = false;
bool mouse_trace_usb_seen = false;
uint32_t mouse_trace_first_raw_change_ms = 0;
uint32_t mouse_trace_first_usb_ms = 0;
uint32_t mouse_trace_fresh = 0;
uint32_t mouse_trace_no_ready = 0;
uint32_t mouse_trace_errors = 0;
uint32_t mouse_trace_initial_fresh = 0;
uint32_t mouse_trace_initial_no_ready = 0;
uint32_t mouse_trace_initial_errors = 0;
uint32_t mouse_trace_max_wait_us = 0;
uint32_t mouse_trace_max_read_us = 0;
uint32_t mouse_trace_usb_sent = 0;
uint32_t mouse_trace_usb_retries = 0;
uint32_t mouse_trace_last_input_sample_at = 0;
uint32_t mouse_trace_last_touch_sample_at = 0;
float mouse_trace_threshold = 0;
float mouse_trace_sensitivity = 0;
float mouse_trace_acceleration = 0;

bool mouse_trace_in_window(uint32_t now) {
    if (!mouse_trace_active || mouse_trace_dumping) return false;
    if (now - mouse_trace_started <= 5000) return true;
    mouse_trace_truncated = true;
    return false;
}

MouseTraceSample* record_mouse_trace(MouseTraceKind kind, int x, int y, int dx, int dy) {
    if ((!mouse_trace_active && !mouse_trace_pending_dump) || mouse_trace_dumping) return nullptr;
    const uint32_t now = millis();
    if (mouse_trace_count == mouse_trace_capacity || now - mouse_trace_started > 5000) {
        mouse_trace_truncated = true;
        return nullptr;
    }
    MouseTraceSample* sample = &mouse_trace[mouse_trace_count++];
    *sample = {now, x, y, dx, dy, kind};
    return sample;
}

void record_mouse_trace_touch(const MouseTraceTouchSample& touch) {
    MouseTraceSample* sample = record_mouse_trace(MouseTraceKind::Touch, touch.x, touch.y, touch.id, touch.size);
    if (!sample) return;
    sample->at = static_cast<int32_t>(touch.at - mouse_trace_started) < 0 ? mouse_trace_started : touch.at;
    sample->wait_us = touch.wait_us;
    sample->read_us = touch.read_us;
    sample->status = touch.status;
    sample->errors = touch.errors;
    memcpy(sample->report, touch.report, sizeof(sample->report));
    sample->report_valid = touch.report_valid;
    sample->post_ack_status = touch.post_ack_status;
    sample->post_ack_errors = touch.post_ack_errors;
    sample->ack_probe_us = touch.ack_probe_us;
    sample->post_ack_checked = touch.post_ack_checked;
    sample->delayed_ack = touch.delayed_ack;
}

void dump_mouse_trace() {
    portENTER_CRITICAL(&mouse_mutex);
    const bool dump = mouse_trace_pending_dump && !mouse_trace_dumping;
    const uint16_t count = mouse_trace_count;
    const uint8_t gesture = mouse_trace_gestures;
    const uint32_t started = mouse_trace_started;
    const bool truncated = mouse_trace_truncated;
    if (dump) mouse_trace_dumping = true;
    portEXIT_CRITICAL(&mouse_mutex);
    if (!dump) return;
    LOGI("MouseTrace", "gesture=%u start=%lu samples=%u truncated=%d (buffered; first 3 gestures only)",
         gesture, static_cast<unsigned long>(started), count, truncated);
        LOGI("MouseTrace", "settings: threshold=%.2f sensitivity=%.2f acceleration=%.2f; capture=5000ms heartbeat=50ms",
            mouse_trace_threshold, mouse_trace_sensitivity, mouse_trace_acceleration);
        LOGI("MouseTrace", "widget: callbacks=%lu max-gap=%lums; through-first-change callbacks=%lu max-gap=%lums",
            static_cast<unsigned long>(mouse_trace_input_stats.callbacks.count),
            static_cast<unsigned long>(mouse_trace_input_stats.callbacks.max_gap),
            static_cast<unsigned long>(mouse_trace_input_stats.before_change.count),
            static_cast<unsigned long>(mouse_trace_input_stats.before_change.max_gap));
        LOGI("MouseTrace", "widget: change-seen=%d first-change=%lums output-seen=%d first-output=%lums changed-without-output=%lu",
            mouse_trace_input_stats.change_seen, static_cast<unsigned long>(mouse_trace_input_stats.first_change_ms),
            mouse_trace_input_stats.output_seen, static_cast<unsigned long>(mouse_trace_input_stats.first_output_ms),
            static_cast<unsigned long>(mouse_trace_input_stats.changed_without_output));
        LOGI("MouseTrace", "gt911: polls=%lu max-gap=%lums ready=%lu no-ready=%lu errors=%lu",
            static_cast<unsigned long>(mouse_trace_touch_polls.count), static_cast<unsigned long>(mouse_trace_touch_polls.max_gap),
            static_cast<unsigned long>(mouse_trace_fresh), static_cast<unsigned long>(mouse_trace_no_ready),
            static_cast<unsigned long>(mouse_trace_errors));
        LOGI("MouseTrace", "gt911 I2C: max-wait=%luus max-read=%luus",
            static_cast<unsigned long>(mouse_trace_max_wait_us),
            static_cast<unsigned long>(mouse_trace_max_read_us));
        LOGI("MouseTrace", "gt911 through-first-raw-change: polls=%lu max-gap=%lums ready=%lu no-ready=%lu errors=%lu",
            static_cast<unsigned long>(mouse_trace_initial_touch_polls.count),
            static_cast<unsigned long>(mouse_trace_initial_touch_polls.max_gap),
            static_cast<unsigned long>(mouse_trace_initial_fresh), static_cast<unsigned long>(mouse_trace_initial_no_ready),
            static_cast<unsigned long>(mouse_trace_initial_errors));
        LOGI("MouseTrace", "gt911: change-seen=%d first-change=%lums", mouse_trace_raw_change_seen,
            static_cast<unsigned long>(mouse_trace_first_raw_change_ms));
        LOGI("MouseTrace", "gt911 post-ack: cleared=%lu still-ready=%lu errors=%lu max-probe=%luus",
            static_cast<unsigned long>(mouse_trace_ack_stats.cleared),
            static_cast<unsigned long>(mouse_trace_ack_stats.still_ready),
            static_cast<unsigned long>(mouse_trace_ack_stats.errors),
            static_cast<unsigned long>(mouse_trace_ack_stats.max_probe_us));
        LOGI("MouseTrace", "gt911 post-ack through-first-raw-change: cleared=%lu still-ready=%lu errors=%lu max-probe=%luus",
            static_cast<unsigned long>(mouse_trace_initial_ack_stats.cleared),
            static_cast<unsigned long>(mouse_trace_initial_ack_stats.still_ready),
            static_cast<unsigned long>(mouse_trace_initial_ack_stats.errors),
            static_cast<unsigned long>(mouse_trace_initial_ack_stats.max_probe_us));
        for (uint8_t index = 0; index < MouseTraceAckSchedule::delay_count; ++index) {
            const MouseTraceAckStats& stats = mouse_trace_delayed_ack_stats[index];
            const MouseTraceAckStats& initial = mouse_trace_initial_delayed_ack_stats[index];
            LOGI("MouseTrace", "gt911 delayed target=%uus cleared=%lu ready=%lu errors=%lu max-read=%luus",
                MouseTraceAckSchedule::delays_us[index], static_cast<unsigned long>(stats.cleared),
                static_cast<unsigned long>(stats.still_ready), static_cast<unsigned long>(stats.errors),
                static_cast<unsigned long>(stats.max_probe_us));
            LOGI("MouseTrace", "gt911 delayed before-change target=%uus cleared=%lu ready=%lu errors=%lu",
                MouseTraceAckSchedule::delays_us[index], static_cast<unsigned long>(initial.cleared),
                static_cast<unsigned long>(initial.still_ready), static_cast<unsigned long>(initial.errors));
        }
        LOGI("MouseTrace", "usb: loops=%lu max-gap=%lums movement-sent=%lu retries=%lu sent-seen=%d first-sent=%lums",
            static_cast<unsigned long>(mouse_trace_usb_loops.count), static_cast<unsigned long>(mouse_trace_usb_loops.max_gap),
            static_cast<unsigned long>(mouse_trace_usb_sent), static_cast<unsigned long>(mouse_trace_usb_retries),
            mouse_trace_usb_seen, static_cast<unsigned long>(mouse_trace_first_usb_ms));
        for (uint16_t index = 0; index < count; ++index) {
        portENTER_CRITICAL(&mouse_mutex);
        const MouseTraceSample sample = mouse_trace[index];
        portEXIT_CRITICAL(&mouse_mutex);
        const char* kind = "input";
        switch (sample.kind) {
            case MouseTraceKind::Press: kind = "press"; break;
            case MouseTraceKind::Input: break;
            case MouseTraceKind::Release: kind = "release"; break;
            case MouseTraceKind::Cancel: kind = "cancel"; break;
            case MouseTraceKind::UsbSent: kind = "usb-sent"; break;
            case MouseTraceKind::UsbRetry: kind = "usb-retry"; break;
            case MouseTraceKind::Touch:
                 LOGI("MouseTrace", "+%lums gt911 raw-x=%d raw-y=%d status=0x%02X id=%d size=%d errors=0x%02X",
                     static_cast<unsigned long>(sample.at - started), sample.x, sample.y,
                     sample.status, sample.dx, sample.dy, sample.errors);
                 LOGI("MouseTrace", "+%lums gt911 wait=%luus read=%luus", static_cast<unsigned long>(sample.at - started),
                     static_cast<unsigned long>(sample.wait_us), static_cast<unsigned long>(sample.read_us));
                 LOGI("MouseTrace", "+%lums gt911 bytes-valid=%d bytes=%02X %02X %02X %02X %02X %02X %02X %02X",
                     static_cast<unsigned long>(sample.at - started), sample.report_valid,
                     sample.report[0], sample.report[1], sample.report[2], sample.report[3],
                     sample.report[4], sample.report[5], sample.report[6], sample.report[7]);
                 LOGI("MouseTrace", "+%lums gt911 ack-checked=%d ack-status=0x%02X ack-errors=0x%02X probe=%luus",
                     static_cast<unsigned long>(sample.at - started), sample.post_ack_checked,
                     sample.post_ack_status, sample.post_ack_errors, static_cast<unsigned long>(sample.ack_probe_us));
                 if (sample.delayed_ack.target_us) {
                     const MouseTraceDelayedAck& delayed = sample.delayed_ack;
                     LOGI("MouseTrace", "+%lums gt911 delayed target=%uus elapsed=%luus status=0x%02X errors=0x%02X read=%luus",
                         static_cast<unsigned long>(sample.at - started), delayed.target_us,
                         static_cast<unsigned long>(delayed.elapsed_us), delayed.status, delayed.status_errors,
                         static_cast<unsigned long>(delayed.read_us));
                     LOGI("MouseTrace", "+%lums gt911 delayed valid=%d errors=0x%02X x=%u y=%u size=%u",
                         static_cast<unsigned long>(sample.at - started), delayed.report_valid, delayed.report_errors,
                         delayed.report[1] | (delayed.report[2] << 8), delayed.report[3] | (delayed.report[4] << 8),
                         delayed.report[5] | (delayed.report[6] << 8));
                     LOGI("MouseTrace", "+%lums gt911 delayed bytes=%02X %02X %02X %02X %02X %02X %02X %02X",
                         static_cast<unsigned long>(sample.at - started), delayed.report[0], delayed.report[1],
                         delayed.report[2], delayed.report[3], delayed.report[4], delayed.report[5],
                         delayed.report[6], delayed.report[7]);
                 }
                continue;
        }
        LOGI("MouseTrace", "+%lums %s x=%d y=%d dx=%d dy=%d",
             static_cast<unsigned long>(sample.at - started), kind,
             sample.x, sample.y, sample.dx, sample.dy);
    }
    portENTER_CRITICAL(&mouse_mutex);
    mouse_trace_pending_dump = mouse_trace_dumping = false;
    portEXIT_CRITICAL(&mouse_mutex);
}

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

void mouse_hid_trace_begin(int x, int y, float threshold, float sensitivity, float acceleration) {
    portENTER_CRITICAL(&mouse_mutex);
    if (!mouse_trace_active && !mouse_trace_pending_dump && mouse_trace_gestures < 3) {
        ++mouse_trace_gestures;
        mouse_trace_count = 0;
        mouse_trace_started = millis();
        mouse_trace_last_x = x;
        mouse_trace_last_y = y;
        mouse_trace_input_stats.reset(mouse_trace_started, x, y);
        mouse_trace_touch_polls.reset(mouse_trace_started);
        mouse_trace_initial_touch_polls.reset(mouse_trace_started);
        mouse_trace_usb_loops.reset(mouse_trace_started);
        mouse_trace_ack_stats = {};
        mouse_trace_initial_ack_stats = {};
        for (uint8_t index = 0; index < MouseTraceAckSchedule::delay_count; ++index) {
            mouse_trace_delayed_ack_stats[index] = {};
            mouse_trace_initial_delayed_ack_stats[index] = {};
        }
        mouse_trace_raw_change_seen = mouse_trace_usb_seen = false;
        mouse_trace_first_raw_change_ms = mouse_trace_first_usb_ms = 0;
        mouse_trace_fresh = mouse_trace_no_ready = mouse_trace_errors = 0;
        mouse_trace_initial_fresh = mouse_trace_initial_no_ready = mouse_trace_initial_errors = 0;
        mouse_trace_max_wait_us = mouse_trace_max_read_us = 0;
        mouse_trace_usb_sent = mouse_trace_usb_retries = 0;
        mouse_trace_last_input_sample_at = mouse_trace_last_touch_sample_at = mouse_trace_started;
        mouse_trace_threshold = threshold;
        mouse_trace_sensitivity = sensitivity;
        mouse_trace_acceleration = acceleration;
        mouse_trace_truncated = false;
        mouse_trace_active = true;
        record_mouse_trace(MouseTraceKind::Press, x, y, 0, 0);
        if (mouse_trace_touch_available) record_mouse_trace_touch(mouse_trace_latest_touch);
    }
    portEXIT_CRITICAL(&mouse_mutex);
}

void mouse_hid_trace_input(int x, int y, int dx, int dy, bool released) {
    portENTER_CRITICAL(&mouse_mutex);
    if (mouse_trace_active) {
        const uint32_t now = millis();
        if (mouse_trace_in_window(now) && !released) mouse_trace_input_stats.observe(now, x, y, dx, dy);
        if (released || x != mouse_trace_last_x || y != mouse_trace_last_y || dx || dy ||
            now - mouse_trace_last_input_sample_at >= 50) {
            record_mouse_trace(released ? MouseTraceKind::Release : MouseTraceKind::Input, x, y, dx, dy);
            mouse_trace_last_input_sample_at = now;
        }
        mouse_trace_last_x = x;
        mouse_trace_last_y = y;
        if (released) {
            mouse_trace_active = false;
            mouse_trace_pending_dump = true;
        }
    }
    portEXIT_CRITICAL(&mouse_mutex);
}

void mouse_hid_trace_touch(const MouseTraceTouchSample& sample) {
    portENTER_CRITICAL(&mouse_mutex);
    const bool changed = mouse_trace_touch_available &&
        (sample.x != mouse_trace_latest_touch.x || sample.y != mouse_trace_latest_touch.y);
    if (mouse_trace_in_window(sample.at)) {
        mouse_trace_touch_polls.observe(sample.at);
        const bool fresh = (sample.status & 0x80) != 0;
        if (!sample.errors && sample.delayed_ack.target_us) {
            for (uint8_t index = 0; index < MouseTraceAckSchedule::delay_count; ++index) {
                if (sample.delayed_ack.target_us != MouseTraceAckSchedule::delays_us[index]) continue;
                const MouseTraceDelayedAck& delayed = sample.delayed_ack;
                mouse_trace_delayed_ack_stats[index].observe(true, delayed.status, delayed.status_errors, delayed.read_us);
                if (!mouse_trace_raw_change_seen) {
                    mouse_trace_initial_delayed_ack_stats[index].observe(true, delayed.status, delayed.status_errors, delayed.read_us);
                }
            }
        }
        if (!mouse_trace_raw_change_seen) {
            mouse_trace_initial_touch_polls.observe(sample.at);
            if (!sample.errors) mouse_trace_initial_ack_stats.observe(sample.post_ack_checked,
                sample.post_ack_status, sample.post_ack_errors, sample.ack_probe_us);
            if (sample.errors) ++mouse_trace_initial_errors;
            else if (fresh) ++mouse_trace_initial_fresh;
            else ++mouse_trace_initial_no_ready;
            if (!sample.errors && fresh && (sample.status & 0x0F) && changed) {
                mouse_trace_raw_change_seen = true;
                mouse_trace_first_raw_change_ms = sample.at - mouse_trace_started;
            }
        }
        if (sample.errors) ++mouse_trace_errors;
        else if (fresh) ++mouse_trace_fresh;
        else ++mouse_trace_no_ready;
        if (!sample.errors) mouse_trace_ack_stats.observe(sample.post_ack_checked,
            sample.post_ack_status, sample.post_ack_errors, sample.ack_probe_us);
        if (sample.wait_us > mouse_trace_max_wait_us) mouse_trace_max_wait_us = sample.wait_us;
        if (sample.read_us > mouse_trace_max_read_us) mouse_trace_max_read_us = sample.read_us;
        if (changed || sample.errors || sample.post_ack_errors || sample.delayed_ack.target_us || sample.wait_us >= 10000 || sample.read_us >= 10000 ||
            sample.at - mouse_trace_last_touch_sample_at >= 50 ||
            (fresh && !(sample.status & 0x0F))) {
            record_mouse_trace_touch(sample);
            mouse_trace_last_touch_sample_at = sample.at;
        }
    }
    mouse_trace_latest_touch = sample;
    mouse_trace_touch_available = true;
    portEXIT_CRITICAL(&mouse_mutex);
}

bool mouse_hid_trace_touch_is_active() {
    portENTER_CRITICAL(&mouse_mutex);
    const bool active = mouse_trace_in_window(millis());
    portEXIT_CRITICAL(&mouse_mutex);
    return active;
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
    if (mouse_trace_active) {
        record_mouse_trace(MouseTraceKind::Cancel, 0, 0, 0, 0);
        mouse_trace_active = false;
        mouse_trace_pending_dump = true;
    }
    mouse_state.reset();
    portEXIT_CRITICAL(&mouse_mutex);
}

void mouse_hid_loop() {
    MouseHidState snapshot;
    portENTER_CRITICAL(&mouse_mutex);
    const uint32_t trace_now = millis();
    if (mouse_trace_in_window(trace_now)) mouse_trace_usb_loops.observe(trace_now);
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
    if (!pending || !usb_hid_is_ready()) {
        dump_mouse_trace();
        return;
    }
    const bool sent = usb_hid_send_mouse_report(report, report_epoch, report_ota_epoch);
    portENTER_CRITICAL(&mouse_mutex);
    if (report.dx || report.dy) {
        if ((mouse_trace_active || mouse_trace_pending_dump) && !mouse_trace_dumping && millis() - mouse_trace_started <= 5000) {
            if (sent) {
                ++mouse_trace_usb_sent;
                if (!mouse_trace_usb_seen) {
                    mouse_trace_usb_seen = true;
                    mouse_trace_first_usb_ms = millis() - mouse_trace_started;
                }
            } else ++mouse_trace_usb_retries;
        }
        record_mouse_trace(sent ? MouseTraceKind::UsbSent : MouseTraceKind::UsbRetry,
                           0, 0, report.dx, report.dy);
    }
    portEXIT_CRITICAL(&mouse_mutex);
    if (!sent) return;
    portENTER_CRITICAL(&mouse_mutex);
    sync_mouse_epoch();
    if (report_epoch == mouse_epoch && report_ota_epoch == mouse_ota_epoch) mouse_state.acknowledge(report);
    portEXIT_CRITICAL(&mouse_mutex);
}

#endif