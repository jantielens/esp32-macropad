#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../src/app/tone_alert_overlay.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    ToneAlertOverlay overlay = {};
    int16_t unchanged[] = {100, -100, 200, -200};
    int16_t expected[sizeof(unchanged) / sizeof(unchanged[0])];
    memcpy(expected, unchanged, sizeof(unchanged));
    tone_alert_overlay_mix(&overlay, unchanged, 2, 48000);
    check(memcmp(unchanged, expected, sizeof(unchanged)) == 0,
          "inactive overlay changed Music PCM");

    check(tone_alert_overlay_start(&overlay, "250:20", false, 1000),
          "valid tone alert was rejected");
    int16_t saturated[] = {32767, 32767, 32767, 32767,
                           0, 0, -32768, -32768};
    tone_alert_overlay_mix(&overlay, saturated, 4, 1000);
    check(saturated[2] == 32767 && saturated[3] == 32767,
          "positive mixed PCM did not saturate");
    check(saturated[6] == -32768 && saturated[7] == -32768,
          "negative mixed PCM did not saturate");

    ToneAlertOverlay short_alert = {};
    check(tone_alert_overlay_start(&short_alert, "1000:1", false, 1000),
          "short tone alert was rejected");
    int16_t frames[] = {0, 0, 0, 0};
    tone_alert_overlay_mix(&short_alert, frames, 2, 1000);
    check(!short_alert.active, "one-shot tone alert did not complete");

    std::puts("tone alert overlay checks passed");
    return 0;
}