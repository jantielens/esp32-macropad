#include "ext_binding.h"

#if HAS_EXTERNAL_DATA
#include "binding_template.h"
#include "binding_value.h"
#include "external_data.h"
#include "log_manager.h"
#include "pad_config.h"

#include <string.h>

static BindingResolverStatus resolve_external(const char* params, char* out, size_t out_len) {
    char name[EXTERNAL_DATA_NAME_BYTES];
    char path[CONFIG_JSON_PATH_MAX_LEN];
    char format[CONFIG_FORMAT_MAX_LEN];
    if (!binding_value_parse_params(params, name, sizeof(name), path, sizeof(path),
                                    format, sizeof(format)) ||
        !external_data_valid_name(name, strlen(name))) return BINDING_RESOLVER_UNAVAILABLE;
    char raw[BINDING_TEMPLATE_MAX_LEN + 1];
    const bool resolved = strncmp(path, ".@", 2) == 0
        ? external_data_get_stat(name, path + 2, raw, sizeof(raw))
        : external_data_get_value(name, path, raw, sizeof(raw));
    if (!resolved) return BINDING_RESOLVER_UNAVAILABLE;
    char formatted[BINDING_TEMPLATE_MAX_LEN + 2];
    if (format[0]) binding_value_format(raw, format, formatted, sizeof(formatted));
    else strlcpy(formatted, raw, sizeof(formatted));
    if (!out || !out_len || strlen(formatted) >= out_len) return BINDING_RESOLVER_UNAVAILABLE;
    strlcpy(out, formatted, out_len);
    return BINDING_RESOLVER_RESOLVED;
}

static const BindingParamDoc params[] = {
    {"source", "Configured source name, using letters, digits, underscore or hyphen."},
    {"path", "JSON path, or .@ followed by a source statistic; empty or . selects the root."},
    {"format", "Optional safe printf format, such as %.1f C."},
};
static const BindingExampleDoc examples[] = {
    {"[ext:weather;current.temperature_2m;%.1f C|No data]", "Current temperature with a fallback."},
    {"[ext:weather;daily.temperature_2m_max.1;%.0f C|No data]", "Second daily maximum."},
    {"[ext:weather;.@age;%.0f s|Unknown]", "Seconds since the last successful refresh."},
    {"[ext:weather;.@state|Unknown]", "Source state."},
};
static const BindingSchemeDoc kExternalDoc = {
    "Data", "Cached JSON from a configured external HTTP or HTTPS source.",
    BINDING_DOC_LIST(params), BINDING_DOC_NONE, BINDING_DOC_LIST(examples),
    nullptr, false, BINDING_DOC_NONE,
    "Statistics: .@state, .@age, .@next, .@duration, .@size, .@http_status, .@error. "
    "Failed refreshes retain the last value. Keys containing dots or binding delimiters are unsupported.",
    nullptr,
};

void external_binding_init() {
    if (!binding_template_register("ext", resolve_external, nullptr,
                                   {1, 3, 2, 2, BINDING_VALIDATION_STANDARD, true, nullptr,
                                    nullptr, &kExternalDoc, nullptr, external_data_valid_name})) {
        LOGE("ExtData", "Failed to register external data binding");
    }
}
#endif
