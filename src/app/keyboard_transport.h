#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum class KeyboardTransport : uint8_t { None, Ble, Usb };

inline void keyboard_transport_device_name(char* name, size_t size, const char* base, KeyboardTransport transport) {
    const char* suffix = transport == KeyboardTransport::Usb ? " USB" : transport == KeyboardTransport::Ble ? " BLE" : "";
    snprintf(name, size, "%s%s", base && base[0] ? base : "Keyboard", suffix);
}

constexpr KeyboardTransport keyboard_transport_default(bool, bool) {
    return KeyboardTransport::None;
}

constexpr KeyboardTransport keyboard_transport_resolve(KeyboardTransport selected, bool has_ble, bool has_usb) {
    if (selected == KeyboardTransport::None ||
        (selected == KeyboardTransport::Ble && has_ble) ||
        (selected == KeyboardTransport::Usb && has_usb)) {
        return selected;
    }
    return keyboard_transport_default(has_ble, has_usb);
}

inline const char* keyboard_transport_name(KeyboardTransport transport) {
    return transport == KeyboardTransport::Usb ? "usb" : transport == KeyboardTransport::Ble ? "ble" : "none";
}

inline bool keyboard_transport_parse(const char* value, KeyboardTransport* transport) {
    if (!value || !transport) return false;
    if (strcmp(value, "none") == 0) *transport = KeyboardTransport::None;
    else if (strcmp(value, "usb") == 0) *transport = KeyboardTransport::Usb;
    else if (strcmp(value, "ble") == 0) *transport = KeyboardTransport::Ble;
    else return false;
    return true;
}