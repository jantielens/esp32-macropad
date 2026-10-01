#include "keyboard_hid.h"

#if (HAS_DISPLAY || HAS_BUTTON) && (HAS_BLE_HID || HAS_USB_HID)

#include "action_continuation.h"
#include "key_sequence_executor.h"
#include "log_manager.h"
#include "ota_activity.h"
#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <new>

#if HAS_USB_HID
#include "usb_hid.h"
#endif
#if HAS_BLE_HID
#include "ble_hid.h"
#endif

namespace {
constexpr const char* kKeyboardTag = "Keyboard";
struct KeyboardRequest {
    uint32_t token;
    uint32_t epoch;
    uint32_t submitted_at;
};
struct KeyboardWorkspace {
    KeySequenceExecutor executor;
    KeyboardRequest request = {};
    char sequence[256] = {};
};
KeyboardWorkspace* workspace = nullptr;
bool workspace_in_psram = false;
KeyboardRequest current = {};
std::atomic<bool> occupied{false};
std::atomic<bool> pending{false};
std::atomic<bool> ready{false};
std::atomic<KeyboardTransport> selected{KeyboardTransport::None};
uint32_t observed_epoch = 0;
bool release_pending = true;
KsUsageType release_type = KS_USAGE_KEYBOARD;

bool backend_ready() {
#if HAS_USB_HID
    if (selected.load() == KeyboardTransport::Usb) return usb_hid_is_ready();
#endif
#if HAS_BLE_HID
    if (selected.load() == KeyboardTransport::Ble) return ble_hid_is_initialized() && ble_hid_is_connected() && ble_hid_is_encrypted();
#endif
    return false;
}

uint32_t backend_epoch() {
#if HAS_USB_HID
    if (selected.load() == KeyboardTransport::Usb) return usb_hid_epoch();
#endif
#if HAS_BLE_HID
    if (selected.load() == KeyboardTransport::Ble) return ble_hid_epoch();
#endif
    return 0;
}

bool send_report(void*, KsUsageType type, uint16_t usage, uint8_t modifiers) {
    if (!backend_ready() || (current.token && current.epoch != backend_epoch())) return false;
    bool sent = false;
#if HAS_USB_HID
    if (selected.load() == KeyboardTransport::Usb) sent = usb_hid_send_report(type, usage, modifiers);
#endif
#if HAS_BLE_HID
    if (selected.load() == KeyboardTransport::Ble) sent = ble_hid_send_report(type, usage, modifiers);
#endif
    return sent;
}

void finish(bool success) {
    if (!success) {
        workspace->executor.cancel(nullptr, nullptr);
        release_pending = true;
        release_type = KS_USAGE_KEYBOARD;
    }
    const uint32_t token = current.token;
    current = {};
    if (!workspace_in_psram) {
        workspace->~KeyboardWorkspace();
        heap_caps_free(workspace);
        workspace = nullptr;
    }
    occupied.store(false);
    action_continuation_complete(token, success);
    if (success) {
        LOGD(kKeyboardTag, "%s macro token=%lu complete", keyboard_transport_name(selected.load()),
             static_cast<unsigned long>(token));
    } else {
        LOGW(kKeyboardTag, "%s macro token=%lu aborted", keyboard_transport_name(selected.load()),
             static_cast<unsigned long>(token));
    }
}
}

void keyboard_hid_init(KeyboardTransport transport) {
    selected.store(keyboard_transport_resolve(transport, HAS_BLE_HID, HAS_USB_HID));
    LOGI(kKeyboardTag, "Transport: %s", keyboard_transport_name(selected.load()));
}

bool keyboard_hid_request_sequence(const char* sequence, uint32_t token) {
    const uint32_t request_epoch = backend_epoch();
    if (!token || !sequence || !sequence[0] || strlen(sequence) >= sizeof(KeyboardWorkspace::sequence) ||
        !ready.load() || ota_activity_is_active() || occupied.exchange(true)) {
        return false;
    }
    if (!workspace) {
        const bool use_psram = psramFound();
        const uint32_t caps = MALLOC_CAP_8BIT | (use_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
        void* allocation = heap_caps_malloc(sizeof(KeyboardWorkspace), caps);
        if (!allocation) {
            occupied.store(false);
            LOGW(kKeyboardTag, "Macro workspace allocation failed");
            return false;
        }
        workspace = new (allocation) KeyboardWorkspace{};
        workspace_in_psram = use_psram;
    }
    strcpy(workspace->sequence, sequence);
    workspace->request.token = token;
    workspace->request.epoch = request_epoch;
    workspace->request.submitted_at = millis();
    pending.store(true);
    return true;
}

void keyboard_hid_loop() {
    const uint32_t now = millis();
    const uint32_t live_epoch = backend_epoch();
    const bool connected = backend_ready();
    if (live_epoch != observed_epoch || !connected) {
        ready.store(false);
        release_pending = true;
        release_type = KS_USAGE_KEYBOARD;
        observed_epoch = live_epoch;
    }
    if (!current.token && pending.exchange(false)) {
        current = workspace->request;
        if (current.epoch != live_epoch || !connected || ota_activity_is_active() ||
            !workspace->executor.start(workspace->sequence, now)) finish(false);
    }
    if (current.token && (!connected || current.epoch != live_epoch || ota_activity_is_active() ||
                          now - current.submitted_at >= ACTION_CONTINUATION_TIMEOUT_MS)) finish(false);
    if (connected && release_pending) {
        if (send_report(nullptr, release_type, 0, 0)) {
            if (release_type == KS_USAGE_KEYBOARD) {
                release_type = KS_USAGE_CONSUMER;
            } else {
                release_pending = false;
            }
        }
        ready.store(!release_pending && live_epoch == backend_epoch());
        return;
    }
    if (current.token && !release_pending) {
        const auto result = workspace->executor.poll(millis(), send_report, nullptr);
        if (result == KeySequenceExecutor::Result::Success || result == KeySequenceExecutor::Result::Failed) {
            finish(result == KeySequenceExecutor::Result::Success && current.epoch == backend_epoch());
        }
    }
    ready.store(connected && !release_pending && live_epoch == backend_epoch());
}

bool keyboard_hid_is_busy() { return occupied.load(); }
bool keyboard_hid_is_ready() { return ready.load(); }
KeyboardTransport keyboard_hid_transport() { return selected.load(); }
const char* keyboard_hid_status() {
    if (selected.load() == KeyboardTransport::None) return "disabled";
    if (occupied.load()) return "busy";
    return ready.load() ? "ready" : "disconnected";
}

#else

void keyboard_hid_init(KeyboardTransport) {}
bool keyboard_hid_request_sequence(const char*, uint32_t) { return false; }
void keyboard_hid_loop() {}
bool keyboard_hid_is_busy() { return false; }
bool keyboard_hid_is_ready() { return false; }
KeyboardTransport keyboard_hid_transport() { return KeyboardTransport::None; }
const char* keyboard_hid_status() { return "unavailable"; }

#endif