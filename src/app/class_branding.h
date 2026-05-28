#ifndef CLASS_BRANDING_H
#define CLASS_BRANDING_H

// Device-class-specific branding helpers.
//
// These are thin convenience wrappers around the device class registry
// (see device_class_registry.{h,cpp}). They resolve the compile-time
// device class into the user-facing branding strings used for SSID
// generation, the web portal title, the default device name, the HTTP
// auth realm, and the /api/info response.
//
// All helpers return pointers to static string literals — no allocation,
// safe to call from any task.

const char* device_class_get_display_name();   // e.g. "Macropad", "E-Paper", "Headless"
const char* device_class_get_slug();           // e.g. "MACROPAD", "EPAPER", "HEADLESS"
const char* device_class_get_full_name();      // e.g. "ESP32 Macropad", "ESP32-MP E-Paper"

#endif
