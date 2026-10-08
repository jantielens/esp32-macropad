#pragma once

#include <stdint.h>
#include <time.h>

enum AlarmState : uint8_t { ALARM_IDLE, ALARM_RINGING, ALARM_SNOOZED };
enum AlarmEffects : uint8_t { ALARM_EFFECT_NONE = 0, ALARM_EFFECT_STOP = 1,
                              ALARM_EFFECT_RING = 2, ALARM_EFFECT_HANDLED = 4 };

struct AlarmClockCore {
    AlarmState state = ALARM_IDLE;
    time_t handled_epoch = 0;
    time_t armed_from = 0;
    bool time_ready = false;
    bool deferred = false;
    uint64_t deadline_ms = 0;
    uint32_t snooze_ms = 9 * 60000;
    uint32_t dismiss_ms = 30 * 60000;
    uint32_t lateness_seconds = 360 * 60;

    bool occurrence_due(time_t now, bool ready, time_t candidate) const {
        return ready && armed_from >= 0 && candidate > 0 && candidate >= armed_from && candidate > handled_epoch
            && candidate <= now && now - candidate <= lateness_seconds;
    }

    void rearm(time_t now, bool ready) {
        time_ready = ready;
        armed_from = ready ? (now / 60 + 1) * 60 : -1;
        deferred = false;
    }

    uint8_t cancel() {
        const uint8_t effects = state == ALARM_RINGING ? ALARM_EFFECT_STOP : ALARM_EFFECT_NONE;
        state = ALARM_IDLE;
        deadline_ms = 0;
        deferred = false;
        return effects;
    }

    uint8_t snooze(uint64_t monotonic_ms) {
        if (state != ALARM_RINGING) return ALARM_EFFECT_NONE;
        state = ALARM_SNOOZED;
        deadline_ms = monotonic_ms + snooze_ms;
        return ALARM_EFFECT_STOP;
    }

    uint8_t tick(time_t now, uint64_t monotonic_ms, bool ready, bool ota,
                 time_t candidate, uint32_t configured_snooze_ms,
                 uint32_t configured_dismiss_ms) {
        if (ready && armed_from < 0) rearm(now, true);
        time_ready = ready;
        if (!ready) time_ready = false;
        const bool due = occurrence_due(now, ready, candidate);
        deferred = ota && (due || (state != ALARM_IDLE && monotonic_ms >= deadline_ms));
        if (ota) return ALARM_EFFECT_NONE;
        if (due) {
            uint8_t effects = cancel() | ALARM_EFFECT_HANDLED | ALARM_EFFECT_RING;
            handled_epoch = candidate;
            snooze_ms = configured_snooze_ms;
            dismiss_ms = configured_dismiss_ms;
            state = ALARM_RINGING;
            deadline_ms = monotonic_ms + dismiss_ms;
            return effects;
        }
        if (state == ALARM_SNOOZED && monotonic_ms >= deadline_ms) {
            if (monotonic_ms - deadline_ms > 300000) return cancel();
            state = ALARM_RINGING;
            deadline_ms = monotonic_ms + dismiss_ms;
            return ALARM_EFFECT_RING;
        }
        if (state == ALARM_RINGING && monotonic_ms >= deadline_ms) return cancel();
        return ALARM_EFFECT_NONE;
    }
};