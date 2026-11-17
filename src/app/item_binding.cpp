#include "item_binding.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "binding_template.h"
#include "item_binding_scheme.h"
#include "binding_value_json.h"
#include "action_parse.h"
#include "action_registry.h"
#include "execution_context.h"
#include "log_manager.h"
#include "pad_binding.h"
#include <memory>
#include <new>
#include <stdio.h>
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

const char* item_binding_validate(const char* templ, const ListSourceDescriptor* source,
                                  const PadBinding* bindings, uint8_t binding_count) {
    if (!templ) return "binding template is required";
    ItemValidation validation = {source, bindings, binding_count, nullptr};
    validate_template(templ, validation);
    return validation.error;
}

const char* item_binding_validate_actions(JsonArrayConst actions, const ListSourceDescriptor* source,
                                          const PadBinding* bindings, uint8_t binding_count) {
    bool item_available = source != nullptr;
    size_t action_index = 0;
    size_t boundary_index = 0;
    for (JsonObjectConst action : actions) {
        ++action_index;
        BoundedPsramJsonAllocator allocator(LIST_SOURCE_DOCUMENT_BYTES);
        JsonDocument copy(&allocator);
        if (!copy.set(action) || copy.overflowed()) return "action validation allocation failed";
        ButtonAction parsed = {};
        action_parse(copy.as<JsonObject>(), parsed);
        const ActionTypeDef* type = action_type_find(parsed.type);
        const bool ends_context = action_type_ends_item_context(type, parsed);
        for (JsonPairConst pair : action) {
            if (!pair.value().is<const char*>()) continue;
            struct FieldCheck { const char* name; bool bindable; } check{pair.key().c_str(), false};
            const auto visit = [](const char* name, char*, size_t, uint8_t flags, void* context) {
                auto& check = *static_cast<FieldCheck*>(context);
                if (!strcmp(check.name, name)) check.bindable = flags & ACTION_FIELD_BINDING;
                return true;
            };
            if (!action_type_visit_fields(type, parsed, visit, &check)) return "invalid action fields";
            const bool resolves_in_context = !ends_context || (type && type->resolve_bindings_before_dispatch);
            const ListSourceDescriptor* allowed = item_available && check.bindable && resolves_in_context
                ? source : nullptr;
            const char* error = item_binding_validate(pair.value().as<const char*>(), allowed,
                                                       bindings, binding_count);
            if (error) {
                static thread_local std::unique_ptr<char[]> action_error;
                constexpr size_t error_capacity = 384;
                if (!action_error) action_error.reset(new (std::nothrow) char[error_capacity]);
                if (!action_error) return "item action validation failed; out of memory while formatting error details";
                const char* reason = error;
                char boundary_reason[128];
                if (!strcmp(error, "item binding is unavailable in this context")) {
                    if (source && !item_available) {
                        snprintf(boundary_reason, sizeof(boundary_reason),
                                 "item context ended at action %u (pause, confirmation, or navigation); capture the item in a variable first",
                                 static_cast<unsigned>(boundary_index));
                        reason = boundary_reason;
                    } else if (!check.bindable) {
                        reason = "this field does not support item bindings";
                    } else if (source && !resolves_in_context) {
                        reason = "this action ends item context before resolving bindings; capture the item in a variable first";
                    }
                }
                snprintf(action_error.get(), error_capacity, "action %u (%s), field '%s': %s",
                         static_cast<unsigned>(action_index), action["type"] | "",
                         pair.key().c_str(), reason);
                return action_error.get();
            }
        }
        if (ends_context) {
            if (item_available) boundary_index = action_index;
            item_available = false;
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
