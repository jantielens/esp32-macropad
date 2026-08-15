#include "native_extension_api.h"

// Hello World is the smallest native extension: it creates one label under
// the firmware-provided widget root and has no package or worker state.

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "hello-world", "1.0.0", "Hello World",
    NATIVE_EXTENSION_TICK_INTERVAL_DEFAULT_MS, 0,
};

// This runs on the LVGL task. The root is already sized and clipped to the
// host button, and child objects are automatically deleted with that root.
extern "C" void native_extension_create_instance(const NativeExtensionHostApi* host,
                                                  void* extension_context, uint32_t instance_id, void* root,
                                                  const char* config_json) {
    (void)extension_context;
    (void)instance_id;
    (void)config_json;
    if (!host || !host->ui || !root || !host->ui->label_create ||
        !host->ui->label_set_text || !host->ui->obj_center) return;

    void* label = host->ui->label_create(root);
    if (!label) return;
    host->ui->label_set_text(label, "hello world");
    host->ui->obj_center(label);
}

// Per-widget cleanup runs before the host destroys the root. This sample owns
// no per-widget resources, so cleanup is intentionally empty.
extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host,
                                                   void* extension_context, uint32_t instance_id) {
    (void)host;
    (void)extension_context;
    (void)instance_id;
}

extern "C" void native_extension_shutdown(const NativeExtensionHostApi* host,
                                           void* extension_context) {
    // ABI 8+ requires shutdown after every host-managed worker has stopped.
    // This package has no worker or package-owned state to release.
    (void)host;
    (void)extension_context;
}