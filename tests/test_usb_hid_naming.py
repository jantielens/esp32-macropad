#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/usb_hid.cpp").read_text()
start = source.index("static bool name_usb_hid_configuration(")
end = source.index("\nvoid usb_event(", start)
harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#define TUSB_DESC_CONFIGURATION 2
#define TUSB_DESC_INTERFACE 4
#define TUSB_CLASS_HID 3
static uint16_t tu_le16toh(uint16_t value) { return value; }
static uint8_t tu_desc_len(const void* descriptor) { return static_cast<const uint8_t*>(descriptor)[0]; }
static uint8_t tu_desc_type(const void* descriptor) { return static_cast<const uint8_t*>(descriptor)[1]; }
struct __attribute__((packed)) tusb_desc_configuration_t {
    uint8_t bLength, bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces, bConfigurationValue, iConfiguration, bmAttributes, bMaxPower;
};
struct __attribute__((packed)) tusb_desc_interface_t {
    uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting,
        bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
};
'''
harness += source[start:end]
harness += r'''
int main() {
    const uint8_t original[] = {
        9, 2, 34, 0, 2, 1, 7, 0x80, 250,
        9, 4, 0, 0, 1, 3, 1, 1, 8,
        7, 5, 0x81, 3, 64, 0, 1,
        9, 4, 1, 0, 0, 2, 2, 1, 9
    };
    uint8_t descriptor[sizeof(original)];
    std::memcpy(descriptor, original, sizeof(original));
    assert(name_usb_hid_configuration(descriptor, 2));
    for (size_t index = 0; index < sizeof(original); ++index) {
        assert(descriptor[index] == ((index == 6 || index == 17) ? 2 : original[index]));
    }
    assert(name_usb_hid_configuration(descriptor, 2));
    assert(!name_usb_hid_configuration(nullptr, 2));
    assert(!name_usb_hid_configuration(descriptor, 0));
    std::memcpy(descriptor, original, sizeof(original));
    descriptor[9] = 0;
    assert(!name_usb_hid_configuration(descriptor, 2));
    descriptor[9] = 40;
    assert(!name_usb_hid_configuration(descriptor, 2));
    descriptor[9] = 8;
    assert(!name_usb_hid_configuration(descriptor, 2));
    std::memcpy(descriptor, original, sizeof(original));
    descriptor[2] = 33;
    assert(!name_usb_hid_configuration(descriptor, 2));
    std::memcpy(descriptor, original, sizeof(original));
    descriptor[2] = 8;
    assert(!name_usb_hid_configuration(descriptor, 2));
}
'''

with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "usb_hid_naming.cpp"
    executable = pathlib.Path(directory) / "usb_hid_naming"
    test_source.write_text(harness)
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(test_source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: USB HID uses the product name while preserving CDC and endpoint descriptors")