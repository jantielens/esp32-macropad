#include "event_binding.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "log_manager.h"
#include <string.h>

namespace {
thread_local const ActionEventData* current_event = nullptr;

const char* event_value(const char* params) {
    if (!current_event) return nullptr;
    if (!strcmp(params, "value")) return current_event->value;
    if (!strcmp(params, "step")) return current_event->step;
    return nullptr;
}

BindingResolverStatus resolve_event(const char* params, char* out, size_t out_len) {
    if (!params || !out || !out_len || (strcmp(params, "value") && strcmp(params, "step")))
        return BINDING_RESOLVER_UNKNOWN;
    const char* value = event_value(params);
    if (!value) return BINDING_RESOLVER_UNAVAILABLE;
    const size_t length = strlen(value);
    if (length >= out_len) return BINDING_RESOLVER_UNAVAILABLE;
    memcpy(out, value, length + 1);
    return BINDING_RESOLVER_RESOLVED;
}

BindingResolvedValueKind event_kind(const char* params) {
    if (!params || !event_value(params)) return BINDING_VALUE_KIND_UNKNOWN;
    if (!strcmp(params, "step")) return BINDING_VALUE_KIND_NUMBER;
    return current_event->value_kind;
}

uint8_t key_count() { return 2; }
const char* key_at(uint8_t index) {
    static const char* const keys[] = {"value", "step"};
    return index < 2 ? keys[index] : nullptr;
}

const BindingParamDoc kParams[] = {{"key", "Event value or Numeric Rocker step."}};
const BindingKeyDoc kKeys[] = {
    {"value", "Value supplied by the activating control.", nullptr},
    {"step", "Signed Numeric Rocker adjustment.", nullptr},
};
const BindingExampleDoc kExamples[] = {
    {"[event:value]", "Use the activating control's value."},
    {"[event:step]", "Use the Numeric Rocker's signed step."},
};
const BindingSchemeDoc kEventDoc = {
    "Logic", "Read a value captured when an action is activated.",
    BINDING_DOC_LIST(kParams), BINDING_DOC_LIST(kKeys), BINDING_DOC_LIST(kExamples),
    nullptr, false, BINDING_DOC_NONE,
    "Available only in an action triggered by a control that supplies the value.", nullptr,
};
const BindingAuthoringKey kAuthoringKeys[] = {
    binding_authoring_text("value", "value"),
    binding_authoring_number("step", "", "1", false),
};
const BindingAuthoringSpec kAuthoring = {BINDING_DOC_LIST(kAuthoringKeys)};
}

EventBindingScope::EventBindingScope(const ActionEventData* event) : previous_(current_event) {
    current_event = event;
}

EventBindingScope::~EventBindingScope() { current_event = previous_; }

const ActionEventData* event_binding_current() { return current_event; }

void event_binding_init() {
    if (!binding_template_register("event", resolve_event, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false, key_count, key_at,
         &kEventDoc, &kAuthoring, nullptr, nullptr, event_kind}))
        LOGE("EventBind", "Failed to register event binding scheme");
}

#endif
