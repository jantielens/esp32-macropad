#include "../src/app/alarm_clock_core.h"
#include <cassert>
#include <cstdio>

static uint8_t tick(AlarmClockCore& core, time_t now, uint64_t monotonic,
                    time_t candidate, bool ota = false, bool ready = true) {
    return core.tick(now, monotonic, ready, ota, candidate, 540000, 1800000);
}

int main() {
    AlarmClockCore core;
    assert(tick(core, 6001, 0, 6000) == 0);
    assert(core.armed_from == 6060);
    assert(tick(core, 6120, 1000, 6120) == (ALARM_EFFECT_RING | ALARM_EFFECT_HANDLED));
    assert(tick(core, 6121, 2000, 6120) == 0);
    assert(core.snooze(2000) == ALARM_EFFECT_STOP);
    assert(core.snooze(3000) == 0);
    assert(tick(core, 5000, 542000, 0) == ALARM_EFFECT_RING);
    assert(tick(core, 99999, 2342000, 0) == ALARM_EFFECT_STOP);
    assert(core.cancel() == 0);
    assert(tick(core, 6120, 2343000, 6120) == 0);

    core.rearm(12000, true);
    assert(tick(core, 12360, 3000000, 12060) == (ALARM_EFFECT_RING | ALARM_EFFECT_HANDLED));
    assert(tick(core, 12420, 3001000, 12420) ==
           (ALARM_EFFECT_STOP | ALARM_EFFECT_RING | ALARM_EFFECT_HANDLED));
    assert(core.snooze(3001000) == ALARM_EFFECT_STOP);
    assert(core.cancel() == 0);
    core.rearm(18000, true);
    assert(tick(core, 18361, 4000000, 18060) == 0);

    core.rearm(24000, true);
    assert(tick(core, 24060, 5000000, 24060, true) == 0);
    assert(core.deferred);
    assert(tick(core, 24360, 5300000, 24060) == (ALARM_EFFECT_RING | ALARM_EFFECT_HANDLED));
    assert(core.cancel() == ALARM_EFFECT_STOP);
    core.rearm(30000, true);
    assert(tick(core, 30060, 6000000, 30060, true) == 0);
    assert(tick(core, 30361, 6301000, 30060) == 0);
    assert(!core.deferred);
    assert(tick(core, 30400, 6302000, 30400, false, false) == 0);
    assert(tick(core, 30401, 6303000, 30400) == 0);

    AlarmClockCore reboot;
    reboot.handled_epoch = core.handled_epoch;
    assert(tick(reboot, 24061, 0, 24060) == 0);
    core.cancel();
    core.rearm(36000, true);
    assert(tick(core, 36060, 7000000, 36060) & ALARM_EFFECT_RING);
    assert(core.snooze(7000000) == ALARM_EFFECT_STOP);
    assert(tick(core, 36061, 7840001, 0, true) == 0);
    assert(tick(core, 36062, 7840001, 0) == 0);
    assert(core.state == ALARM_IDLE);
    std::puts("alarm clock core: PASS");
}