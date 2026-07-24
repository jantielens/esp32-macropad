#ifndef DEVICE_CLASS_REGISTRY_H
#define DEVICE_CLASS_REGISTRY_H

// Device class registry — single source of truth for product/variant identity
// and the user-facing branding strings derived from it.
//
// Adding a new device class requires only two edits to this module:
//   1. Add a value to the DeviceClass enum below.
//   2. Add a row to the DESCRIPTORS[] table in device_class_registry.cpp.
//   3. If the class is selected by a product-variant compile flag (IS_*),
//      also add an #elif branch to device_class_detect() in the .cpp file.
//
// Detection precedence (first match wins) lives in device_class_detect():
//   1. IS_* product variant flags (future: IS_DARKROOM_TIMER, IS_SHUTTER_TESTER, ...)
//   2. HAS_EPAPER hardware flag           -> EPAPER
//   3. !HAS_DISPLAY hardware flag         -> HEADLESS
//   4. default                            -> MACROPAD
//
// All descriptor fields point to static string literals — no allocation,
// safe to call from any task.

enum class DeviceClass {
    MACROPAD,
    EPAPER,
    HEADLESS,
    SHUTTER_TESTER,
    COFFEE_SCALE,
    DARKROOM_TIMER,
    EPAPER_BLE_BRIDGE,
};

struct DeviceClassDescriptor {
    DeviceClass device_class;
    const char* display_name;  // short name, e.g. "Macropad", "E-Paper", "Headless"
    const char* slug;          // upper-case slug used in SSIDs, e.g. "MACROPAD"
    const char* full_name;     // branded full name, e.g. "ESP32 Macropad"
};

// Detect device class from compile-time flags. This is the ONLY place the
// #if detection ladder exists for device class selection.
DeviceClass device_class_detect();

// Look up the descriptor for a device class. Never returns nullptr; if the
// class is not in the table (should never happen), the macropad descriptor
// is returned as a safe fallback.
const DeviceClassDescriptor* device_class_get_descriptor(DeviceClass cls);

#endif
