#include "auto_shutdown.h"

#if HAS_AUTO_SHUTDOWN
#include "action_continuation.h"
#include "board_hardware.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "power_manager.h"
#include "sensors/sensor_manager.h"
#if HAS_DISPLAY
#include "device_dialog.h"
#include "display_manager_lock.h"
#include "screen_saver_manager.h"
#endif

#include <Arduino.h>
#include <atomic>
#include <math.h>

namespace {
std::atomic<uint16_t> timeoutSeconds{0};
std::atomic<uint32_t> lastActivityMs{0};
bool wasEligible = false;
uint32_t shutdownToken = 0;
bool shutdownRequested = false;
#if HAS_DISPLAY
uint32_t noticeGeneration = 0;
uint32_t noticeActivityMs = 0;
uint32_t noticeStartedMs = 0;
bool noticeAttempted = false;
bool noticeSettling = false;
uint32_t noticePresentedMs = 0;
constexpr uint32_t kNoticeSettleMs = 1000;
#endif

void clear_shutdown() {
#if HAS_DISPLAY
#ifdef BOARD_AUTO_SHUTDOWN_CANCEL
    BOARD_AUTO_SHUTDOWN_CANCEL(noticeGeneration);
#endif
    if (noticeGeneration) device_dialog_dismiss_notification(noticeGeneration);
    noticeGeneration = 0;
    noticeAttempted = false;
    noticeSettling = false;
#endif
    action_continuation_release(shutdownToken);
    shutdownToken = 0;
    shutdownRequested = false;
}
}

void auto_shutdown_configure(uint16_t seconds) {
    lastActivityMs.store(millis());
    timeoutSeconds.store(seconds);
}

void auto_shutdown_notify_activity() {
    lastActivityMs.store(millis());
}

void auto_shutdown_loop() {
    const uint32_t now = millis();
    const uint16_t seconds = timeoutSeconds.load();
    if (shutdownRequested) {
        if (!seconds) wasEligible = false;
        if (!action_continuation_is_pending(shutdownToken)) {
            LOGW("Power", "Auto-shutdown did not complete; clearing its notification");
            clear_shutdown();
        }
        return;
    }
    if (!seconds) {
        wasEligible = false;
        if (shutdownToken) clear_shutdown();
        return;
    }
    const PowerMode mode = power_manager_get_current_mode();
    BatterySnapshot battery;
    sensor_manager_get_battery(battery);
    const float externalPower = battery.readings.get(BatteryField::ExternalPowerPresent);
    const bool eligible = (mode == PowerMode::AlwaysOn || mode == PowerMode::Offline) &&
        (battery.supported & battery_field_mask(BatteryField::ExternalPowerPresent)) &&
        isfinite(externalPower) && externalPower == 0;
    if (!eligible || !wasEligible) lastActivityMs.store(now);
    wasEligible = eligible;
    if (shutdownToken) {
#if HAS_DISPLAY
        if (!eligible || lastActivityMs.load() != noticeActivityMs || ota_activity_is_busy()) {
            LOGI("Power", "Auto-shutdown cancelled: activity, power, or update changed");
            clear_shutdown();
            lastActivityMs.store(now);
            return;
        }
        if (!noticeAttempted && screen_saver_manager_input_ready()) {
            noticeAttempted = true;
            const bool notified = device_dialog_notify("Powered off automatically", 0, &noticeGeneration);
            if (!notified)
                LOGW("Power", "Auto-shutdown notification unavailable");
        }
        bool presented = false;
        if (noticeGeneration) {
            const DeviceNotificationState state = device_dialog_notification_state(noticeGeneration);
            if (state == DEVICE_NOTIFICATION_MISSING) {
                LOGW("Power", "Auto-shutdown cancelled: notification replaced or dismissed");
                clear_shutdown();
                lastActivityMs.store(now);
                return;
            }
            presented = state == DEVICE_NOTIFICATION_PRESENTED;
        }
        if (lastActivityMs.load() != noticeActivityMs) {
            LOGI("Power", "Auto-shutdown cancelled: activity during notification");
            clear_shutdown();
            return;
        }
        if (!presented) {
            if (uint32_t(now - noticeStartedMs) < ACTION_CONTINUATION_TIMEOUT_MS) return;
            if (noticeSettling) {
                LOGW("Power", "Auto-shutdown cancelled: final presentation timed out");
                clear_shutdown();
                lastActivityMs.store(now);
                return;
            }
            LOGW("Power", "Auto-shutdown proceeding without a displayed notification");
        } else {
            if (!noticeSettling) {
                noticeSettling = true;
                noticePresentedMs = now;
            }
            if (uint32_t(now - noticePresentedMs) < kNoticeSettleMs) return;
#ifdef BOARD_AUTO_SHUTDOWN_PREPARE
            const DeviceNotificationState finalState = BOARD_AUTO_SHUTDOWN_PREPARE(noticeGeneration);
            if (finalState == DEVICE_NOTIFICATION_MISSING ||
                (finalState != DEVICE_NOTIFICATION_PRESENTED &&
                 uint32_t(now - noticeStartedMs) >= ACTION_CONTINUATION_TIMEOUT_MS)) {
                LOGW("Power", "Auto-shutdown cancelled: final presentation unavailable");
                clear_shutdown();
                lastActivityMs.store(now);
                return;
            }
            if (finalState != DEVICE_NOTIFICATION_PRESENTED) return;
#endif
        }
#endif
    } else {
        const uint32_t activityMs = lastActivityMs.load();
        if (!eligible || int32_t(now - activityMs) < int32_t(seconds) * 1000) return;
        if (ota_activity_is_busy() || action_continuation_is_active()) return;

        if (!action_continuation_begin(nullptr, 0, "Auto-shutdown", &shutdownToken)) {
            LOGW("Power", "Auto-shutdown deferred: no action continuation available");
            lastActivityMs.store(now);
            return;
        }
#if HAS_DISPLAY
        noticeActivityMs = activityMs;
        noticeStartedMs = now;
        screen_saver_manager_wake();
        return;
#endif
    }
    if (BOARD_AUTO_SHUTDOWN_REQUEST(shutdownToken)) {
        shutdownRequested = true;
        action_continuation_mark_pending(shutdownToken);
#if HAS_DISPLAY
        bool locked = false;
        display_manager_lock_if_needed(&locked);
        display_manager_unlock_if_needed(locked);
#endif
        LOGI("Power", "Battery inactivity reached %us; shutdown requested", unsigned(seconds));
    } else {
        clear_shutdown();
        LOGW("Power", "Auto-shutdown rejected; retrying after another inactivity interval");
    }
    // A failed hardware shutdown must not trigger another attempt every loop.
    lastActivityMs.store(now);
}
#endif
