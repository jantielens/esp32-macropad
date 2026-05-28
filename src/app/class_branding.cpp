#include "class_branding.h"
#include "device_class_registry.h"

// Thin convenience wrappers around the device class registry. The actual
// detection ladder and branding strings live in device_class_registry.{h,cpp};
// these accessors exist so callers can keep using simple `const char*` helpers
// without depending on the registry types directly.

const char* device_class_get_display_name() {
    return device_class_get_descriptor(device_class_detect())->display_name;
}

const char* device_class_get_slug() {
    return device_class_get_descriptor(device_class_detect())->slug;
}

const char* device_class_get_full_name() {
    return device_class_get_descriptor(device_class_detect())->full_name;
}
