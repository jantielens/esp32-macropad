#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_result.h"
#include "pad_config.h"  // ButtonAction
#include <ArduinoJson.h>

// ============================================================================
// Action Type Registry
// ============================================================================
// Registry for built-in and device-class typed operations and catalog metadata.
typedef void (*ActionTemplateVisitor)(const char* field, void* context);
enum ActionExecutionClass { ACTION_EXECUTION_UNKNOWN, ACTION_EXECUTION_SYNC,
                            ACTION_EXECUTION_PAUSABLE };

constexpr uint8_t ACTION_OPERATION_MAX_ARGUMENTS = 16;
constexpr uint16_t ACTION_OPERATION_MAX_TEXT_LENGTH = 255;

enum ActionArgumentType : uint8_t {
    ACTION_ARGUMENT_SCREEN,
    ACTION_ARGUMENT_INTEGER,
    ACTION_ARGUMENT_BOOLEAN,
    ACTION_ARGUMENT_ENUM,
    ACTION_ARGUMENT_STRING,
    ACTION_ARGUMENT_NUMBER,
};

inline bool action_argument_is_text(ActionArgumentType type) {
    return type == ACTION_ARGUMENT_SCREEN || type == ACTION_ARGUMENT_ENUM ||
           type == ACTION_ARGUMENT_STRING;
}

struct ActionArgumentDef {
    const char* name;
    const char* label;
    ActionArgumentType type;
    int32_t minimum;
    int32_t maximum;
    uint16_t max_length;
    bool dynamic;
    const char* example;
    const char* control;
    const char* const* choices = nullptr;
    uint8_t choice_count = 0;
    bool allow_empty = false;
    const char* (*validate_text)(const char*) = nullptr;
    const char* const* choice_labels = nullptr;
    int32_t nonzero_minimum = 0;
    const char* default_text = nullptr;
    int32_t default_integer = 0;
    bool has_default_integer = false;
    const char* zero_label = nullptr;
};

struct ActionResolvedArgument {
    const char* text;
    int32_t integer;
    float number;
};

struct ActionOperationDef {
    const char* id;
    const char* label;
    const ActionArgumentDef* arguments;
    uint8_t argument_count;
    ActionResult (*dispatch)(const ActionResolvedArgument* arguments, const char* label,
                             uint32_t continuation_token);
    ActionExecutionClass execution = ACTION_EXECUTION_SYNC;
    bool variable_transaction = false;
};

struct ActionTypeDef {
    constexpr ActionTypeDef(
        const char* type_name,
        void (*describe)(JsonObject& out),
        bool (*available)() = nullptr,
        ActionExecutionClass execution = ACTION_EXECUTION_UNKNOWN,
        bool display_lock_required = true,
        bool dispatch_lock_always = false,
        const ActionOperationDef* operations = nullptr,
        uint8_t operation_count = 0)
        : type_name(type_name), describe(describe), available(available),
          execution(execution), display_lock_required(display_lock_required),
          dispatch_lock_always(dispatch_lock_always),
          operations(operations), operation_count(operation_count) {}

    const char* type_name;
    void (*describe)(JsonObject& out);
    bool (*available)();
    ActionExecutionClass execution;
    bool display_lock_required;
    bool dispatch_lock_always;
    const ActionOperationDef* operations;
    uint8_t operation_count;
};

void action_type_register(const ActionTypeDef* type);
// Report registration failures collected before logging was initialized.
void action_registry_logging_ready();
const ActionTypeDef* action_type_find(const char* type_name);

// Registry enumeration (for the MCP capability manifest): device-class action
// types self-register, so the manifest can list them generated, not hand-coded.
uint8_t action_type_count();
const ActionTypeDef* action_type_at(uint8_t index);
const ActionOperationDef* action_operation_find(const char* id, const ActionTypeDef** owner = nullptr);
const ActionOperationDef* action_operation_for_action(const ButtonAction& action,
                                                      const ActionTypeDef** owner = nullptr);
bool action_may_pause(const ButtonAction& action);

// Returns true only when a registered type is available in this build. Use
// this shared decision for authoring validation and catalog discovery.
bool action_type_is_supported(const char* type_name);

void action_visit_templates(const ButtonAction& act, ActionTemplateVisitor visitor, void* context);

// Auto-register an ActionTypeDef instance via a static constructor.
#define REGISTER_ACTION_TYPE(var)                                              \
    static struct var##AutoReg {                                               \
        var##AutoReg() { action_type_register(&var); }                         \
    } _##var##_auto_reg

// Define an immutable action type and register it as one declaration. Use
// this for production action types so the definition cannot be omitted from
// the registry by a separate registration step.
template<size_t capacity>
constexpr bool action_type_name_fits(const char (&)[capacity]) {
    return capacity <= CONFIG_ACTION_TYPE_MAX_LEN;
}

#define DEFINE_AND_REGISTER_ACTION_TYPE(var, type_name, ...)                   \
    static_assert(action_type_name_fits(type_name),                            \
                  "Action identifier exceeds CONFIG_ACTION_TYPE_MAX_LEN; shorten the persisted type name"); \
    const ActionTypeDef var = { type_name, __VA_ARGS__ };                       \
    REGISTER_ACTION_TYPE(var)

#endif // HAS_DISPLAY || HAS_BUTTON
