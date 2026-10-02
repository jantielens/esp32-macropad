#include "usb_hid.h"

#if HAS_USB_HID

#include "log_manager.h"
#include "keyboard_transport.h"
#include "project_branding.h"
#include "mouse_hid_state.h"
#include "gamepad_hid_state.h"
#include "ota_activity.h"

#include <USB.h>
#include <USBHID.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDMouse.h>
#include <atomic>
#include <esp_mac.h>
#include <tusb.h>

#if !SOC_USB_OTG_SUPPORTED || ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT != 0
#error "USB HID requires native TinyUSB and CDCOnBoot=default for firmware-controlled startup."
#endif

namespace {
constexpr const char* kUsbHidTag = "UsbHID";
constexpr uint32_t kReportTimeoutMs = 5;
USBHID* hid = nullptr;
std::atomic<uint32_t> epoch{1};
std::atomic<bool> initialized{false};
std::atomic<bool> gamepad_enabled{false};

const uint8_t gamepad_descriptor[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP), HID_USAGE(HID_USAGE_DESKTOP_GAMEPAD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION), HID_REPORT_ID(HID_REPORT_ID_GAMEPAD)
    HID_USAGE(HID_USAGE_DESKTOP_X), HID_USAGE(HID_USAGE_DESKTOP_Y),
    HID_USAGE(HID_USAGE_DESKTOP_Z), HID_USAGE(HID_USAGE_DESKTOP_RZ),
    HID_LOGICAL_MIN_N(gamepad_protocol::axis_min, 2), HID_LOGICAL_MAX_N(gamepad_protocol::axis_max, 2),
    HID_REPORT_COUNT(gamepad_protocol::axis_count), HID_REPORT_SIZE(sizeof(int16_t) * 8),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_USAGE(HID_USAGE_DESKTOP_RX), HID_USAGE(HID_USAGE_DESKTOP_RY),
    HID_LOGICAL_MIN(0), HID_LOGICAL_MAX_N(gamepad_protocol::trigger_max, 2),
    HID_REPORT_COUNT(gamepad_protocol::trigger_count), HID_REPORT_SIZE(sizeof(uint8_t) * 8),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_USAGE(HID_USAGE_DESKTOP_HAT_SWITCH), HID_LOGICAL_MIN(1), HID_LOGICAL_MAX(gamepad_protocol::hat_position_count),
    HID_PHYSICAL_MIN(0), HID_PHYSICAL_MAX_N(360 - 360 / gamepad_protocol::hat_position_count, 2), HID_UNIT(0x14),
    HID_REPORT_COUNT(1), HID_REPORT_SIZE(8),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_NULL_STATE),
    HID_PHYSICAL_MAX(0), HID_UNIT(0),
    HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON), HID_USAGE_MIN(1), HID_USAGE_MAX(gamepad_protocol::button_count),
    HID_LOGICAL_MIN(0), HID_LOGICAL_MAX(1), HID_REPORT_COUNT(gamepad_protocol::button_count), HID_REPORT_SIZE(1),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), HID_COLLECTION_END
};

class MacropadUsbGamepad : public USBHIDDevice {
public:
    MacropadUsbGamepad() { USBHID::addDevice(this, sizeof(gamepad_descriptor)); }
    uint16_t _onGetDescriptor(uint8_t* destination) override {
        memcpy(destination, gamepad_descriptor, sizeof(gamepad_descriptor));
        return sizeof(gamepad_descriptor);
    }
};

struct __attribute__((packed)) GamepadUsbReport {
    int16_t left_x, left_y, right_x, right_y;
    uint8_t left_trigger, right_trigger, hat;
    uint16_t buttons;
};
static_assert(sizeof(GamepadUsbReport) == gamepad_protocol::report_bytes, "Gamepad descriptor/report mismatch");

static bool name_usb_hid_configuration(uint8_t* descriptor, uint8_t product_index) {
    if (!descriptor || !product_index) return false;
    auto* configuration = reinterpret_cast<tusb_desc_configuration_t*>(descriptor);
    const size_t length = tu_le16toh(configuration->wTotalLength);
    if (configuration->bDescriptorType != TUSB_DESC_CONFIGURATION ||
        configuration->bLength < sizeof(tusb_desc_configuration_t) ||
        configuration->bLength > length) return false;
    size_t offset = configuration->bLength;
    while (offset < length) {
        constexpr size_t descriptor_header_length = 2;
        if (length - offset < descriptor_header_length) return false;
        uint8_t* entry = descriptor + offset;
        const uint8_t entry_length = tu_desc_len(entry);
        if (entry_length < descriptor_header_length || entry_length > length - offset) return false;
        if (tu_desc_type(entry) == TUSB_DESC_INTERFACE) {
            if (entry_length < sizeof(tusb_desc_interface_t)) return false;
            auto* interface = reinterpret_cast<tusb_desc_interface_t*>(entry);
            if (interface->bInterfaceClass == TUSB_CLASS_HID) interface->iInterface = product_index;
        }
        offset += entry_length;
    }
    configuration->iConfiguration = product_index;
    return true;
}

void usb_event(void*, esp_event_base_t, int32_t event, void*) {
    if (event == ARDUINO_USB_STARTED_EVENT || event == ARDUINO_USB_STOPPED_EVENT ||
        event == ARDUINO_USB_SUSPEND_EVENT || event == ARDUINO_USB_RESUME_EVENT) {
        epoch.fetch_add(1);
    }
}

}

bool usb_hid_is_ready() { return initialized.load() && hid && tud_mounted() && !tud_suspended(); }
bool usb_hid_gamepad_enabled() { return gamepad_enabled.load(); }
uint32_t usb_hid_epoch() { return epoch.load(); }

bool usb_hid_send_report(KsUsageType type, uint16_t usage, uint8_t modifiers) {
    if (!usb_hid_is_ready() || !hid->ready()) {
        return false;
    }
    bool sent;
    if (type == KS_USAGE_CONSUMER) {
        sent = hid->SendReport(HID_REPORT_ID_CONSUMER_CONTROL, &usage, sizeof(usage), kReportTimeoutMs);
    } else {
        KeyReport report = {};
        report.modifiers = modifiers;
        report.keys[0] = static_cast<uint8_t>(usage);
        sent = hid->SendReport(HID_REPORT_ID_KEYBOARD, &report, sizeof(report), kReportTimeoutMs);
    }
    return sent;
}

bool usb_hid_send_mouse_report(const MouseHidReport& mouse_report, uint32_t expected_epoch,
                              uint32_t expected_ota_epoch) {
    if (!usb_hid_is_ready() || !hid->ready() || tud_hid_n_get_protocol(0) == HID_PROTOCOL_BOOT) return false;
    hid_mouse_report_t report = {};
    report.buttons = mouse_report.buttons;
    report.x = mouse_report.dx;
    report.y = mouse_report.dy;
    report.wheel = mouse_report.wheel;
    report.pan = mouse_report.pan;
    const bool ota_active = ota_activity_is_active();
    const uint32_t live_ota_epoch = ota_activity_epoch();
    if (!mouse_report.can_submit(expected_epoch, usb_hid_epoch(), expected_ota_epoch,
                                 live_ota_epoch, ota_active)) return false;
    return tud_hid_n_report(0, HID_REPORT_ID_MOUSE, &report, sizeof(report));
}

bool usb_hid_send_gamepad_report(const GamepadReport& state, uint32_t expected_epoch,
                                uint32_t expected_ota_epoch, uint32_t expected_generation) {
    if (!usb_hid_gamepad_enabled() || !usb_hid_is_ready() || !hid->ready() ||
        tud_hid_n_get_protocol(0) == HID_PROTOCOL_BOOT) return false;
    const GamepadUsbReport report = {state.left_x, state.left_y, state.right_x, state.right_y,
        state.left_trigger, state.right_trigger, state.hat, state.buttons};
    if (expected_epoch != usb_hid_epoch() || expected_ota_epoch != ota_activity_epoch() ||
        expected_generation != gamepad_hid_submission_generation()) return false;
    if (ota_activity_is_active() && !state.same_value(GamepadReport{})) return false;
    return tud_hid_n_report(0, HID_REPORT_ID_GAMEPAD, &report, sizeof(report));
}

void usb_hid_init(const char* device_name, bool enable_hid) {
    char product_name[40];
    keyboard_transport_device_name(product_name, sizeof(product_name),
                                   device_name && device_name[0] ? device_name : PROJECT_DISPLAY_NAME,
                                   KeyboardTransport::Usb);
    USB.productName(product_name);
    USB.manufacturerName(PROJECT_DISPLAY_NAME);
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        char serial[13];
        snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        USB.serialNumber(serial);
    }
    USB.onEvent(usb_event);
    if (enable_hid) {
        static USBHID active_hid(HID_ITF_PROTOCOL_KEYBOARD);
        static USBHIDKeyboard keyboard;
        static USBHIDConsumerControl consumer;
        static USBHIDMouse mouse;
        static MacropadUsbGamepad gamepad;
        hid = &active_hid;
        keyboard.begin();
        consumer.begin();
        mouse.begin();
        gamepad_enabled.store(true);
    }
    initialized.store(USB.begin());
    if (initialized.load() && enable_hid) {
        tud_disconnect();
        auto* configuration = const_cast<uint8_t*>(tud_descriptor_configuration_cb(0));
        const auto* device = reinterpret_cast<const tusb_desc_device_t*>(tud_descriptor_device_cb());
        if (!device || !name_usb_hid_configuration(configuration, device->iProduct)) {
            LOGW(kUsbHidTag, "Unable to set HID interface friendly name");
        }
        delay(10);
        tud_connect();
    }
    LOGI(kUsbHidTag, "Initialized=%d HID=%d product='%s'", initialized.load(), enable_hid, USB.productName());
}

#endif