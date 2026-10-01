#include "usb_hid.h"

#if HAS_USB_HID

#include "log_manager.h"
#include "keyboard_transport.h"
#include "project_branding.h"

#include <USB.h>
#include <USBHID.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>
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
        hid = &active_hid;
        keyboard.begin();
        consumer.begin();
    }
    initialized.store(USB.begin());
    LOGI(kUsbHidTag, "Initialized=%d HID=%d product='%s'", initialized.load(), enable_hid, USB.productName());
}

#endif