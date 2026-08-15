#include "native_extension_api.h"

// Advanced Sample demonstrates per-instance UI state, package-owned state,
// live binding resolution, lifecycle cleanup, and handled/pass-through events.

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "advanced-sample", "1.1.0", "Advanced Sample",
    NATIVE_EXTENSION_TICK_INTERVAL_DEFAULT_MS, 0,
};

namespace {

constexpr uint8_t MAX_INSTANCES = 4;

struct InstanceState {
    uint32_t instance_id;
    void* cpu_label;
    char last_value[16];
};

struct SampleState {
    // One package context is shared by all widgets using this extension.
    InstanceState instances[MAX_INSTANCES];
};

void copy_text(char* out, size_t capacity, const char* input) {
    if (!out || capacity == 0) return;
    size_t index = 0;
    while (input && input[index] && index + 1 < capacity) { out[index] = input[index]; ++index; }
    out[index] = '\0';
}

bool text_equals(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

SampleState* get_state(const NativeExtensionHostApi* host, void* extension_context) {
    // Mutable package state cannot live in the flash-mapped ELF. Allocate it
    // through the host and attach it to the loader-owned package context.
    SampleState* state = static_cast<SampleState*>(host->core->context_get_data(extension_context));
    if (state) return state;
    state = static_cast<SampleState*>(host->core->alloc(sizeof(SampleState)));
    if (!state) return nullptr;
    for (auto& instance : state->instances) instance = {};
    host->core->context_set_data(extension_context, state);
    return state;
}

InstanceState* find_instance(SampleState* state, uint32_t instance_id) {
    for (auto& instance : state->instances) {
        if (instance.instance_id == instance_id) return &instance;
    }
    return nullptr;
}

InstanceState* create_instance(SampleState* state, uint32_t instance_id) {
    // Each widget placement gets a stable ID; use it to isolate UI state.
    for (auto& instance : state->instances) {
        if (instance.instance_id == 0) {
            instance = {};
            instance.instance_id = instance_id;
            return &instance;
        }
    }
    return nullptr;
}

void set_advanced_label(const NativeExtensionHostApi* host, InstanceState* instance, void* root) {
    void* panel = host->ui->obj_create(root);
    if (!panel) return;
    host->ui->obj_set_size(panel, 140, 92);
    host->ui->obj_center(panel);
    host->ui->obj_set_bg_color(panel, 0x173A4D, 255);
    host->ui->obj_set_border(panel, 0x42A5C5, 1, 6);
    host->ui->obj_set_padding(panel, 6);

    instance->cpu_label = host->ui->label_create(panel);
    if (!instance->cpu_label) return;
    host->ui->label_set_text(instance->cpu_label, "CPU: ---");
    host->ui->obj_set_text_color(instance->cpu_label, 0xE0F7FA);
    host->ui->obj_center(instance->cpu_label);
}

} // namespace

extern "C" void native_extension_create_instance(const NativeExtensionHostApi* host,
                                                  void* extension_context, uint32_t instance_id, void* root,
                                                  const char* config_json) {
    (void)config_json; // This sample has no package-specific JSON settings.
    if (!host || !host->core || !host->ui || !host->binding || !root || !host->ui->obj_create ||
        !host->ui->obj_set_size || !host->ui->label_create ||
        !host->ui->label_set_text || !host->ui->obj_center) return;
    SampleState* state = get_state(host, extension_context);
    InstanceState* instance = state ? create_instance(state, instance_id) : nullptr;
    if (!instance) return;
    host->core->log(NATIVE_EXTENSION_LOG_INFO, "advanced sample created");
    set_advanced_label(host, instance, root);
}

extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host,
                                                   void* extension_context, uint32_t instance_id) {
    SampleState* state = host ? static_cast<SampleState*>(host->core->context_get_data(extension_context)) : nullptr;
    // LVGL deletes the child panel with the root; discard only our reference.
    if (InstanceState* instance = state ? find_instance(state, instance_id) : nullptr) *instance = {};
    if (host && host->core) host->core->log(NATIVE_EXTENSION_LOG_INFO, "advanced sample destroyed");
}

extern "C" void native_extension_shutdown(const NativeExtensionHostApi* host,
                                           void* extension_context) {
    if (!host || !host->core) return;
    SampleState* state = static_cast<SampleState*>(host->core->context_get_data(extension_context));
    // Shutdown follows worker join, so it is safe to release package state.
    if (state) host->core->free(state);
    host->core->context_set_data(extension_context, nullptr);
    host->core->log(NATIVE_EXTENSION_LOG_INFO, "advanced sample shutdown");
}

extern "C" void native_extension_tick(const NativeExtensionHostApi* host,
                                       void* extension_context, uint32_t instance_id) {
    if (!host || !host->binding) return;
    SampleState* state = static_cast<SampleState*>(host->core->context_get_data(extension_context));
    InstanceState* instance = state ? find_instance(state, instance_id) : nullptr;
    if (!instance || !instance->cpu_label) return;
    // Binding resolution is LVGL-task-only and uses this widget's pad context.
    // Cache the result so an unchanged value does not trigger an LVGL update.
    char cpu[16];
    if (!host->binding->resolve(extension_context, instance_id, "[health:cpu]", cpu, sizeof(cpu))) return;
    if (text_equals(instance->last_value, cpu)) return;
    copy_text(instance->last_value, sizeof(instance->last_value), cpu);
    char label[24] = "CPU: ";
    size_t index = 5;
    for (size_t source = 0; cpu[source] && index + 2 < sizeof(label); ++source) label[index++] = cpu[source];
    label[index++] = '%';
    label[index] = '\0';
    host->ui->label_set_text(instance->cpu_label, label);
    host->ui->obj_center(instance->cpu_label);
}

extern "C" NativeExtensionEventResult native_extension_on_tap(const NativeExtensionHostApi* host,
                                                                void* extension_context, uint32_t instance_id) {
    (void)extension_context;
    (void)instance_id;
    if (host && host->core) host->core->log(NATIVE_EXTENSION_LOG_INFO, "advanced sample handled tap");
    if (host && host->core) host->core->notify("Advanced sample tap handled");
    // HANDLED suppresses the host button's ordinary tap action list.
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