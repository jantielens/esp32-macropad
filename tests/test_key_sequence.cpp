// ============================================================================
// Unit tests for key_sequence — DSL parser
// ============================================================================
// No Arduino, no ESP32. Compiles and runs on any host.
//
// Build:
//   g++ -std=c++17 tests/test_key_sequence.cpp src/app/key_sequence.cpp
//       src/app/key_sequence_executor.cpp
//       -o tests/bin/test_key_sequence
// Run:
//   ./tests/bin/test_key_sequence

#include "../src/app/key_sequence.h"
#include "../src/app/key_sequence_executor.h"
#include "../src/app/keyboard_transport.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

// ============================================================================
// Helpers
// ============================================================================

static void check_parse_ok(const char* dsl, int expected_steps, const char* label) {
    KsSequence seq;
    bool ok = ks_parse(dsl, &seq);
    if (!ok) {
        printf("  FAIL [%s]: ks_parse(\"%s\") failed: %s\n", label, dsl, seq.error);
        g_fail++;
    } else if (seq.count != expected_steps) {
        printf("  FAIL [%s]: ks_parse(\"%s\") count=%d, expected %d\n", label, dsl, seq.count, expected_steps);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void check_parse_err(const char* dsl, const char* label) {
    KsSequence seq;
    bool ok = ks_parse(dsl, &seq);
    if (ok) {
        printf("  FAIL [%s]: ks_parse(\"%s\") should have failed but succeeded with %d steps\n", label, dsl, seq.count);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void check_step_key(const KsStep& step, uint16_t usage, uint8_t mods, KsUsageType ut, const char* label) {
    if (step.type != KS_STEP_KEY) {
        printf("  FAIL [%s]: expected KEY step, got type %d\n", label, step.type);
        g_fail++;
    } else if (step.key.usage != usage || step.key.modifiers != mods || step.key.usage_type != ut) {
        printf("  FAIL [%s]: key usage=0x%04X mod=0x%02X type=%d, expected 0x%04X 0x%02X %d\n",
               label, step.key.usage, step.key.modifiers, step.key.usage_type,
               usage, mods, ut);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void check_step_text(const KsStep& step, const char* expected, const char* label) {
    if (step.type != KS_STEP_TEXT) {
        printf("  FAIL [%s]: expected TEXT step, got type %d\n", label, step.type);
        g_fail++;
    } else if (step.text.length != (uint16_t)strlen(expected) ||
               memcmp(step.text.start, expected, step.text.length) != 0) {
        printf("  FAIL [%s]: text = \"%.*s\", expected \"%s\"\n",
               label, step.text.length, step.text.start, expected);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void check_step_delay(const KsStep& step, uint16_t ms, const char* label) {
    if (step.type != KS_STEP_DELAY) {
        printf("  FAIL [%s]: expected DELAY step, got type %d\n", label, step.type);
        g_fail++;
    } else if (step.delay.ms != ms) {
        printf("  FAIL [%s]: delay=%d, expected %d\n", label, step.delay.ms, ms);
        g_fail++;
    } else {
        g_pass++;
    }
}

// ============================================================================
// Test groups
// ============================================================================

static void test_simple_keys() {
    printf("--- Simple keys ---\n");

    KsSequence seq;

    // Single letter
    check_parse_ok("a", 1, "letter a");
    ks_parse("a", &seq);
    check_step_key(seq.steps[0], 0x04, 0, KS_USAGE_KEYBOARD, "a → HID 0x04");

    // Single letter Z
    check_parse_ok("z", 1, "letter z");
    ks_parse("z", &seq);
    check_step_key(seq.steps[0], 0x1D, 0, KS_USAGE_KEYBOARD, "z → HID 0x1D");

    // Digit
    check_parse_ok("0", 1, "digit 0");
    ks_parse("0", &seq);
    check_step_key(seq.steps[0], 0x27, 0, KS_USAGE_KEYBOARD, "0 → HID 0x27");

    check_parse_ok("1", 1, "digit 1");
    ks_parse("1", &seq);
    check_step_key(seq.steps[0], 0x1E, 0, KS_USAGE_KEYBOARD, "1 → HID 0x1E");

    // Named keys
    check_parse_ok("enter", 1, "enter");
    ks_parse("enter", &seq);
    check_step_key(seq.steps[0], 0x28, 0, KS_USAGE_KEYBOARD, "enter → HID 0x28");

    check_parse_ok("esc", 1, "esc alias");
    check_parse_ok("escape", 1, "escape full");
    check_parse_ok("tab", 1, "tab");
    check_parse_ok("space", 1, "space");
    check_parse_ok("backspace", 1, "backspace");
    check_parse_ok("bksp", 1, "bksp alias");
    check_parse_ok("delete", 1, "delete");
    check_parse_ok("del", 1, "del alias");

    // F-keys
    check_parse_ok("f1", 1, "f1");
    ks_parse("f1", &seq);
    check_step_key(seq.steps[0], 0x3A, 0, KS_USAGE_KEYBOARD, "f1 → HID 0x3A");

    check_parse_ok("f12", 1, "f12");
    ks_parse("f12", &seq);
    check_step_key(seq.steps[0], 0x45, 0, KS_USAGE_KEYBOARD, "f12 → HID 0x45");

    check_parse_ok("f24", 1, "f24");

    // Navigation
    check_parse_ok("up", 1, "up");
    check_parse_ok("down", 1, "down");
    check_parse_ok("left", 1, "left");
    check_parse_ok("right", 1, "right");
    check_parse_ok("home", 1, "home");
    check_parse_ok("end", 1, "end");
    check_parse_ok("pageup", 1, "pageup");
    check_parse_ok("pgup", 1, "pgup alias");
    check_parse_ok("pgdn", 1, "pgdn alias");

    // Case insensitive
    check_parse_ok("ENTER", 1, "ENTER uppercase");
    check_parse_ok("Enter", 1, "Enter mixed case");
    check_parse_ok("F5", 1, "F5 uppercase");
}

static void test_modifier_combos() {
    printf("--- Modifier combos ---\n");

    KsSequence seq;

    // ctrl+c
    check_parse_ok("ctrl+c", 1, "ctrl+c");
    ks_parse("ctrl+c", &seq);
    check_step_key(seq.steps[0], 0x06, KS_MOD_LCTRL, KS_USAGE_KEYBOARD, "ctrl+c mods");

    // ctrl+shift+t
    check_parse_ok("ctrl+shift+t", 1, "ctrl+shift+t");
    ks_parse("ctrl+shift+t", &seq);
    check_step_key(seq.steps[0], 0x17, KS_MOD_LCTRL | KS_MOD_LSHIFT, KS_USAGE_KEYBOARD, "ctrl+shift+t mods");

    // alt+f4
    check_parse_ok("alt+f4", 1, "alt+f4");
    ks_parse("alt+f4", &seq);
    check_step_key(seq.steps[0], 0x3D, KS_MOD_LALT, KS_USAGE_KEYBOARD, "alt+f4 mods");

    // gui+l  (win alias)
    check_parse_ok("gui+l", 1, "gui+l");
    ks_parse("gui+l", &seq);
    check_step_key(seq.steps[0], 0x0F, KS_MOD_LGUI, KS_USAGE_KEYBOARD, "gui+l mods");

    check_parse_ok("win+l", 1, "win+l alias");
    ks_parse("win+l", &seq);
    check_step_key(seq.steps[0], 0x0F, KS_MOD_LGUI, KS_USAGE_KEYBOARD, "win+l mods");

    check_parse_ok("cmd+l", 1, "cmd+l alias");
    check_parse_ok("super+l", 1, "super+l alias");

    // ctrl+alt+delete
    check_parse_ok("ctrl+alt+delete", 1, "ctrl+alt+delete");
    ks_parse("ctrl+alt+delete", &seq);
    check_step_key(seq.steps[0], 0x4C, KS_MOD_LCTRL | KS_MOD_LALT, KS_USAGE_KEYBOARD, "ctrl+alt+del mods");

    // Right-side modifiers
    check_parse_ok("rctrl+a", 1, "rctrl+a");
    ks_parse("rctrl+a", &seq);
    check_step_key(seq.steps[0], 0x04, KS_MOD_RCTRL, KS_USAGE_KEYBOARD, "rctrl+a mods");

    check_parse_ok("altgr+a", 1, "altgr alias");
    ks_parse("altgr+a", &seq);
    check_step_key(seq.steps[0], 0x04, KS_MOD_RALT, KS_USAGE_KEYBOARD, "altgr+a mods");

    // Modifier-only (no key, just modifier press)
    check_parse_ok("ctrl+shift", 1, "modifier-only combo");
    ks_parse("ctrl+shift", &seq);
    check_step_key(seq.steps[0], 0, KS_MOD_LCTRL | KS_MOD_LSHIFT, KS_USAGE_KEYBOARD, "ctrl+shift mods-only");
}

static void test_media_keys() {
    printf("--- Media keys ---\n");

    KsSequence seq;

    check_parse_ok("play_pause", 1, "play_pause");
    ks_parse("play_pause", &seq);
    check_step_key(seq.steps[0], 0x00CD, 0, KS_USAGE_CONSUMER, "play_pause consumer");

    check_parse_ok("vol_up", 1, "vol_up");
    ks_parse("vol_up", &seq);
    check_step_key(seq.steps[0], 0x00E9, 0, KS_USAGE_CONSUMER, "vol_up consumer");

    check_parse_ok("vol_down", 1, "vol_down");
    check_parse_ok("mute", 1, "mute");
    check_parse_ok("next_track", 1, "next_track");
    check_parse_ok("prev_track", 1, "prev_track");
    check_parse_ok("stop", 1, "stop");
}

static void test_text_literals() {
    printf("--- Text literals ---\n");

    KsSequence seq;

    check_parse_ok("\"hello\"", 1, "simple text");
    ks_parse("\"hello\"", &seq);
    check_step_text(seq.steps[0], "hello", "hello text");

    check_parse_ok("\"Hello World\"", 1, "text with space");
    ks_parse("\"Hello World\"", &seq);
    check_step_text(seq.steps[0], "Hello World", "Hello World text");

    // Text with escaped quote
    check_parse_ok("\"say \\\"hi\\\"\"", 1, "text with escaped quotes");
    ks_parse("\"say \\\"hi\\\"\"", &seq);
    check_step_text(seq.steps[0], "say \\\"hi\\\"", "escaped quotes raw");

    // Empty text
    check_parse_ok("\"\"", 1, "empty text");
    ks_parse("\"\"", &seq);
    check_step_text(seq.steps[0], "", "empty text content");
}

static void test_delays() {
    printf("--- Delays ---\n");

    KsSequence seq;

    check_parse_ok("200ms", 1, "200ms");
    ks_parse("200ms", &seq);
    check_step_delay(seq.steps[0], 200, "200ms value");

    check_parse_ok("50ms", 1, "50ms");
    ks_parse("50ms", &seq);
    check_step_delay(seq.steps[0], 50, "50ms value");

    check_parse_ok("1000ms", 1, "1000ms");
    ks_parse("1000ms", &seq);
    check_step_delay(seq.steps[0], 1000, "1000ms value");

    check_parse_ok("0ms", 1, "0ms");
    ks_parse("0ms", &seq);
    check_step_delay(seq.steps[0], 0, "0ms value");
}

static void test_macro_sequences() {
    printf("--- Macro sequences ---\n");

    KsSequence seq;

    // ctrl+c ctrl+v
    check_parse_ok("ctrl+c ctrl+v", 2, "copy-paste");
    ks_parse("ctrl+c ctrl+v", &seq);
    check_step_key(seq.steps[0], 0x06, KS_MOD_LCTRL, KS_USAGE_KEYBOARD, "copy");
    check_step_key(seq.steps[1], 0x19, KS_MOD_LCTRL, KS_USAGE_KEYBOARD, "paste");

    // ctrl+c 50ms ctrl+v
    check_parse_ok("ctrl+c 50ms ctrl+v", 3, "copy-delay-paste");
    ks_parse("ctrl+c 50ms ctrl+v", &seq);
    check_step_key(seq.steps[0], 0x06, KS_MOD_LCTRL, KS_USAGE_KEYBOARD, "copy2");
    check_step_delay(seq.steps[1], 50, "50ms gap");
    check_step_key(seq.steps[2], 0x19, KS_MOD_LCTRL, KS_USAGE_KEYBOARD, "paste2");

    // gui+r 200ms "notepad" enter
    check_parse_ok("gui+r 200ms \"notepad\" enter", 4, "run-notepad macro");
    ks_parse("gui+r 200ms \"notepad\" enter", &seq);
    check_step_key(seq.steps[0], 0x15, KS_MOD_LGUI, KS_USAGE_KEYBOARD, "win+r");
    check_step_delay(seq.steps[1], 200, "200ms after win+r");
    check_step_text(seq.steps[2], "notepad", "notepad text");
    check_step_key(seq.steps[3], 0x28, 0, KS_USAGE_KEYBOARD, "enter after text");

    // Mixed: key + text + delay + media
    check_parse_ok("ctrl+a \"hello\" 100ms play_pause", 4, "mixed macro");
}

static void test_ascii_to_hid() {
    printf("--- ASCII to HID ---\n");

    uint16_t usage;
    uint8_t mods;

    // Lowercase letter
    if (ks_ascii_to_hid('a', &usage, &mods) && usage == 0x04 && mods == 0) g_pass++; else { printf("  FAIL: 'a'\n"); g_fail++; }
    if (ks_ascii_to_hid('z', &usage, &mods) && usage == 0x1D && mods == 0) g_pass++; else { printf("  FAIL: 'z'\n"); g_fail++; }

    // Uppercase = shift
    if (ks_ascii_to_hid('A', &usage, &mods) && usage == 0x04 && mods == KS_MOD_LSHIFT) g_pass++; else { printf("  FAIL: 'A'\n"); g_fail++; }

    // Digits
    if (ks_ascii_to_hid('0', &usage, &mods) && usage == 0x27 && mods == 0) g_pass++; else { printf("  FAIL: '0'\n"); g_fail++; }
    if (ks_ascii_to_hid('1', &usage, &mods) && usage == 0x1E && mods == 0) g_pass++; else { printf("  FAIL: '1'\n"); g_fail++; }

    // Space
    if (ks_ascii_to_hid(' ', &usage, &mods) && usage == 0x2C && mods == 0) g_pass++; else { printf("  FAIL: ' '\n"); g_fail++; }

    // Shifted symbols
    if (ks_ascii_to_hid('!', &usage, &mods) && usage == 0x1E && mods == KS_MOD_LSHIFT) g_pass++; else { printf("  FAIL: '!'\n"); g_fail++; }
    if (ks_ascii_to_hid('@', &usage, &mods) && usage == 0x1F && mods == KS_MOD_LSHIFT) g_pass++; else { printf("  FAIL: '@'\n"); g_fail++; }

    // Non-shifted punctuation
    if (ks_ascii_to_hid('-', &usage, &mods) && usage == 0x2D && mods == 0) g_pass++; else { printf("  FAIL: '-'\n"); g_fail++; }
    if (ks_ascii_to_hid('=', &usage, &mods) && usage == 0x2E && mods == 0) g_pass++; else { printf("  FAIL: '='\n"); g_fail++; }

    // Out of range
    if (!ks_ascii_to_hid('\n', &usage, &mods)) g_pass++; else { printf("  FAIL: '\\n' should fail\n"); g_fail++; }
    if (!ks_ascii_to_hid('\x7F', &usage, &mods)) g_pass++; else { printf("  FAIL: DEL should fail\n"); g_fail++; }
}

static void test_error_cases() {
    printf("--- Error cases ---\n");

    check_parse_err("", "empty string");
    check_parse_err("  ", "whitespace only");
    check_parse_err("unknownkey", "unknown key");
    check_parse_err("ctrl+unknownkey", "unknown key in combo");
    check_parse_err("\"unterminated", "unterminated string");
    check_parse_err("ctrl++c", "empty segment in combo");
}

static void test_whitespace_handling() {
    printf("--- Whitespace handling ---\n");

    // Leading/trailing whitespace
    check_parse_ok("  enter  ", 1, "leading/trailing spaces");

    // Multiple spaces between tokens
    check_parse_ok("ctrl+c   ctrl+v", 2, "multiple spaces");

    // Tab separators
    check_parse_ok("ctrl+c\tctrl+v", 2, "tab separator");
}

// ============================================================================
// Main
// ============================================================================

struct ExecutorReport {
    KsUsageType type;
    uint16_t usage;
    uint8_t modifiers;
};

static void test_executor() {
    std::vector<ExecutorReport> reports;
    const auto sink = [](void* context, KsUsageType type, uint16_t usage, uint8_t modifiers) {
        static_cast<std::vector<ExecutorReport>*>(context)->push_back({type, usage, modifiers});
        return true;
    };
    KeySequenceExecutor executor;
    char sequence[] = "ctrl+a 100ms \"B\" enter";
    assert(executor.start(sequence, 0));
    sequence[0] = 'x';
    assert(!executor.start("a", 0));
    assert(executor.poll(0, sink, &reports) == KeySequenceExecutor::Result::Running);
    assert(reports.size() == 1 && reports[0].usage == 0x04 && reports[0].modifiers == KS_MOD_LCTRL);
    executor.poll(19, sink, &reports);
    assert(reports.size() == 1);
    executor.poll(20, sink, &reports);
    assert(reports.size() == 2 && reports.back().usage == 0 && reports.back().modifiers == 0);
    executor.poll(20, sink, &reports);
    executor.poll(119, sink, &reports);
    assert(reports.size() == 2);
    executor.poll(120, sink, &reports);
    assert(reports.back().usage == 0x05 && reports.back().modifiers == KS_MOD_LSHIFT);
    executor.poll(140, sink, &reports);
    executor.poll(159, sink, &reports);
    assert(reports.size() == 4);
    executor.poll(160, sink, &reports);
    assert(reports.back().usage == 0x28);
    executor.poll(180, sink, &reports);
    assert(executor.poll(180, sink, &reports) == KeySequenceExecutor::Result::Success);
    assert(reports.size() == 6);

    reports.clear();
    assert(executor.start("\"aa\\\"\" mute", 0));
    for (uint32_t now = 0; now < 200 && executor.active(); ++now) {
        executor.poll(now, sink, &reports);
    }
    assert(!executor.active() && reports.size() == 8);
    assert(reports[0].usage == 0x04 && reports[2].usage == 0x04);
    assert(reports[4].usage == 0x34 && reports[4].modifiers == KS_MOD_LSHIFT);
    assert(reports[6].type == KS_USAGE_CONSUMER && reports[6].usage == 0xE2);
    assert(reports[7].type == KS_USAGE_CONSUMER && reports[7].usage == 0);

    assert(executor.start("a", UINT32_MAX - 10));
    executor.poll(UINT32_MAX - 10, sink, &reports);
    const size_t before_release = reports.size();
    executor.poll(8, sink, &reports);
    assert(reports.size() == before_release);
    executor.poll(9, sink, &reports);
    assert(reports.size() == before_release + 1);
    executor.cancel(sink, &reports);
    assert(!executor.active());

    assert(executor.start("ctrl+a", 0));
    executor.poll(0, sink, &reports);
    executor.cancel(sink, &reports);
    assert(reports.back().usage == 0 && reports.back().modifiers == 0);
    assert(!executor.active());
    assert(!executor.start("no_such_key", 0));
    assert(!executor.start("", 0));
    char oversized[257];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    assert(!executor.start(oversized, 0));
    assert(executor.start("a", 0));
    const auto failing_sink = [](void*, KsUsageType, uint16_t, uint8_t) { return false; };
    assert(executor.poll(0, failing_sink, nullptr) == KeySequenceExecutor::Result::Failed);
    assert(!executor.active());

    assert(executor.start("ctrl+a", 0));
    executor.poll(0, sink, &reports);
    assert(executor.poll(20, failing_sink, nullptr) == KeySequenceExecutor::Result::Failed);
    assert(!executor.active());
    assert(executor.poll(40, sink, &reports) == KeySequenceExecutor::Result::Idle);

    reports.clear();
    assert(executor.start("1000ms a", 0));
    executor.poll(0, sink, &reports);
    executor.cancel(sink, &reports);
    assert(executor.poll(2000, sink, &reports) == KeySequenceExecutor::Result::Idle);
    assert(reports.empty());
    g_pass++;
}

int main() {
    char device_name[40];
    keyboard_transport_device_name(device_name, sizeof(device_name), "Kitchen Pad", KeyboardTransport::Usb);
    assert(strcmp(device_name, "Kitchen Pad USB") == 0);
    keyboard_transport_device_name(device_name, sizeof(device_name), "Kitchen Pad", KeyboardTransport::Ble);
    assert(strcmp(device_name, "Kitchen Pad BLE") == 0);
    keyboard_transport_device_name(device_name, sizeof(device_name), nullptr, KeyboardTransport::Ble);
    assert(strcmp(device_name, "Keyboard BLE") == 0);
    keyboard_transport_device_name(device_name, sizeof(device_name), "", KeyboardTransport::Usb);
    assert(strcmp(device_name, "Keyboard USB") == 0);
    const char* maximum_device_name = "1234567890123456789012345678901";
    keyboard_transport_device_name(device_name, sizeof(device_name), maximum_device_name, KeyboardTransport::Usb);
    assert(strcmp(device_name, "1234567890123456789012345678901 USB") == 0);
    keyboard_transport_device_name(device_name, sizeof(device_name), maximum_device_name, KeyboardTransport::Ble);
    assert(strcmp(device_name, "1234567890123456789012345678901 BLE") == 0);
    for (bool has_ble : {false, true}) {
        for (bool has_usb : {false, true}) {
            const KeyboardTransport expected = KeyboardTransport::None;
            assert(keyboard_transport_default(has_ble, has_usb) == expected);
            assert(keyboard_transport_resolve(KeyboardTransport::None, has_ble, has_usb) == expected);
            assert(keyboard_transport_resolve(static_cast<KeyboardTransport>(99), has_ble, has_usb) == expected);
            assert(keyboard_transport_resolve(KeyboardTransport::Ble, has_ble, has_usb) ==
                     (has_ble ? KeyboardTransport::Ble : expected));
                 assert(keyboard_transport_resolve(KeyboardTransport::Usb, has_ble, has_usb) ==
                     (has_usb ? KeyboardTransport::Usb : expected));
        }
    }
    KeyboardTransport parsed_transport;
    assert(keyboard_transport_parse("none", &parsed_transport) && parsed_transport == KeyboardTransport::None);
    assert(keyboard_transport_parse("usb", &parsed_transport) && parsed_transport == KeyboardTransport::Usb);
    assert(keyboard_transport_parse("ble", &parsed_transport) && parsed_transport == KeyboardTransport::Ble);
    assert(!keyboard_transport_parse("auto", &parsed_transport));
    assert(!keyboard_transport_parse(nullptr, &parsed_transport));
    test_executor();
    printf("=== Key Sequence DSL Parser Tests ===\n\n");

    test_simple_keys();
    test_modifier_combos();
    test_media_keys();
    test_text_literals();
    test_delays();
    test_macro_sequences();
    test_ascii_to_hid();
    test_error_cases();
    test_whitespace_handling();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
