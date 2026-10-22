#include "binding_schema.h"

#include "binding_template.h"

#include <ArduinoJson.h>
#include <string.h>

// Doc strings are static constants, so link them instead of copying.
static void set_text(JsonObject object, const char* name, const char* value) {
    if (value && value[0]) object[name] = JsonString(value, JsonString::Linked);
}

static const char* authoring_kind_name(BindingAuthoringValueKind kind);

static void emit_doc(JsonObject scheme, const BindingSchemeSpec& spec) {
    const BindingSchemeDoc* doc = spec.doc;
    if (!doc) return;
    set_text(scheme, "category", doc->category);
    set_text(scheme, "summary", doc->summary);
    set_text(scheme, "note", doc->note);
    if (doc->status) set_text(scheme, "status", doc->status());

    JsonArray params = scheme["params"].to<JsonArray>();
    for (uint8_t i = 0; i < doc->param_count; ++i) {
        JsonObject param = params.add<JsonObject>();
        set_text(param, "name", doc->params[i].name);
        set_text(param, "desc", doc->params[i].desc);
        param["required"] = i < spec.min_params;
        set_text(param, "default", doc->params[i].default_value);
    }
    if (doc->key_doc_count) {
        JsonArray keys = scheme["key_docs"].to<JsonArray>();
        for (uint8_t i = 0; i < doc->key_doc_count; ++i) {
            JsonObject key = keys.add<JsonObject>();
            set_text(key, "key", doc->keys[i].key);
            set_text(key, "desc", doc->keys[i].desc);
            set_text(key, "group", doc->keys[i].group);
        }
    }
    JsonArray examples = scheme["examples"].to<JsonArray>();
    for (uint8_t i = 0; i < doc->example_count; ++i) {
        JsonObject example = examples.add<JsonObject>();
        set_text(example, "code", doc->examples[i].code);
        set_text(example, "desc", doc->examples[i].desc);
    }
    if (doc->reference_count) {
        JsonObject reference = scheme["reference"].to<JsonObject>();
        set_text(reference, "title", doc->reference_title);
        reference["formats"] = doc->reference_is_formats;
        JsonArray rows = reference["rows"].to<JsonArray>();
        for (uint8_t i = 0; i < doc->reference_count; ++i) {
            const BindingReferenceDoc& entry = doc->reference[i];
            JsonObject row = rows.add<JsonObject>();
            set_text(row, "group", entry.group);
            set_text(row, "syntax", entry.syntax);
            set_text(row, "sample", entry.sample);
            set_text(row, "desc", entry.desc);
            set_text(row, "example", entry.example);
            if (entry.sample) {
                if (entry.unit) row["unit"] = JsonString(entry.unit, JsonString::Linked);
                set_text(row, "kind", authoring_kind_name(entry.kind));
            }
        }
    }
}

static const char* authoring_kind_name(BindingAuthoringValueKind kind) {
    switch (kind) {
        case BINDING_AUTHORING_KIND_INTEGER: return "integer";
        case BINDING_AUTHORING_KIND_TEXT: return "text";
        case BINDING_AUTHORING_KIND_NUMBER: return "number";
        default: return nullptr;
    }
}

static void emit_authoring(JsonObject scheme, const BindingSchemeSpec& spec) {
    const BindingAuthoringSpec* authoring = spec.authoring;
    if (!authoring || !authoring->keys || !authoring->key_count ||
        (!spec.free_form && (!spec.key_count || !spec.key_at))) return;

    JsonObject authoring_json = scheme["authoring"].to<JsonObject>();
    JsonArray keys = authoring_json["keys"].to<JsonArray>();
    const uint8_t count = spec.free_form ? authoring->key_count : spec.key_count();
    for (uint8_t i = 0; i < count; ++i) {
        const char* registered = spec.free_form ? authoring->keys[i].key : spec.key_at(i);
        if (!registered) continue;
        const BindingAuthoringKey* match = nullptr;
        for (uint8_t j = 0; j < authoring->key_count; ++j) {
            const auto& candidate = authoring->keys[j];
            if (!candidate.key) continue;
            if (strcmp(candidate.key, registered) == 0) { match = &candidate; break; }
            if (!match && binding_key_doc_matches(candidate.key, registered)) match = &candidate;
        }
        if (!match) continue;
        const BindingAuthoringKey& metadata = *match;
        const char* kind = authoring_kind_name(metadata.kind);
        if (!kind) continue;

        JsonObject key = keys.add<JsonObject>();
        key["key"] = JsonString(registered, JsonString::Copied);
        set_text(key, "kind", kind);
        if (metadata.unit) key["unit"] = JsonString(metadata.unit, JsonString::Linked);
        set_text(key, "example_value", metadata.example_value);

        JsonArray formats = key["formats"].to<JsonArray>();
        if (metadata.formats & BINDING_AUTHORING_FORMAT_RAW) formats.add("raw");
        if (metadata.formats & BINDING_AUTHORING_FORMAT_NUMBER) formats.add("number");
        if ((metadata.formats & BINDING_AUTHORING_FORMAT_INTEGER) && metadata.integer_format) {
            formats.add("integer");
            key["integer_format"] = metadata.integer_format;
        }

        JsonArray capabilities = key["capabilities"].to<JsonArray>();
        if (metadata.capabilities & BINDING_AUTHORING_CAP_DISPLAY) capabilities.add("display");
        if (metadata.capabilities & BINDING_AUTHORING_CAP_NUMERIC_WIDGET) {
            capabilities.add("numeric_widget");
        }
        if (metadata.capabilities & BINDING_AUTHORING_CAP_LIVE_SAMPLING) {
            capabilities.add("live_sampling");
        }
        key["read_only"] = metadata.read_only;
    }
    if (keys.size() == 0) scheme.remove("authoring");
}

void binding_schema_emit(void* out_json, bool include_docs) {
    JsonArray& out = *static_cast<JsonArray*>(out_json);
    for (uint8_t index = 0; index < binding_template_scheme_count(); ++index) {
        const char* name = binding_template_scheme_name(index);
        const BindingSchemeSpec* spec = binding_template_scheme_spec(index);
        if (!name || !name[0] || !spec) continue;

        JsonObject scheme = out.add<JsonObject>();
        scheme["name"] = name;
        scheme["min_params"] = spec->min_params;
        scheme["max_params"] = spec->max_params;
        scheme["widget_max_params"] = spec->widget_max_params;
        scheme["format_param"] = spec->format_param;
        scheme["validation_mode"] = spec->validation_mode;
        scheme["free_form"] = spec->free_form;
        if (!spec->free_form) {
            JsonArray keys = scheme["keys"].to<JsonArray>();
            for (uint8_t key_index = 0; key_index < spec->key_count(); ++key_index) {
                const char* key = spec->key_at(key_index);
                if (key) keys.add(key);
            }
        }
        if (include_docs) {
            emit_doc(scheme, *spec);
            emit_authoring(scheme, *spec);
        }
    }
}