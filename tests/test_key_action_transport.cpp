#include "board_config.h"

#undef HAS_USB_HID
#define HAS_USB_HID TEST_USB_TRANSPORT
#undef HAS_BLE_HID
#define HAS_BLE_HID TEST_BLE_TRANSPORT

#define TEST_HEAP_CAPS_ALLOCATOR
#include "esp_heap_caps.h"
#include "../src/app/actions/key_action.cpp"
#include "../src/app/actions/mouse_button_action.cpp"
#include "../src/app/actions/gamepad_action.cpp"
#include "../src/app/actions/ble_pair_action.cpp"
#include "../src/app/keyboard_hid.cpp"
#include "../src/app/key_sequence.h"
#include <mutex>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mutex) (mutex)->lock()
#define portEXIT_CRITICAL(mutex) (mutex)->unlock()
#include "../src/app/mouse_hid.cpp"
#include "../src/app/gamepad_hid.cpp"

#include <cassert>
#include <cstring>
#include <vector>

namespace {
uint32_t clock_ms = 0;
bool backend_connected = true;
bool backend_encrypted = true;
bool ble_initialized = true;
#if TEST_BLE_TRANSPORT
unsigned pairing_requests = 0;
#endif
bool report_succeeds = true;
bool ota_active = false;
bool psram_available = true;
bool allocation_succeeds = true;
unsigned allocation_calls = 0;
unsigned free_calls = 0;
unsigned registered_actions = 0;
uint32_t allocation_caps = 0;
uint32_t test_epoch = 1;
unsigned usb_calls = 0;
unsigned ble_calls = 0;
uint32_t finished_token = 0;
bool finished_success = false;
unsigned usb_mouse_calls = 0;
unsigned ble_mouse_calls = 0;
unsigned usb_gamepad_calls = 0;
unsigned ble_gamepad_calls = 0;
#if TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT
MouseHidReport last_mouse;
GamepadReport last_gamepad;
#endif
std::vector<uint16_t> usages;
}

bool psramFound() { return psram_available; }
void* heap_caps_malloc(size_t size, uint32_t caps) {
    ++allocation_calls;
    allocation_caps = caps;
    return allocation_succeeds ? malloc(size) : nullptr;
}
void heap_caps_free(void* ptr) {
    ++free_calls;
    free(ptr);
}

void action_type_register(const ActionTypeDef*) { ++registered_actions; }

unsigned long millis() { return clock_ms; }
bool ota_activity_is_active() { return ota_active; }
uint32_t ota_activity_epoch() { return 1; }
bool action_continuation_complete(uint32_t token, bool success) {
    finished_token = token;
    finished_success = success;
    return true;
}
bool action_continuation_set_timeout(uint32_t token, uint32_t timeout) {
    return token && timeout >= 60000 && timeout <= 300000;
}
bool usb_hid_is_ready() { return backend_connected; }
uint32_t usb_hid_epoch() { return test_epoch; }
bool usb_hid_send_report(KsUsageType, uint16_t usage, uint8_t) {
    ++usb_calls;
    usages.push_back(usage);
    return report_succeeds;
}

bool ble_hid_is_initialized() { return ble_initialized; }
#if TEST_BLE_TRANSPORT
void ble_hid_request_pairing() { ++pairing_requests; }
#endif
bool ble_hid_is_connected() { return backend_connected; }
bool ble_hid_is_encrypted() { return backend_encrypted; }
uint32_t ble_hid_epoch() { return test_epoch; }
bool ble_hid_send_report(KsUsageType, uint16_t usage, uint8_t) {
    ++ble_calls;
    usages.push_back(usage);
    return report_succeeds;
}

#if TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT
bool usb_hid_send_mouse_report(const MouseHidReport& report, uint32_t, uint32_t) {
    ++usb_mouse_calls;
    last_mouse = report;
    return report_succeeds;
}
bool ble_hid_send_mouse_report(const MouseHidReport& report, uint32_t, uint32_t) {
    ++ble_mouse_calls;
    last_mouse = report;
    return report_succeeds;
}
bool usb_hid_send_gamepad_report(const GamepadReport& report, uint32_t, uint32_t, uint32_t) {
    ++usb_gamepad_calls;
    last_gamepad = report;
    return report_succeeds;
}
bool ble_hid_send_gamepad_report(const GamepadReport& report, uint32_t, uint32_t, uint32_t) {
    ++ble_gamepad_calls;
    last_gamepad = report;
    return report_succeeds;
}

void check_pointer_transport(KeyboardTransport transport) {
    keyboard_hid_init(transport);
    mouse_hid_loop();
    gamepad_hid_loop();
    usb_mouse_calls = ble_mouse_calls = usb_gamepad_calls = ble_gamepad_calls = 0;
    assert(mouse_hid_is_ready() && gamepad_hid_is_ready());
    const char* buttons[] = {"left", "right", "middle"};
    for (unsigned index = 0; index < 3; ++index) {
        const ActionResolvedArgument argument{buttons[index], 0, 0};
        assert(kMouseOperations[0].dispatch(&argument, "click operation", 0) == ACTION_COMPLETE);
        mouse_hid_loop();
        assert(last_mouse.buttons == (1 << index));
        mouse_hid_loop();
        assert(last_mouse.buttons == 0);
    }

    const ActionResolvedArgument controls[] = {{nullptr, 16, 0}, {"right", 0, 0}, {"right", 0, 0}};
    for (unsigned family = 0; family < 3; ++family) {
        const ActionOperationDef* operations = kGamepadOperations + family * 3;
        const ActionResolvedArgument* argument = &controls[family];
        assert(operations[1].dispatch(argument, "hold", 0) == ACTION_COMPLETE);
        gamepad_hid_loop();
        if (family == 0) assert(last_gamepad.buttons == 0x8000);
        else if (family == 1) assert(last_gamepad.hat == 3);
        else assert(last_gamepad.right_trigger == 255 && last_gamepad.left_trigger == 0);
        assert(operations[0].dispatch(argument, "held tap", 90 + family) == ACTION_FAILED);
        assert(operations[2].dispatch(argument, "release", 0) == ACTION_COMPLETE);
        gamepad_hid_loop();
        assert(!last_gamepad.buttons && !last_gamepad.hat && !last_gamepad.right_trigger);
        assert(operations[0].dispatch(argument, "no token", 0) == ACTION_FAILED);
        assert(operations[0].dispatch(argument, "tap", 100 + family) == ACTION_PENDING);
        gamepad_hid_loop();
        if (family == 0) assert(last_gamepad.buttons == 0x8000);
        else if (family == 1) assert(last_gamepad.hat == 3);
        else assert(last_gamepad.right_trigger == 255);
        clock_ms += GamepadHidState::tap_duration_ms;
        gamepad_hid_loop();
        assert(!last_gamepad.buttons && !last_gamepad.hat && !last_gamepad.right_trigger);
        assert(finished_token == 100 + family && finished_success);
    }

    const uint32_t pointer_epoch = mouse_hid_epoch();
    const uint32_t pointer_generation = mouse_hid_generation();
    const uint32_t controller_generation = gamepad_hid_generation();
    const uint32_t pointer_owner = mouse_hid_acquire(pointer_epoch, 1);
    const uint32_t controller_owner = gamepad_hid_acquire_stick(0, controller_generation);
    assert(pointer_owner && controller_owner);
    const ActionResolvedArgument left_button{"left", 0, 0};
    assert(kMouseOperations[0].dispatch(&left_button, "owned click", 0) == ACTION_FAILED);
    mouse_hid_move(12, -9, pointer_epoch);
    assert(gamepad_hid_move(controller_owner, 1234, -2345, controller_generation));
    mouse_hid_loop();
    gamepad_hid_loop();
    assert(last_mouse.buttons == 1);
    assert(last_gamepad.left_x == 1234 && last_gamepad.left_y == -2345);
    if (transport == KeyboardTransport::Usb) {
        assert(usb_mouse_calls && usb_gamepad_calls && !ble_mouse_calls && !ble_gamepad_calls);
    } else {
        assert(ble_mouse_calls && ble_gamepad_calls && !usb_mouse_calls && !usb_gamepad_calls);
    }
    backend_connected = false;
    ++test_epoch;
    mouse_hid_loop();
    gamepad_hid_loop();
    assert(!mouse_hid_is_ready() && !gamepad_hid_is_ready());
    backend_connected = true;
    ++test_epoch;
    mouse_hid_loop();
    gamepad_hid_loop();
    assert(mouse_hid_generation() != pointer_generation);
    assert(gamepad_hid_generation() != controller_generation);
    assert(!last_mouse.buttons && !last_gamepad.left_x && !last_gamepad.left_y);
    assert(!gamepad_hid_move(controller_owner, 1, 1, controller_generation));
    assert(!mouse_hid_acquire(pointer_epoch, 1));
    assert(mouse_hid_click(mouse_hid_epoch(), 1));
    assert(gamepad_hid_hold({GamepadControlKind::Button, 0}, true));
    ota_active = true;
    mouse_hid_loop();
    gamepad_hid_loop();
    assert(!last_mouse.buttons && !last_gamepad.buttons);
    ota_active = false;
    keyboard_hid_init(KeyboardTransport::None);
    const unsigned pointer_calls = usb_mouse_calls + ble_mouse_calls;
    const unsigned controller_calls = usb_gamepad_calls + ble_gamepad_calls;
    mouse_hid_loop();
    gamepad_hid_loop();
    assert(!mouse_hid_is_ready() && !gamepad_hid_is_ready());
    assert(usb_mouse_calls + ble_mouse_calls == pointer_calls);
    assert(usb_gamepad_calls + ble_gamepad_calls == controller_calls);
}
#endif

void run_until_idle() {
    for (unsigned iteration = 0; iteration < 1000 && keyboard_hid_is_busy(); ++iteration) {
        keyboard_hid_loop();
        ++clock_ms;
    }
    assert(!keyboard_hid_is_busy());
}

void poll_until_ready() {
    for (unsigned iteration = 0; iteration < 4 && !keyboard_hid_is_ready(); ++iteration) {
        keyboard_hid_loop();
        ++clock_ms;
    }
    assert(keyboard_hid_is_ready());
}

int main() {
    keyboard_hid_init(KeyboardTransport::None);
    keyboard_hid_loop();
    assert(registered_actions == 3 + unsigned(TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT));
    assert(mouse_button_available() == bool(TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT));
    const char* buttons[] = { "left", "right", "middle" };
    for (unsigned index = 0; index < 3; ++index) {
        const ActionResolvedArgument argument{buttons[index], 0, 0};
        const ActionResult result = kMouseOperations[0].dispatch(&argument, "mouse test", 0);
        assert(result == ACTION_FAILED);
    }
    assert(keyboard_hid_transport() == keyboard_transport_default(TEST_BLE_TRANSPORT, TEST_USB_TRANSPORT));
#if TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT
    char action_sequence[] = "a 100ms b";
    ActionResolvedArgument arguments[] = {{action_sequence, 0, 0}};
#endif
    assert(!keyboard_hid_is_ready());
    assert(!keyboard_hid_is_busy());
    assert(!keyboard_hid_request_sequence("a", 40));
#if TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT
    assert(key_available());
    assert(dispatch_key_operation(arguments, "disabled", 40) == ACTION_FAILED);
#endif
    assert(usb_calls == 0 && ble_calls == 0);
    assert(strcmp(keyboard_hid_status(), TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT ? "disabled" : "unavailable") == 0);
    assert(allocation_calls == 0);
#if TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT
    keyboard_hid_init(TEST_USB_TRANSPORT ? KeyboardTransport::Usb : KeyboardTransport::Ble);
    keyboard_hid_loop();
    assert(!keyboard_hid_is_ready() && usb_calls + ble_calls == 1);
    keyboard_hid_loop();
    assert(keyboard_hid_is_ready() && usb_calls + ble_calls == 2);
    usb_calls = ble_calls = 0;
    usages.clear();
    char oversized_sequence[257];
    memset(oversized_sequence, 'a', sizeof(oversized_sequence) - 1);
    oversized_sequence[sizeof(oversized_sequence) - 1] = '\0';
    assert(!keyboard_hid_request_sequence(oversized_sequence, 39));
    assert(allocation_calls == 0);
    allocation_succeeds = false;
    assert(!keyboard_hid_request_sequence("a", 41));
    assert(allocation_calls == 1 && allocation_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(!keyboard_hid_is_busy() && keyboard_hid_is_ready());
    keyboard_hid_loop();
    assert(finished_token == 0 && usb_calls == 0 && ble_calls == 0);
    allocation_succeeds = true;
    assert(dispatch_key_operation(arguments, "test", 42) == ACTION_PENDING);
    assert(allocation_calls == 2 && allocation_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    action_sequence[0] = '\0';
    assert(!keyboard_hid_request_sequence("c", 43));
    assert(keyboard_hid_is_busy());
    run_until_idle();
    assert(finished_token == 42 && finished_success);
    assert(free_calls == 0 && workspace != nullptr);
    assert(usages.size() == 4 && usages[0] == 4 && usages[1] == 0 && usages[2] == 5 && usages[3] == 0);
    assert(TEST_USB_TRANSPORT ? usb_calls == 4 && ble_calls == 0 : ble_calls == 4 && usb_calls == 0);
    assert(dispatch_key_operation(arguments, "test", 44) == ACTION_FAILED);
    assert(!keyboard_hid_request_sequence("a", 0));
    const uint32_t token_before_invalid = finished_token;
    assert(!keyboard_hid_request_sequence("\"unterminated", 55));
    keyboard_hid_loop();
    assert(finished_token == token_before_invalid && !keyboard_hid_is_busy());
    assert(allocation_calls == 2);
    poll_until_ready();

    assert(keyboard_hid_request_sequence("a 500ms b", 45));
    keyboard_hid_loop();
    backend_connected = false;
    ++test_epoch;
    keyboard_hid_loop();
    assert(finished_token == 45 && !finished_success);
    backend_connected = true;
    ++test_epoch;
    clock_ms += 1000;
    poll_until_ready();
    assert(!keyboard_hid_is_busy() && keyboard_hid_is_ready());
    assert(usages.back() == 0);

    assert(keyboard_hid_request_sequence("a", 46));
    ++test_epoch;
    keyboard_hid_loop();
    assert(finished_token == 46 && !finished_success);
    poll_until_ready();

    assert(keyboard_hid_request_sequence("a", 47));
    ota_active = true;
    keyboard_hid_loop();
    assert(finished_token == 47 && !finished_success);
    assert(!keyboard_hid_request_sequence("a", 48));
    ota_active = false;
    poll_until_ready();

    assert(keyboard_hid_request_sequence("a", 49));
    report_succeeds = false;
    keyboard_hid_loop();
    assert(finished_token == 49 && !finished_success);
    const unsigned calls_before_release = usb_calls + ble_calls;
    keyboard_hid_loop();
    assert(usb_calls + ble_calls == calls_before_release + 1);
    assert(!keyboard_hid_is_ready());
    report_succeeds = true;
    poll_until_ready();

    assert(keyboard_hid_request_sequence("65535ms a", 50));
    keyboard_hid_loop();
    clock_ms += ACTION_CONTINUATION_TIMEOUT_MS;
    keyboard_hid_loop();
    assert(finished_token == 50 && !finished_success);
    assert(allocation_calls == 2);
    poll_until_ready();
    usages.clear();
    const unsigned calls_before_invalid = usb_calls + ble_calls;
    assert(!keyboard_hid_request_text("a\xf0\x9f\x98\x80", 160));
    assert(!keyboard_hid_is_busy() && usb_calls + ble_calls == calls_before_invalid);
    char literal[KEYBOARD_TEXT_MAX_BYTES + 1];
    memset(literal, 'a', sizeof(literal) - 1);
    literal[sizeof(literal) - 1] = '\0';
    assert(keyboard_hid_request_text(literal, 161));
    literal[0] = 'b';
    assert(!keyboard_hid_request_text("busy", 162));
    for (unsigned i = 0; keyboard_hid_is_busy() && i < 10000; ++i) {
        keyboard_hid_loop();
        clock_ms += 20;
    }
    assert(!keyboard_hid_is_busy() && finished_token == 161 && finished_success);
    assert(usages.size() == 4096 && usages.front() == 0x04);
    assert(keyboard_hid_completed_bytes() == KEYBOARD_TEXT_MAX_BYTES &&
           keyboard_hid_total_bytes() == KEYBOARD_TEXT_MAX_BYTES &&
           !strcmp(keyboard_hid_output_status(), "completed"));
    assert(keyboard_hid_request_text("ab", 163));
    keyboard_hid_loop();
    keyboard_hid_cancel();
    keyboard_hid_loop();
    assert(finished_token == 163 && !finished_success &&
           !strcmp(keyboard_hid_output_status(), "cancelled"));
    poll_until_ready();
    assert(usages.back() == 0);
    assert(keyboard_hid_request_text("ab", 164));
    keyboard_hid_cancel();
    keyboard_hid_loop();
    assert(finished_token == 164 && !finished_success);
    poll_until_ready();
    assert(keyboard_hid_request_text("ab", 165));
    clock_ms += workspace->executor.timeout_ms();
    keyboard_hid_loop();
    assert(finished_token == 165 && !finished_success &&
           !strcmp(keyboard_hid_output_status(), "timeout"));
    poll_until_ready();
    keyboard_hid_init(TEST_USB_TRANSPORT ? KeyboardTransport::Usb : KeyboardTransport::Ble, KeyboardLayout::Be);
    assert(keyboard_hid_request_text("a", 166));
    run_until_idle();
    assert(finished_token == 166 && finished_success && usages[usages.size() - 2] == 0x14);
    keyboard_hid_init(TEST_USB_TRANSPORT ? KeyboardTransport::Usb : KeyboardTransport::Ble);

    workspace->~KeyboardWorkspace();
    heap_caps_free(workspace);
    workspace = nullptr;
    psram_available = false;
    poll_until_ready();
    allocation_succeeds = false;
    assert(!keyboard_hid_request_sequence("a", 53));
    assert(allocation_calls == 3 && allocation_caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    assert(!keyboard_hid_is_busy());
    allocation_succeeds = true;
    assert(keyboard_hid_request_sequence("\"ab\"", 54));
    run_until_idle();
    assert(finished_token == 54 && finished_success);
    assert(allocation_calls == 4 && allocation_caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    assert(workspace == nullptr && free_calls == 2);
    assert(keyboard_hid_request_sequence("a 500ms b", 56));
    keyboard_hid_loop();
    ota_active = true;
    keyboard_hid_loop();
    assert(finished_token == 56 && !finished_success);
    assert(workspace == nullptr && free_calls == 3);
    ota_active = false;
    poll_until_ready();
    assert(keyboard_hid_request_sequence("a", 57));
    run_until_idle();
    assert(finished_token == 57 && finished_success);
    assert(workspace == nullptr && free_calls == 4 && allocation_calls == 6);

#if TEST_USB_TRANSPORT && TEST_BLE_TRANSPORT
    keyboard_hid_init(KeyboardTransport::Ble);
    keyboard_hid_loop();
    usb_calls = ble_calls = 0;
    assert(keyboard_hid_request_sequence("a", 51));
    run_until_idle();
    assert(finished_success && ble_calls == 2 && usb_calls == 0);
    assert(allocation_calls == 7 && free_calls == 5);
#endif
#if TEST_BLE_TRANSPORT
    if (keyboard_hid_transport() == KeyboardTransport::Ble) {
        backend_encrypted = false;
        keyboard_hid_loop();
        assert(!keyboard_hid_is_ready());
        assert(!keyboard_hid_request_sequence("a", 52));
        backend_encrypted = true;
        poll_until_ready();
        assert(keyboard_hid_is_ready());
    }
#endif
    assert(workspace == nullptr);
#else
    assert(!keyboard_hid_is_ready());
    assert(usb_calls == 0 && ble_calls == 0);
    assert(allocation_calls == 0);
#endif
#if TEST_USB_TRANSPORT
    check_pointer_transport(KeyboardTransport::Usb);
#endif
#if TEST_BLE_TRANSPORT
    check_pointer_transport(KeyboardTransport::Ble);
#endif
#if TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT
    keyboard_hid_init(TEST_BLE_TRANSPORT ? KeyboardTransport::Ble : KeyboardTransport::Usb);
    poll_until_ready();
    char sequence[] = "a";
    const ActionResolvedArgument key_argument{sequence, 0, 0};
    assert(kKeyOperations[0].dispatch(&key_argument, "no token", 0) == ACTION_FAILED);
    assert(kKeyOperations[0].dispatch(&key_argument, "keys", 130) == ACTION_PENDING);
    sequence[0] = 'b';
    assert(kKeyOperations[0].dispatch(&key_argument, "busy", 131) == ACTION_FAILED);
#if TEST_BLE_TRANSPORT
    assert(kBlePairOperations[0].dispatch(nullptr, "busy", 0) == ACTION_FAILED);
    assert(pairing_requests == 0);
#endif
    usages.clear();
    run_until_idle();
    assert(finished_token == 130 && finished_success);
    assert(usages.size() >= 2 && usages[usages.size() - 2] == 4 && usages.back() == 0);
    char max_sequence[CONFIG_KEY_SEQ_MAX_LEN];
    memset(max_sequence, 'a', sizeof(max_sequence) - 1);
    max_sequence[0] = '"';
    max_sequence[sizeof(max_sequence) - 2] = '"';
    max_sequence[sizeof(max_sequence) - 1] = '\0';
    const ActionResolvedArgument max_key_argument{max_sequence, 0, 0};
    assert(kKeyOperations[0].dispatch(&max_key_argument, "max sequence", 132) == ACTION_PENDING);
    assert(workspace && workspace->executor.total_bytes() == strlen(max_sequence));
    ota_active = true;
    keyboard_hid_loop();
    assert(finished_token == 132 && !finished_success);
    ota_active = false;
    poll_until_ready();
    char too_long[CONFIG_KEY_SEQ_MAX_LEN + 1];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    const ActionResolvedArgument too_long_argument{too_long, 0, 0};
    assert(kKeyOperations[0].dispatch(&too_long_argument, "oversized sequence", 133) == ACTION_FAILED);
#endif
#if TEST_BLE_TRANSPORT
    ble_initialized = false;
    assert(kBlePairOperations[0].dispatch(nullptr, "uninitialized", 0) == ACTION_FAILED);
    ble_initialized = true;
    assert(kBlePairOperations[0].dispatch(nullptr, "pair", 0) == ACTION_COMPLETE);
    assert(pairing_requests == 1);
    keyboard_hid_init(KeyboardTransport::None);
    assert(kBlePairOperations[0].dispatch(nullptr, "wrong transport", 0) == ACTION_FAILED);
    assert(pairing_requests == 1);
#else
    assert(kBlePairOperations[0].dispatch(nullptr, "unavailable", 0) == ACTION_FAILED);
#endif
#if !TEST_USB_TRANSPORT && !TEST_BLE_TRANSPORT
    assert(usb_mouse_calls + ble_mouse_calls == 0);
    assert(usb_gamepad_calls + ble_gamepad_calls == 0);
    const ActionResolvedArgument button{"left", 0, 0};
    const ActionResolvedArgument gamepad_button{nullptr, 1, 0};
    assert(kMouseOperations[0].dispatch(&button, "unavailable", 0) == ACTION_FAILED);
    assert(kGamepadOperations[0].dispatch(&gamepad_button, "unavailable", 140) == ACTION_FAILED);
    assert(kGamepadOperations[1].dispatch(&gamepad_button, "unavailable", 0) == ACTION_FAILED);
#endif
    return 0;
}