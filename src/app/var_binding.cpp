#include "board_config.h"
#include "var_binding.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "binding_template.h"
#include "runtime_variables.h"

#include <string.h>

namespace {
bool valid_var_name(const char* value, size_t length) {
    if (!value || length == 0 || length >= RUNTIME_VARIABLE_NAME_CAPACITY) return false;
    char name[RUNTIME_VARIABLE_NAME_CAPACITY];
    memcpy(name, value, length);
    name[length] = '\0';
    return runtime_variable_name_is_valid(name);
}

BindingResolverStatus resolve_var(const char* params, char* out, size_t out_len) {
    if (!params || !out || out_len == 0) return BINDING_RESOLVER_UNKNOWN;
    const size_t name_length = strcspn(params, ";|");
    if (params[name_length] || !valid_var_name(params, name_length))
        return BINDING_RESOLVER_UNKNOWN;
    const RuntimeVariableLookupResult result = runtime_variable_get(params, out, out_len);
    if (result == RUNTIME_VARIABLE_FOUND) return BINDING_RESOLVER_RESOLVED;
    if (result == RUNTIME_VARIABLE_MISSING) return BINDING_RESOLVER_UNAVAILABLE;
    return BINDING_RESOLVER_UNKNOWN;
}

static const BindingParamDoc kVarParams[] = {
    {"name", "Literal, case-sensitive name using 1-32 ASCII letters, digits, _, -, or .",
     nullptr, "^[A-Za-z0-9_.-]{1,32}$"},
};
static const BindingKeyDoc kVarKeys[] = {
    {"*", "A device-global runtime variable name.", nullptr},
};
static const BindingExampleDoc kVarExamples[] = {
    {"[var:counter|0]", "Show zero until the counter is assigned."},
    {"[var:status]", "Show the stored text without evaluating it again."},
};
static const BindingSchemeDoc kVarDoc = {
    "Logic", "Read a volatile, device-global string variable.",
    BINDING_DOC_LIST(kVarParams), BINDING_DOC_LIST(kVarKeys), BINDING_DOC_LIST(kVarExamples),
    nullptr, false, BINDING_DOC_NONE,
    "Values survive pad changes and configuration reloads, but are cleared on reboot. "
    "Empty values are valid; an unset name is unavailable and uses the token fallback or ---.",
    nullptr,
};
static const BindingAuthoringKey kVarAuthoringKeys[] = {
    {"*", BINDING_AUTHORING_KIND_TEXT, "", BINDING_AUTHORING_FORMAT_RAW, nullptr,
     BINDING_AUTHORING_CAP_DISPLAY, true, nullptr},
};
static const BindingAuthoringSpec kVarAuthoring = {
    kVarAuthoringKeys, static_cast<uint8_t>(sizeof(kVarAuthoringKeys) / sizeof(kVarAuthoringKeys[0]))
};

void var_binding_init_impl() {
    binding_template_register("var", resolve_var, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr,
         &kVarDoc, &kVarAuthoring, valid_var_name});
}
}

void var_binding_init() { var_binding_init_impl(); }

#else

void var_binding_init() {}

#endif