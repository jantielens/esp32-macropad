#include "native_extension_api.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "hello-world", "1.0.0", "Hello World",
};

// The smallest usable Extension: create one LVGL label in the root supplied by
// the firmware. The root is already sized and clipped to the host button.
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

// The host calls this before destroying the button UI. This sample owns no
// dynamic resources, so cleanup is intentionally empty.
extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host,
                                                   void* extension_context, uint32_t instance_id) {
    (void)host;
    (void)extension_context;
    (void)instance_id;
}

extern "C" void native_extension_shutdown(const NativeExtensionHostApi* host,
                                           void* extension_context) {
    (void)host;
    (void)extension_context;
}