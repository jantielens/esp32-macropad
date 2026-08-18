#ifndef SCREEN_SAVER_SCHEDULE_H
#define SCREEN_SAVER_SCHEDULE_H

#include <stdint.h>

enum class ScreenSaverScheduleAction : uint8_t {
    None,
    ShowIdleScreen,
    Sleep,
};

inline ScreenSaverScheduleAction screen_saver_schedule_action(
        bool displaySleepEnabled,
        uint32_t displaySleepTimeoutMs,
        bool idleScreenEnabled,
        bool idleScreenActive,
        bool idleScreenAttempted,
        bool idleScreenPadConfigured,
        uint32_t idleScreenTimeoutMs,
        uint32_t elapsedMs) {
    if (displaySleepEnabled && displaySleepTimeoutMs > 0 &&
            elapsedMs >= displaySleepTimeoutMs) {
        return ScreenSaverScheduleAction::Sleep;
    }
    if (!idleScreenEnabled || idleScreenActive || idleScreenAttempted ||
            !idleScreenPadConfigured || idleScreenTimeoutMs == 0 ||
            elapsedMs < idleScreenTimeoutMs) {
        return ScreenSaverScheduleAction::None;
    }
    return ScreenSaverScheduleAction::ShowIdleScreen;
}

#endif // SCREEN_SAVER_SCHEDULE_H