#include "board_config.h"

#undef HAS_USB_HID
#define HAS_USB_HID TEST_USB_TRANSPORT
#undef HAS_BLE_HID
#define HAS_BLE_HID TEST_BLE_TRANSPORT

#define TEST_HEAP_CAPS_ALLOCATOR
#include "esp_heap_caps.h"
#include "../src/app/actions/key_action.cpp"
#include "../src/app/actions/mouse_button_action.cpp"
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
bool usb_hid_is_ready() { return backend_connected; }
uint32_t usb_hid_epoch() { return test_epoch; }
bool usb_hid_send_report(KsUsageType, uint16_t usage, uint8_t) {
    ++usb_calls;
    usages.push_back(usage);
    return report_succeeds;
}

bool ble_hid_is_initialized() { return true; }
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
    ButtonAction click = {};
    const char* buttons[] = {"left", "right", "middle"};
    for (unsigned index = 0; index < 3; ++index) {
        strlcpy(click.payload.mouse_button.button, buttons[index], sizeof(click.payload.mouse_button.button));
        assert(dispatch_mouse_button(click, "click", 0) == ACTION_COMPLETE);
        mouse_hid_loop();
        assert(last_mouse.buttons == (1 << index));
        mouse_hid_loop();
        assert(last_mouse.buttons == 0);
    }
    const uint32_t pointer_epoch = mouse_hid_epoch();
    const uint32_t pointer_generation = mouse_hid_generation();
    const uint32_t controller_generation = gamepad_hid_generation();
    const uint32_t pointer_owner = mouse_hid_acquire(pointer_epoch, 1);
    const uint32_t controller_owner = gamepad_hid_acquire_stick(0, controller_generation);
    assert(pointer_owner && controller_owner);
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
    assert(registered_actions == 1 + unsigned(TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT));
    assert(mouse_button_available() == bool(TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT));
    ButtonAction mouse_action = {};
    const char* buttons[] = { "left", "right", "middle" };
    for (unsigned index = 0; index < 3; ++index) {
        strlcpy(mouse_action.payload.mouse_button.button, buttons[index], sizeof(mouse_action.payload.mouse_button.button));
        const ActionResult result = dispatch_mouse_button(mouse_action, "mouse test", 0);
        assert(result == ACTION_FAILED);
    }
    assert(keyboard_hid_transport() == keyboard_transport_default(TEST_BLE_TRANSPORT, TEST_USB_TRANSPORT));
    ButtonAction action = {};
    strlcpy(action.payload.key.key_sequence, "a 100ms b", sizeof(action.payload.key.key_sequence));
    assert(!keyboard_hid_is_ready());
    assert(!keyboard_hid_is_busy());
    assert(!keyboard_hid_request_sequence("a", 40));
#if TEST_USB_TRANSPORT || TEST_BLE_TRANSPORT
    assert(key_available());
    assert(dispatch_key(action, "disabled", 40) == ACTION_FAILED);
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
    assert(dispatch_key(action, "test", 42) == ACTION_PENDING);
    assert(allocation_calls == 2 && allocation_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    action.payload.key.key_sequence[0] = '\0';
    assert(!keyboard_hid_request_sequence("c", 43));
    assert(keyboard_hid_is_busy());
    run_until_idle();
    assert(finished_token == 42 && finished_success);
    assert(free_calls == 0 && workspace != nullptr);
    assert(usages.size() == 4 && usages[0] == 4 && usages[1] == 0 && usages[2] == 5 && usages[3] == 0);
    assert(TEST_USB_TRANSPORT ? usb_calls == 4 && ble_calls == 0 : ble_calls == 4 && usb_calls == 0);
    assert(dispatch_key(action, "test", 44) == ACTION_FAILED);
    assert(!keyboard_hid_request_sequence("a", 0));
    assert(keyboard_hid_request_sequence("\"unterminated", 55));
    keyboard_hid_loop();
    assert(finished_token == 55 && !finished_success && !keyboard_hid_is_busy());
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
#if !TEST_USB_TRANSPORT && !TEST_BLE_TRANSPORT
    assert(usb_mouse_calls + ble_mouse_calls == 0);
    assert(usb_gamepad_calls + ble_gamepad_calls == 0);
#endif
    return 0;
}