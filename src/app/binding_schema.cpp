#include "binding_schema.h"

#include "binding_template.h"

#include <ArduinoJson.h>

// Doc strings are static constants, so link them instead of copying.
static void set_text(JsonObject object, const char* name, const char* value) {
    if (value && value[0]) object[name] = JsonString(value, JsonString::Linked);
}

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
        }
    }
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
        if (include_docs) emit_doc(scheme, *spec);
    }
}