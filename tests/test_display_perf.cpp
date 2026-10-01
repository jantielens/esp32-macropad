#include "display_perf.h"
#include <cassert>

int main() {
    DisplayPerfWindow window;
    DisplayPerfStats stats = {};
    window.reset(200);
    window.frames = 60;
    window.lv_timer.add(100);
    window.lv_timer.add(300);
    window.data_stream.add(50);
    window.screen_update.add(80);
    window.cycle.add(500);
    window.present.add(700);
    window.present.add(900);
    assert(!window.publish(1199, stats));
    assert(window.publish(1700, stats));
    assert(stats.fps == 40);
    assert(stats.lv_timer_us == 200 && stats.lv_timer_peak_us == 300);
    assert(stats.present_us == 800 && stats.present_peak_us == 900);
    assert(stats.data_stream_us == 50 && stats.screen_update_us == 80);
    assert(stats.cycle_us == 500 && stats.cycle_peak_us == 500);
    assert(window.publish(2700, stats));
    assert(stats.fps == 0 && stats.lv_timer_us == 0 && stats.present_peak_us == 0);
    window.frames = 10;
    window.reset(4000);
    assert(window.publish(5000, stats) && stats.fps == 0);
    window.reset(UINT32_MAX - 499);
    window.frames = 20;
    assert(window.publish(500, stats) && stats.fps == 20);
    window.frames = UINT32_MAX;
    assert(window.publish(1500, stats) && stats.fps == UINT16_MAX);
}