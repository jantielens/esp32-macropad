#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../src/app/tone_alert_overlay.h"
#include "../src/app/loop_feedback.h"

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

    LoopFeedbackMailbox mailbox;
    const uint32_t first_loop = mailbox.replace(true);
    check(mailbox.submit(overlay), "queued loop did not protect feedback");
    check(mailbox.submit(short_alert), "rapid feedback was rejected");
    ToneAlertOverlay received = {};
    check(mailbox.take(first_loop, &received) && !received.active,
          "latest feedback did not replace earlier feedback");
    check(!mailbox.take(first_loop, &received), "feedback was consumed twice");
    const uint32_t second_loop = mailbox.replace(true);
    mailbox.finish(first_loop);
    check(mailbox.matches(second_loop), "old completion unprotected a queued loop");
    check(mailbox.submit(overlay), "new loop rejected feedback");
    check(!mailbox.take(first_loop, &received), "old loop consumed new feedback");
    mailbox.replace(false);
    check(!mailbox.take(second_loop, &received) && !mailbox.submit(overlay),
          "stop or replacement retained loop feedback");

    ToneAlertOverlay loop = {};
    ToneAlertOverlay tap = {};
    check(tone_alert_overlay_start(&loop, "250:4 4", true, 1000), "loop rejected");
    check(tone_alert_overlay_start(&tap, "250:2", false, 1000, 0.25f), "tap rejected");
    int16_t loop_frames[32] = {};
    tone_alert_overlay_mix(&loop, loop_frames, 16, 1000);
    const uint32_t loop_position = loop.frame_index;
    loop_feedback_mix(&tap, loop_frames, 16, 1000);
    check(loop.active && loop.frame_index == loop_position && !tap.active,
          "feedback advanced or stopped the primary tone loop");
    int16_t unchanged_loop[32];
    memcpy(unchanged_loop, loop_frames, sizeof(loop_frames));
    loop_feedback_mix(&tap, loop_frames, 16, 1000);
    check(!memcmp(unchanged_loop, loop_frames, sizeof(loop_frames)),
          "inactive feedback ducked primary PCM");
    check(tone_alert_overlay_start(&tap, "250:4", false, 1000, 0.25f), "tap rejected");
    int16_t loud[] = {32767, -32768, 32767, -32768, 32767, -32768, 32767, -32768};
    loop_feedback_mix(&tap, loud, 4, 1000);
    check(loud[2] < INT16_MAX && loud[3] > INT16_MIN,
          "feedback mix did not reserve headroom");

    std::puts("tone alert overlay checks passed");
    return 0;
}