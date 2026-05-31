#include "device_class_registry.h"
#include "board_config.h"

#include <stddef.h>

// Detection ladder — the SINGLE source of truth for mapping compile-time
// hardware/product flags onto a DeviceClass value.
//
// Future product variants (IS_DARKROOM_TIMER, IS_SHUTTER_TESTER, IS_COFFEE_SCALE,
// ...) must be checked BEFORE the HAS_* hardware flags, because a product
// variant may run on top of the same hardware as a generic macropad/epaper build.
DeviceClass device_class_detect() {
    // Product-variant flags (IS_*) are checked BEFORE hardware flags so a
    // variant that runs on top of the same hardware as a generic macropad
    // build still resolves to its specialized device class.

#if IS_SHUTTER_TESTER
    return DeviceClass::SHUTTER_TESTER;
#elif IS_COFFEE_SCALE
    return DeviceClass::COFFEE_SCALE;
#elif IS_DARKROOM_TIMER
    return DeviceClass::DARKROOM_TIMER;
#elif HAS_EPAPER
    return DeviceClass::EPAPER;
#elif !HAS_DISPLAY
    return DeviceClass::HEADLESS;
#else
    return DeviceClass::MACROPAD;
#endif
}

// Branding table — the SINGLE source of truth for the user-facing strings
// associated with each device class. Order is not significant for lookup,
// but the tests/test_branding_mirror.sh guard walks this table textually
// and expects one row per device class.
static const DeviceClassDescriptor DESCRIPTORS[] = {
    { DeviceClass::MACROPAD,       "Macropad",       "MACROPAD", "ESP32 Macropad"          },
    { DeviceClass::EPAPER,         "E-Paper",        "EPAPER",   "ESP32-MP E-Paper"        },
    { DeviceClass::HEADLESS,       "Headless",       "HEADLESS", "ESP32-MP Headless"       },
    { DeviceClass::SHUTTER_TESTER, "Shutter Tester", "SHUTTER",  "ESP32-MP Shutter Tester" },
    { DeviceClass::COFFEE_SCALE,   "Coffee Scale",   "SCALE",    "ESP32-MP Coffee Scale"   },
    { DeviceClass::DARKROOM_TIMER, "Darkroom Timer", "DARKROOM", "ESP32-MP Darkroom Timer" },
};

static const size_t DESCRIPTOR_COUNT = sizeof(DESCRIPTORS) / sizeof(DESCRIPTORS[0]);

const DeviceClassDescriptor* device_class_get_descriptor(DeviceClass cls) {
    for (size_t i = 0; i < DESCRIPTOR_COUNT; i++) {
        if (DESCRIPTORS[i].device_class == cls) {
            return &DESCRIPTORS[i];
        }
    }
    // Safe fallback: an unrecognized class falls back to the macropad
    // descriptor so callers never see a nullptr. Drift between the enum
    // and the table is caught at test time by test_branding_mirror.sh.
    return &DESCRIPTORS[0];
}
