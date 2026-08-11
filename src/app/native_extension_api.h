#pragma once

#include <stddef.h>
#include <stdint.h>

// The extension ABI is intentionally C-shaped. Native packages are built
// separately from the firmware, so this remains the compatibility boundary.
#define NATIVE_EXTENSION_ABI_VERSION 3u

enum NativeExtensionEventResult : uint8_t {
    NATIVE_EXTENSION_PASS_THROUGH = 0,
    NATIVE_EXTENSION_HANDLED = 1,
};

struct NativeExtensionHostApi {
    uint32_t abi_version;
    void (*set_text)(char* out, size_t out_len, const char* text);
    void* (*label_create)(void* parent);
    void (*label_set_text)(void* label, const char* text);
    void (*obj_center)(void* obj);
    void (*log_info)(const char* message);
    void (*notify)(const char* message);
};

typedef void (*NativeExtensionCreateFn)(const NativeExtensionHostApi* host,
                                        uint32_t instance_id, void* root,
                                        const char* config_json);
typedef void (*NativeExtensionDestroyFn)(const NativeExtensionHostApi* host,
                                         uint32_t instance_id);
typedef NativeExtensionEventResult (*NativeExtensionEventFn)(const NativeExtensionHostApi* host,
                                                             uint32_t instance_id);
typedef void (*NativeExtensionTickFn)(const NativeExtensionHostApi* host,
                                      uint32_t instance_id);