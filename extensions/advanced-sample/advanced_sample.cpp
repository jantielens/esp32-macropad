#include "native_extension_api.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "advanced-sample", "1.0.0", "Advanced Sample",
};

// Compose a label from static text plus the per-button configuration string.
// The configuration is owned by the firmware and is valid for this call only.
static void set_advanced_label(const NativeExtensionHostApi* host, void* root,
                               const char* config_json) {
    void* panel = host->ui->obj_create(root);
    if (!panel) return;
    host->ui->obj_set_size(panel, 140, 92);
    host->ui->obj_center(panel);
    host->ui->obj_set_bg_color(panel, 0x173A4D, 255);
    host->ui->obj_set_border(panel, 0x42A5C5, 1, 6);
    host->ui->obj_set_padding(panel, 6);

    void* label = host->ui->label_create(panel);
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
    host->ui->label_set_text(label, message);
    host->ui->obj_set_text_color(label, 0xE0F7FA);
    host->ui->obj_center(label);
}

extern "C" void native_extension_create_instance(const NativeExtensionHostApi* host,
                                                  void* extension_context, uint32_t instance_id, void* root,
                                                  const char* config_json) {
    (void)extension_context;
    if (!host || !host->core || !host->ui || !root || !host->ui->obj_create ||
        !host->ui->obj_set_size || !host->ui->label_create ||
        !host->ui->label_set_text || !host->ui->obj_center) return;
    // Each placement gets a unique instance ID. A real extension can use it to
    // distinguish multiple buttons using the same package.
    host->core->log(NATIVE_EXTENSION_LOG_INFO, "advanced sample created");
    (void)instance_id;
    set_advanced_label(host, root, config_json);
}

extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host,
                                                   void* extension_context, uint32_t instance_id) {
    (void)extension_context;
    (void)instance_id;
    if (host && host->core) host->core->log(NATIVE_EXTENSION_LOG_INFO, "advanced sample destroyed");
}

extern "C" NativeExtensionEventResult native_extension_on_tap(const NativeExtensionHostApi* host,
                                                                void* extension_context, uint32_t instance_id) {
    (void)extension_context;
    (void)instance_id;
    if (host && host->core) host->core->log(NATIVE_EXTENSION_LOG_INFO, "advanced sample handled tap");
    if (host && host->core) host->core->notify("Advanced sample tap handled");
    // HANDLED suppresses the button's ordinary tap actions.
    return NATIVE_EXTENSION_HANDLED;
}

extern "C" NativeExtensionEventResult native_extension_on_long_press(const NativeExtensionHostApi* host,
                                                                       void* extension_context, uint32_t instance_id) {
    (void)extension_context;
    (void)instance_id;
    if (host && host->core) host->core->log(NATIVE_EXTENSION_LOG_INFO, "advanced sample passed through long press");
    // PASS_THROUGH leaves ordinary button long-press actions active.
    return NATIVE_EXTENSION_PASS_THROUGH;
}