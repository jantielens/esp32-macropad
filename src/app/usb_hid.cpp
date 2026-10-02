#include "usb_hid.h"

#if HAS_USB_HID

#include "log_manager.h"
#include "keyboard_transport.h"
#include "project_branding.h"
#include "mouse_hid_state.h"
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

void usb_event(void*, esp_event_base_t, int32_t event, void*) {
    if (event == ARDUINO_USB_STARTED_EVENT || event == ARDUINO_USB_STOPPED_EVENT ||
        event == ARDUINO_USB_SUSPEND_EVENT || event == ARDUINO_USB_RESUME_EVENT) {
        epoch.fetch_add(1);
    }
}

}

bool usb_hid_is_ready() { return initialized.load() && hid && tud_mounted() && !tud_suspended(); }
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
        hid = &active_hid;
        keyboard.begin();
        consumer.begin();
        mouse.begin();
    }
    initialized.store(USB.begin());
    LOGI(kUsbHidTag, "Initialized=%d HID=%d product='%s'", initialized.load(), enable_hid, USB.productName());
}

#endif