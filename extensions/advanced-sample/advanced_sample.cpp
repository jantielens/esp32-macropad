#include "native_extension_api.h"

// Compose a label from static text plus the per-button configuration string.
// The configuration is owned by the firmware and is valid for this call only.
static void set_advanced_label(const NativeExtensionHostApi* host, void* root,
                               const char* config_json) {
    void* label = host->label_create(root);
    if (!label) return;

    char message[160];
    const char* prefix = "Advanced sample";
    size_t index = 0;
    while (prefix[index] && index + 1 < sizeof(message)) {
        message[index] = prefix[index];
        ++index;
    }
    if (config_json && config_json[0] && index + 2 < sizeof(message)) {
        message[index++] = '\n';
        for (size_t config_index = 0; config_json[config_index] && index + 1 < sizeof(message); ++config_index) {
            message[index++] = config_json[config_index];
        }
    }
    message[index] = '\0';
    host->label_set_text(label, message);
    host->obj_center(label);
}

extern "C" void native_extension_create_instance(const NativeExtensionHostApi* host,
                                                  uint32_t instance_id, void* root,
                                                  const char* config_json) {
    if (!host || !root || !host->label_create || !host->label_set_text || !host->obj_center) return;
    // Each placement gets a unique instance ID. A real extension can use it to
    // distinguish multiple buttons using the same package.
    if (host->log_info) host->log_info("advanced sample created");
    (void)instance_id;
    set_advanced_label(host, root, config_json);
}

extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host,
                                                   uint32_t instance_id) {
    (void)instance_id;
    if (host && host->log_info) host->log_info("advanced sample destroyed");
}

extern "C" NativeExtensionEventResult native_extension_on_tap(const NativeExtensionHostApi* host,
                                                                uint32_t instance_id) {
    (void)instance_id;
    if (host && host->log_info) host->log_info("advanced sample handled tap");
    if (host && host->notify) host->notify("Advanced sample tap handled");
    // HANDLED suppresses the button's ordinary tap actions.
    return NATIVE_EXTENSION_HANDLED;
}

extern "C" NativeExtensionEventResult native_extension_on_long_press(const NativeExtensionHostApi* host,
                                                                       uint32_t instance_id) {
    (void)instance_id;
    if (host && host->log_info) host->log_info("advanced sample passed through long press");
    // PASS_THROUGH leaves ordinary button long-press actions active.
    return NATIVE_EXTENSION_PASS_THROUGH;
}