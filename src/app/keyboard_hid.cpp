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
    uint32_t timeout_ms;
    uint32_t cancellation_epoch;
};
struct KeyboardWorkspace {
    KeySequenceExecutor executor;
    KeyboardRequest request = {};
};
KeyboardWorkspace* workspace = nullptr;
bool workspace_in_psram = false;
KeyboardRequest current = {};
std::atomic<bool> occupied{false};
std::atomic<bool> pending{false};
std::atomic<bool> ready{false};
std::atomic<KeyboardTransport> selected{KeyboardTransport::None};
std::atomic<KeyboardLayout> selected_layout{KeyboardLayout::Us};
enum class OutputStatus { Idle, Running, Completed, Cancelled, Disconnected, Ota, Timeout, ReportFailed };
std::atomic<OutputStatus> output_status{OutputStatus::Idle};
std::atomic<uint32_t> completed_bytes{0};
std::atomic<uint32_t> total_bytes{0};
std::atomic<uint32_t> cancellation_epoch{0};
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

void finish(OutputStatus result) {
    const bool success = result == OutputStatus::Completed;
    completed_bytes.store(workspace->executor.completed_bytes());
    output_status.store(result);
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
    } else {
        workspace->executor.clear();
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

void keyboard_hid_init(KeyboardTransport transport, KeyboardLayout layout) {
    selected.store(keyboard_transport_resolve(transport, HAS_BLE_HID, HAS_USB_HID));
    selected_layout.store(layout);
    LOGI(kKeyboardTag, "Transport: %s", keyboard_transport_name(selected.load()));
}

static bool request_output(const char* sequence, uint32_t token, bool literal, bool allow_controls) {
    const uint32_t request_epoch = backend_epoch();
    const uint32_t request_cancellation_epoch = cancellation_epoch.load();
    if (!token || !sequence || !sequence[0] || strlen(sequence) > (literal ? KEYBOARD_TEXT_MAX_BYTES : 255) ||
        !ready.load() || ota_activity_is_active() || occupied.exchange(true)) {
        LOGW(kKeyboardTag, "Output rejected: invalid size/token, busy, disconnected, or OTA");
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
    const uint32_t submitted_at = millis();
    const bool valid = literal ? workspace->executor.start_text(sequence, submitted_at, selected_layout.load(), allow_controls)
                               : workspace->executor.start(sequence, submitted_at, selected_layout.load());
    if (!valid || (literal && !action_continuation_set_timeout(token, workspace->executor.timeout_ms()))) {
        LOGW(kKeyboardTag, "Output rejected: %s", valid ? "continuation unavailable" : workspace->executor.error());
        workspace->executor.cancel(nullptr, nullptr);
        if (!workspace_in_psram) {
            workspace->~KeyboardWorkspace();
            heap_caps_free(workspace);
            workspace = nullptr;
        } else {
            workspace->executor.clear();
        }
        occupied.store(false);
        return false;
    }
    workspace->request.token = token;
    workspace->request.epoch = request_epoch;
    workspace->request.submitted_at = submitted_at;
    workspace->request.timeout_ms = workspace->executor.timeout_ms();
    workspace->request.cancellation_epoch = request_cancellation_epoch;
    completed_bytes.store(0);
    total_bytes.store(workspace->executor.total_bytes());
    output_status.store(OutputStatus::Running);
    pending.store(true);
    return true;
}

bool keyboard_hid_request_sequence(const char* sequence, uint32_t token) {
    return request_output(sequence, token, false, false);
}
bool keyboard_hid_request_text(const char* text, uint32_t token, bool allow_controls) {
    return request_output(text, token, true, allow_controls);
}
void keyboard_hid_cancel() {
    if (occupied.load()) cancellation_epoch.fetch_add(1);
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
    }
    if (current.token) {
        if (current.cancellation_epoch != cancellation_epoch.load()) finish(OutputStatus::Cancelled);
        else if (!connected || current.epoch != live_epoch) finish(OutputStatus::Disconnected);
        else if (ota_activity_is_active()) finish(OutputStatus::Ota);
        else if (now - current.submitted_at >= current.timeout_ms) finish(OutputStatus::Timeout);
    }
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
        completed_bytes.store(workspace->executor.completed_bytes());
        if (result == KeySequenceExecutor::Result::Success || result == KeySequenceExecutor::Result::Failed) {
            finish(result == KeySequenceExecutor::Result::Success && current.epoch == backend_epoch()
                       ? OutputStatus::Completed : OutputStatus::ReportFailed);
        }
    }
    ready.store(connected && !release_pending && live_epoch == backend_epoch());
}

bool keyboard_hid_is_busy() { return occupied.load(); }
bool keyboard_hid_is_ready() { return ready.load(); }
KeyboardTransport keyboard_hid_transport() { return selected.load(); }
bool keyboard_hid_transport_is_ready() { return backend_ready(); }
uint32_t keyboard_hid_transport_epoch() { return backend_epoch(); }
KeyboardLayout keyboard_hid_layout() { return selected_layout.load(); }
uint32_t keyboard_hid_completed_bytes() { return completed_bytes.load(); }
uint32_t keyboard_hid_total_bytes() { return total_bytes.load(); }
const char* keyboard_hid_output_status() {
    switch (output_status.load()) {
        case OutputStatus::Idle: return "idle";
        case OutputStatus::Running: return "running";
        case OutputStatus::Completed: return "completed";
        case OutputStatus::Cancelled: return "cancelled";
        case OutputStatus::Disconnected: return "disconnected";
        case OutputStatus::Ota: return "ota";
        case OutputStatus::Timeout: return "timeout";
        case OutputStatus::ReportFailed: return "report_failed";
    }
    return "idle";
}
const char* keyboard_hid_status() {
    if (selected.load() == KeyboardTransport::None) return "disabled";
    if (occupied.load()) return "busy";
    return ready.load() ? "ready" : "disconnected";
}

#else

void keyboard_hid_init(KeyboardTransport, KeyboardLayout) {}
bool keyboard_hid_request_sequence(const char*, uint32_t) { return false; }
bool keyboard_hid_request_text(const char*, uint32_t, bool) { return false; }
void keyboard_hid_cancel() {}
KeyboardLayout keyboard_hid_layout() { return KeyboardLayout::Us; }
uint32_t keyboard_hid_completed_bytes() { return 0; }
uint32_t keyboard_hid_total_bytes() { return 0; }
const char* keyboard_hid_output_status() { return "unavailable"; }
void keyboard_hid_loop() {}
bool keyboard_hid_is_busy() { return false; }
bool keyboard_hid_is_ready() { return false; }
KeyboardTransport keyboard_hid_transport() { return KeyboardTransport::None; }
bool keyboard_hid_transport_is_ready() { return false; }
uint32_t keyboard_hid_transport_epoch() { return 0; }
const char* keyboard_hid_status() { return "unavailable"; }

#endif