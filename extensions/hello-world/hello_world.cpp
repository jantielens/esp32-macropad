#include "native_extension_api.h"

// The smallest usable Extension: create one LVGL label in the root supplied by
// the firmware. The root is already sized and clipped to the host button.
extern "C" void native_extension_create_instance(const NativeExtensionHostApi* host,
                                                  uint32_t instance_id, void* root,
                                                  const char* config_json) {
    (void)instance_id;
    (void)config_json;
    if (!host || !root || !host->label_create || !host->label_set_text || !host->obj_center) return;

    void* label = host->label_create(root);
    if (!label) return;
    host->label_set_text(label, "hello world");
    host->obj_center(label);
}

// The host calls this before destroying the button UI. This sample owns no
// dynamic resources, so cleanup is intentionally empty.
extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host,
                                                   uint32_t instance_id) {
    (void)host;
    (void)instance_id;
}