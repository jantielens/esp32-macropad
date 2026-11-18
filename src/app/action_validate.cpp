#include "action_validate.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_registry.h"
#include "binding_template.h"
#include "pad_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_action_validation_error[96];

namespace {
class StrictActionJson {
    const char* cursor;
    void whitespace() { while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor; }
    bool string() {
        if (*cursor != '"') return false;
        ++cursor;
        while (*cursor && *cursor != '"') {
            if (static_cast<unsigned char>(*cursor) < 32) return false;
            if (*cursor++ != '\\') continue;
            if (*cursor == 'u') {
                ++cursor;
                for (unsigned digit = 0; digit < 4; ++digit) {
                    if (!isxdigit(static_cast<unsigned char>(*cursor))) return false;
                    ++cursor;
                }
            } else {
                if (!*cursor || !strchr("\"\\/bfnrt", *cursor)) return false;
                ++cursor;
            }
        }
        if (*cursor != '"') return false;
        ++cursor;
        return true;
    }
    bool number() {
        if (*cursor == '-') ++cursor;
        if (*cursor == '0') ++cursor;
        else {
            if (*cursor < '1' || *cursor > '9') return false;
            while (*cursor >= '0' && *cursor <= '9') ++cursor;
        }
        if (*cursor == '.') {
            ++cursor;
            if (*cursor < '0' || *cursor > '9') return false;
            while (*cursor >= '0' && *cursor <= '9') ++cursor;
        }
        if (*cursor == 'e' || *cursor == 'E') {
            ++cursor;
            if (*cursor == '+' || *cursor == '-') ++cursor;
            if (*cursor < '0' || *cursor > '9') return false;
            while (*cursor >= '0' && *cursor <= '9') ++cursor;
        }
        return true;
    }
    bool value(unsigned depth) {
        whitespace();
        if (depth > 10) return false;
        if (*cursor == '"') return string();
        if (*cursor == '{' || *cursor == '[') {
            const bool object = *cursor++ == '{';
            const char closing = object ? '}' : ']';
            whitespace();
            if (*cursor == closing) { ++cursor; return true; }
            do {
                whitespace();
                if (object) {
                    if (!string()) return false;
                    whitespace();
                    if (*cursor != ':') return false;
                    ++cursor;
                }
                if (!value(depth + 1)) return false;
                whitespace();
                if (*cursor == closing) { ++cursor; return true; }
                if (*cursor != ',') return false;
                ++cursor;
            } while (*cursor);
            return false;
        }
        for (const char* literal : {"true", "false", "null"}) {
            const size_t length = strlen(literal);
            if (!strncmp(cursor, literal, length)) { cursor += length; return true; }
        }
        return number();
    }
public:
    explicit StrictActionJson(const char* source) : cursor(source) {}
    bool object() {
        whitespace();
        if (*cursor != '{' || !value(0)) return false;
        whitespace();
        return !*cursor;
    }
};
}

bool action_json_object_valid(const char* value) {
    return value && StrictActionJson(value).object();
}

const char* action_validate_binding_tokens(const char* value) {
    if (!value) return nullptr;
    const char* token = value;
    while ((token = strchr(token, '[')) != nullptr) {
        const char* scheme = token + 1;
        const char* separator = scheme;
        while ((*separator >= 'a' && *separator <= 'z')
            || (*separator >= 'A' && *separator <= 'Z')
            || (*separator >= '0' && *separator <= '9') || *separator == '_') separator++;
        if (*separator != ':' || separator == scheme) { token++; continue; }
        size_t scheme_len = (size_t)(separator - scheme);
        if (!binding_template_scheme_known(scheme, scheme_len)) {
            char scheme_name[16];
            size_t copied = scheme_len < sizeof(scheme_name) - 1 ? scheme_len : sizeof(scheme_name) - 1;
            memcpy(scheme_name, scheme, copied);
            scheme_name[copied] = '\0';
            snprintf(s_action_validation_error, sizeof(s_action_validation_error),
                     "unknown binding scheme '%s'", scheme_name);
            return s_action_validation_error;
        }
        const char* params = separator + 1;
        const char* end = params;
        int bracket_depth = 1;
        while (*end && bracket_depth > 0) {
            if (*end == '[') ++bracket_depth;
            else if (*end == ']') --bracket_depth;
            ++end;
        }
        if (bracket_depth != 0) { token++; continue; }
        const size_t parameter_len = (size_t)((end - 1) - params);
        if (parameter_len >= BINDING_TEMPLATE_MAX_LEN) return "binding parameters too long";
        char parameter_value[BINDING_TEMPLATE_MAX_LEN];
        memcpy(parameter_value, params, parameter_len);
        parameter_value[parameter_len] = '\0';
        const char* error = binding_template_validate_params(scheme, scheme_len, parameter_value);
        if (error) return error;
        token = end;
    }
    return nullptr;
}

const char* action_operation_validate_descriptor(const ActionOperationDef& operation) {
    if (operation.argument_count > ACTION_OPERATION_MAX_ARGUMENTS ||
        (operation.argument_count && !operation.arguments))
        return "invalid operation argument count";
    if (operation.variable_transaction && operation.execution != ACTION_EXECUTION_SYNC)
        return "variable transaction must be synchronous";
    for (uint8_t index = 0; index < operation.argument_count; ++index) {
        const ActionArgumentDef& argument = operation.arguments[index];
        if (!argument.name || !argument.name[0]) return "invalid argument name";
        if (action_argument_is_text(argument.type) &&
            (!argument.max_length || argument.max_length > ACTION_OPERATION_MAX_TEXT_LENGTH))
            return "invalid argument length limit";
        if (argument.type == ACTION_ARGUMENT_INTEGER && argument.minimum > argument.maximum)
            return "invalid argument range";
        if (argument.nonzero_minimum &&
            (argument.type != ACTION_ARGUMENT_INTEGER || argument.minimum != 0 ||
             argument.nonzero_minimum <= 0 || argument.nonzero_minimum > argument.maximum))
            return "invalid nonzero minimum";
        if (argument.type > ACTION_ARGUMENT_STRING) return "unsupported argument type";
        if (argument.type == ACTION_ARGUMENT_ENUM) {
            if (!argument.choices || !argument.choice_count) return "enum choices are required";
            for (uint8_t choice = 0; choice < argument.choice_count; ++choice) {
                const char* value = argument.choices[choice];
                if (!value || !value[0] || strlen(value) > argument.max_length ||
                    (argument.choice_labels && (!argument.choice_labels[choice] || !argument.choice_labels[choice][0])))
                    return "invalid enum choice";
                for (uint8_t previous = 0; previous < choice; ++previous)
                    if (!strcmp(value, argument.choices[previous])) return "duplicate enum choice";
            }
        }
    }
    return nullptr;
}

static const char* validate_argument_literal(const ActionArgumentDef& argument, JsonVariantConst value) {
    if (action_argument_is_text(argument.type)) {
        if (!value.is<const char*>()) return "argument must be a string";
        const JsonString text = value.as<JsonString>();
        if ((!text.size() && !argument.allow_empty) || text.size() > argument.max_length ||
            strlen(text.c_str()) != text.size()) return "invalid string argument";
        const char* output = nullptr;
        return action_argument_normalize_text(argument, text.c_str(), &output, nullptr);
    }
    if (argument.type == ACTION_ARGUMENT_BOOLEAN)
        return value.is<bool>() ? nullptr : "argument must be boolean";
    if (argument.type != ACTION_ARGUMENT_INTEGER) return "unsupported argument type";
    if (!value.is<int32_t>()) return "argument must be a whole number";
    const int32_t number = value.as<int32_t>();
    return number < argument.minimum || number > argument.maximum ||
        (argument.nonzero_minimum && number != 0 && number < argument.nonzero_minimum)
        ? "argument out of range" : nullptr;
}

const char* action_operation_validate_json(JsonObjectConst action, bool validate_bindings) {
    if (!action["op"].is<const char*>()) return "operation is required";
    const ActionOperationDef* operation = action_operation_find(action["op"].as<const char*>());
    if (!operation) return "unknown or unavailable operation";
    if (const char* error = action_operation_validate_descriptor(*operation)) return error;
    if (!action["args"].is<JsonObjectConst>()) return "operation arguments must be an object";
    for (JsonPairConst pair : action) {
        if (strcmp(pair.key().c_str(), "op") && strcmp(pair.key().c_str(), "args"))
            return "unknown operation field";
    }
    JsonObjectConst args = action["args"].as<JsonObjectConst>();
    for (JsonPairConst pair : args) {
        bool known = false;
        for (uint8_t index = 0; index < operation->argument_count; ++index)
            if (!strcmp(pair.key().c_str(), operation->arguments[index].name)) known = true;
        if (!known) return "unknown operation argument";
    }
    for (uint8_t index = 0; index < operation->argument_count; ++index) {
        const ActionArgumentDef& argument = operation->arguments[index];
        if (!args.containsKey(argument.name)) return "required operation argument missing";
        JsonVariantConst value = args[argument.name];
        if (value.is<JsonObjectConst>()) {
            JsonObjectConst dynamic = value.as<JsonObjectConst>();
            if (!argument.dynamic || dynamic.size() != 1 || !dynamic["$template"].is<const char*>())
                return "invalid dynamic argument";
            const JsonString templ = dynamic["$template"].as<JsonString>();
            if (!templ.size() || templ.size() >= BINDING_TEMPLATE_MAX_LEN ||
                strlen(templ.c_str()) != templ.size() ||
                !binding_template_well_formed(templ.c_str())) return "invalid dynamic template";
            const char* error = validate_bindings ? action_validate_binding_tokens(templ.c_str()) : nullptr;
            if (error) return error;
        } else {
            const char* error = validate_argument_literal(argument, value);
            if (error) return error;
        }
    }
    return nullptr;
}

const char* action_argument_normalize_text(const ActionArgumentDef& argument, const char* value,
                                           const char** text, int32_t* integer) {
    if (!value) return "argument is unavailable";
    if (action_argument_is_text(argument.type)) {
        const size_t length = strlen(value);
        if ((!length && !argument.allow_empty) || length > argument.max_length || !text)
            return "invalid string argument";
        if (argument.type == ACTION_ARGUMENT_ENUM) {
            bool found = false;
            for (uint8_t index = 0; index < argument.choice_count; ++index)
                if (argument.choices && argument.choices[index] &&
                    !strcmp(value, argument.choices[index])) found = true;
            if (!found) return "unknown enum value";
        }
        if (argument.validate_text) {
            const char* error = argument.validate_text(value);
            if (error) return error;
        }
        *text = value;
        return nullptr;
    }
    if (argument.type == ACTION_ARGUMENT_BOOLEAN) {
        if (!integer) return "missing boolean output";
        if (!strcmp(value, "true")) *integer = 1;
        else if (!strcmp(value, "false")) *integer = 0;
        else return "argument must be true or false";
        return nullptr;
    }
    if (argument.type != ACTION_ARGUMENT_INTEGER) return "unsupported argument type";
    if (!integer || !value[0]) return "argument must be a whole number";
    const char* cursor = value;
    if (*cursor == '-' || *cursor == '+') ++cursor;
    if (!*cursor) return "argument must be a whole number";
    for (; *cursor; ++cursor)
        if (*cursor < '0' || *cursor > '9') return "argument must be a whole number";
    errno = 0;
    char* end = nullptr;
    long long parsed = strtoll(value, &end, 10);
    if (errno == ERANGE || *end || parsed < argument.minimum || parsed > argument.maximum ||
        (argument.nonzero_minimum && parsed != 0 && parsed < argument.nonzero_minimum))
        return "argument out of range";
    *integer = static_cast<int32_t>(parsed);
    return nullptr;
}

static const char* validate_action_contract(JsonObjectConst action) {
    const char* type = action["type"] | "";
    const ActionTypeDef* registered_type = action_type_find(type);
    if (registered_type) {
        if (!action_type_is_supported(type)) return "action is unavailable on this board";
        return action_type_validate(registered_type, action);
    }
    // Isolated host validation tests intentionally omit built-in registration.
    if (strcmp(type, ACTION_TYPE_CYCLE_PAD) == 0) {
        if (action.containsKey("direction")) {
            if (!action["direction"].is<const char*>()) return "cycle_pad direction must be a string";
            const char* direction = action["direction"].as<const char*>();
            if (strcmp(direction, "next") != 0 && strcmp(direction, "previous") != 0) {
                return "cycle_pad direction must be 'next' or 'previous'";
            }
        }
        if (action.containsKey("wrap") && !action["wrap"].is<bool>()) return "cycle_pad wrap must be boolean";
        return action.containsKey("excluded_pads") && !action["excluded_pads"].is<const char*>()
            ? "cycle_pad excluded_pads must be a string" : nullptr;
    }
    if (strcmp(type, ACTION_TYPE_HA_SERVICE) != 0) return nullptr;

    if (!action.containsKey("entity_id")) return "ha_service missing entity_id";
    if (!action["entity_id"].is<const char*>()) return "ha_service entity_id must be a string";
    const char* entity_id = action["entity_id"].as<const char*>();
    if (!entity_id[0]) return "ha_service entity_id must not be empty";
    if (strlen(entity_id) >= sizeof(((HaServicePayload*)nullptr)->entity_id)) return "ha_service entity_id too long";
    const char* separator = strchr(entity_id, '.');
    if (!separator || separator == entity_id || !separator[1] || strchr(separator + 1, '.')) {
        return "ha_service entity_id must have nonempty domain and object portions";
    }
    for (const char* cursor = entity_id; *cursor; ++cursor) {
        if (isspace((unsigned char)*cursor)) return "ha_service entity_id must not contain whitespace";
    }
    if (!action.containsKey("service")) return "ha_service missing service";
    if (!action["service"].is<const char*>()) return "ha_service service must be a string";
    const char* service = action["service"].as<const char*>();
    if (!service[0]) return "ha_service service must not be empty";
    const char* service_separator = strrchr(service, '.');
    if (service_separator) {
        snprintf(s_action_validation_error, sizeof(s_action_validation_error),
                 "service must be bare; use '%s'", service_separator + 1);
        return s_action_validation_error;
    }
    if (strlen(service) >= sizeof(((HaServicePayload*)nullptr)->service)) return "ha_service service too long";
    for (const char* cursor = service; *cursor; ++cursor) {
        unsigned char character = (unsigned char)*cursor;
        if (!islower(character) && !isdigit(character) && character != '_') {
            return "ha_service service must contain only lowercase letters, digits, and '_'";
        }
    }
    if (!action.containsKey("data_json")) return nullptr;
    if (!action["data_json"].is<const char*>()) return "ha_service data_json must be a string";
    const char* data_json = action["data_json"].as<const char*>();
    if (!data_json[0]) return nullptr;
    if (strlen(data_json) >= sizeof(((HaServicePayload*)nullptr)->data_json)) return "ha_service data_json too long";
    return action_json_object_valid(data_json) ? nullptr : "ha_service data_json must contain a JSON object";
}

static bool action_type_known(const char* type) {
    if (!type || !type[0] || strcmp(type, "none") == 0) return true;
    static const char* const builtins[] = {
        ACTION_TYPE_MQTT, ACTION_TYPE_TIMER, ACTION_TYPE_SOUND_ALERT,
        ACTION_TYPE_NOTIFY, ACTION_TYPE_SYSTEM, ACTION_TYPE_HA_SERVICE,
        ACTION_TYPE_VISUAL_ALERT, ACTION_TYPE_CYCLE_PAD,
    };
    for (const char* builtin : builtins) {
        if (strcmp(type, builtin) == 0) return true;
    }
    return action_type_is_supported(type);
}

struct BindingValidationContext { const char* error; };

static bool validate_binding_field(char* value, size_t, bool, void* context) {
    BindingValidationContext* validation = static_cast<BindingValidationContext*>(context);
    validation->error = action_validate_binding_tokens(value);
    return validation->error == nullptr;
}

const char* action_validate_json(JsonObjectConst action, bool require_known_type,
                                 const char* value_token, const char* event_value) {
    if (!action.containsKey("type") && action.containsKey("op"))
        return action_operation_validate_json(action);
    if (action.containsKey("type") && action.containsKey("args"))
        return "mixed legacy and operation action";
    const char* type = action["type"] | "";
    if (!type[0]) return "action missing type";
    if (action.containsKey("op") && strcmp(type, ACTION_TYPE_VISUAL_ALERT))
        return "mixed legacy and operation action";
    if (require_known_type && !action_type_known(type)) return "unknown action type";

    const ActionTypeDef* registered_type = action_type_find(type);
    ButtonAction parsed = {};
    JsonDocument normalized;
    normalized.set(action);
    for (JsonPairConst pair : action) {
        const char* name = pair.key().c_str();
        if (strstr(name, "{value}") || strstr(name, "{step}")) return "event tokens are not valid in action field names";
        if (!pair.value().is<const char*>()) continue;
        const char* source = pair.value().as<const char*>();
        const bool has_value = strstr(source, "{value}");
        const bool has_step = strstr(source, "{step}");
        if (!has_value && !has_step) continue;
        const char* token = has_value ? "{value}" : "{step}";
        if (!value_token || strcmp(token, value_token) || (has_value && has_step)) {
            snprintf(s_action_validation_error, sizeof(s_action_validation_error),
                "%s: %s is unavailable in this action context", name, token);
            return s_action_validation_error;
        }
        struct Context { const char* name; const char* source; const char* token;
                         const char* event_value; JsonObject output; bool found; };
        Context context = {name, source, token, event_value, normalized.as<JsonObject>(), false};
        auto visit = [](const char* name, char* field, size_t capacity, uint8_t flags, void* opaque) {
            auto* context = static_cast<Context*>(opaque);
            if (strcmp(name, context->name)) return true;
            context->found = flags & ACTION_FIELD_EVENT;
            if (!context->found || strlen(context->source) >= capacity) return false;
            strlcpy(field, context->source, capacity);
            if (context->event_value) {
                if (!action_substitute_token_field(field, capacity, context->token, context->event_value)) return false;
            } else {
                if (!action_substitute_token_field(field, capacity, context->token, "-999999.9999")) return false;
                strlcpy(field, context->source, capacity);
                if (!action_substitute_token_field(field, capacity, context->token, "0")) return false;
            }
            context->output[name] = field;
            return true;
        };
        if (!action_type_visit_fields(registered_type, parsed, visit, &context) || !context.found) {
            snprintf(s_action_validation_error, sizeof(s_action_validation_error),
                "%s: event token is unsupported or expanded value exceeds field capacity", name);
            return s_action_validation_error;
        }
    }
    const char* error = validate_action_contract(normalized.as<JsonObjectConst>());
    if (error || !registered_type) return error;
    strlcpy(parsed.type, type, sizeof(parsed.type));
    if (registered_type->parse) registered_type->parse(normalized.as<JsonObject>(), parsed);
    BindingValidationContext validation = {};
    if (!action_type_visit_bindable_fields(registered_type, parsed, validate_binding_field, &validation)) {
        return validation.error ? validation.error : "invalid action binding";
    }
    return nullptr;
}

#endif // HAS_DISPLAY || HAS_BUTTON