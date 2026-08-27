#include "binding_schema.h"

#include "binding_template.h"

#include <ArduinoJson.h>

void binding_schema_emit(void* out_json) {
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
    }
}