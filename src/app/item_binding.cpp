#include "item_binding.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "binding_template.h"
#include "item_binding_scheme.h"
#include "binding_value_json.h"
#include "execution_context.h"
#include "log_manager.h"
#include "pad_binding.h"
#include <string.h>

namespace {
struct ItemValidation {
    const ListSourceDescriptor* source;
    const PadBinding* bindings;
    uint8_t binding_count;
    const char* error;
};

void validate_token(const char* scheme, size_t scheme_len, const char* params, void* context);

void validate_template(const char* templ, ItemValidation& validation) {
    if (!binding_template_visit_tokens_checked(templ, validate_token, &validation) && !validation.error)
        validation.error = "binding exceeds token scan limits";
}

void validate_token(const char* scheme, size_t scheme_len, const char* params, void* context) {
    auto& validation = *static_cast<ItemValidation*>(context);
    if (validation.error) return;
    if (scheme_len == 4 && !strncmp(scheme, "item", 4)) {
        if (!validation.source) validation.error = "item binding is unavailable in this context";
    } else if (scheme_len == 4 && !strncmp(scheme, "list", 4)) {
        validation.error = "legacy list binding is not supported";
    } else if (scheme_len == 3 && !strncmp(scheme, "pad", 3)) {
        const size_t name_len = strcspn(params, ";");
        const char* alias = pad_binding_find_template(validation.bindings, validation.binding_count,
                                                      params, name_len);
        if (!alias) return;
        if (strstr(alias, "[pad:")) {
            validation.error = "recursive pad binding";
            return;
        }
        validate_template(alias, validation);
    }
}

}

BindingResolverStatus item_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !out || !out_len || !item_binding_field_valid(params, strlen(params)))
        return BINDING_RESOLVER_UNKNOWN;
    if (!thread_execution_context.item_scope) return BINDING_RESOLVER_UNAVAILABLE;
    JsonObjectConst item = thread_execution_context.item_scope->item();
    if (item.isNull() || item[params].isNull()) return BINDING_RESOLVER_UNAVAILABLE;
    return binding_value_scalar_json(item[params], out, out_len)
        ? BINDING_RESOLVER_RESOLVED : BINDING_RESOLVER_UNAVAILABLE;
}

BindingResolvedValueKind item_binding_value_kind(const char* params) {
    if (!params || !item_binding_field_valid(params, strlen(params)) ||
        !thread_execution_context.item_scope) return BINDING_VALUE_KIND_UNKNOWN;
    JsonVariantConst value = thread_execution_context.item_scope->item()[params];
    if (value.is<const char*>()) return BINDING_VALUE_KIND_TEXT;
    if (!value.isNull() && !value.is<bool>() &&
        (value.is<int64_t>() || value.is<uint64_t>() || value.is<double>()))
        return BINDING_VALUE_KIND_NUMBER;
    return BINDING_VALUE_KIND_UNKNOWN;
}

const char* item_binding_validate(const char* templ, const ListSourceDescriptor* source,
                                  const PadBinding* bindings, uint8_t binding_count) {
    if (!templ) return "binding template is required";
    ItemValidation validation = {source, bindings, binding_count, nullptr};
    validate_template(templ, validation);
    return validation.error;
}

const char* item_binding_validate_actions(JsonArrayConst actions, const ListSourceDescriptor* source,
                                          const PadBinding* bindings, uint8_t binding_count) {
    for (JsonObjectConst action : actions) {
        if (action.containsKey("type") || !action["op"].is<const char*>())
            return "legacy actions are not supported";
        for (JsonPairConst argument : action["args"].as<JsonObjectConst>()) {
            JsonObjectConst dynamic = argument.value().as<JsonObjectConst>();
            if (dynamic.isNull()) continue;
            const char* error = item_binding_validate(dynamic["$template"] | "",
                                                      source, bindings, binding_count);
            if (error) return error;
        }
    }
    return nullptr;
}

ItemBindingScope::ItemBindingScope(ListSourceSnapshot* snapshot, size_t index)
    : snapshot_(snapshot), index_(index), previous_(thread_execution_context.item_scope) {
    if (snapshot_) snapshot_->retain();
    thread_execution_context.item_scope = this;
}

ItemBindingScope::~ItemBindingScope() {
    thread_execution_context.item_scope = previous_;
    if (snapshot_) snapshot_->release();
}

JsonObjectConst ItemBindingScope::item() const {
    if (!snapshot_) return JsonObjectConst();
    return snapshot_->items()[index_].as<JsonObjectConst>();
}

void ItemBindingScope::end_current() {
    while (thread_execution_context.item_scope) {
        ItemBindingScope* scope = thread_execution_context.item_scope;
        thread_execution_context.item_scope = scope->previous_;
        scope->previous_ = nullptr;
    }
}

#endif
