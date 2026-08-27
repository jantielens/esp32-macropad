#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#include "binding_schema.h"
#include "binding_template.h"

static int g_failures = 0;

static void expect(bool condition, const char* label) {
    if (!condition) {
        std::printf("  FAIL [%s]\n", label);
        ++g_failures;
    }
}

static BindingResolverStatus resolve(const char*, char* out, size_t out_len) {
    strlcpy(out, "ok", out_len);
    return BINDING_RESOLVER_RESOLVED;
}

static uint8_t finite_key_count() { return 2; }

static const char* finite_key_at(uint8_t index) {
    static const char* const keys[] = {"one", "two"};
    return index < finite_key_count() ? keys[index] : nullptr;
}

static BindingResolverStatus unavailable_resolve(const char* params, char*, size_t) {
    return (std::strcmp(params, "one") == 0 || std::strcmp(params, "two") == 0)
               ? BINDING_RESOLVER_UNAVAILABLE : BINDING_RESOLVER_UNKNOWN;
}

static BindingResolverStatus declared_key_unknown_resolve(const char*, char*, size_t) {
    return BINDING_RESOLVER_UNKNOWN;
}

static bool all_finite_keys_are_recognized() {
    bool all_recognized = true;
    char resolved[8] = {};
    for (uint8_t scheme_index = 0; scheme_index < binding_template_scheme_count(); ++scheme_index) {
        const char* scheme = binding_template_scheme_name(scheme_index);
        const BindingSchemeSpec* spec = binding_template_scheme_spec(scheme_index);
        if (!scheme || !spec || spec->free_form) continue;
        for (uint8_t key_index = 0; key_index < spec->key_count(); ++key_index) {
            const char* key = spec->key_at(key_index);
            if (binding_template_resolve_registered(scheme, std::strlen(scheme), key,
                                                    resolved, sizeof(resolved)) ==
                BINDING_RESOLVER_UNKNOWN) {
                all_recognized = false;
            }
        }
    }
    return all_recognized;
}

int main() {
    const BindingSchemeSpec finite = {
        1, 2, 1, 1, BINDING_VALIDATION_STANDARD, false, finite_key_count, finite_key_at
    };
    const BindingSchemeSpec free_form = {
        1, 1, 1, -1, BINDING_VALIDATION_STRUCTURAL_ONLY, true, nullptr, nullptr
    };

    expect(binding_template_register("finite", resolve, nullptr, finite), "finite scheme registered");
        expect(binding_template_register("unavailable", unavailable_resolve, nullptr, finite),
            "unavailable finite scheme registered");
    expect(binding_template_register("free", resolve, nullptr, free_form), "free scheme registered");
    expect(binding_template_validate_params("finite", 6, "one") == nullptr, "known key accepted");
    expect(binding_template_validate_params("finite", 6, "missing") != nullptr, "unknown key rejected");

    char resolved[8] = {};
    expect(all_finite_keys_are_recognized(), "declared finite keys are recognized by their resolvers");
    expect(binding_template_resolve_registered("unavailable", 11, "one", resolved, sizeof(resolved)) ==
               BINDING_RESOLVER_UNAVAILABLE,
           "declared finite key can be unavailable");
    expect(binding_template_resolve_registered("unavailable", 11, "missing", resolved, sizeof(resolved)) ==
               BINDING_RESOLVER_UNKNOWN,
           "undeclared finite key is unknown");
        expect(binding_template_register("declared_unknown", declared_key_unknown_resolve, nullptr, finite),
            "declared key regression scheme registered");
        expect(!all_finite_keys_are_recognized(),
            "generic scan catches a declared finite key reported as unknown");

    JsonDocument doc;
    JsonArray schemes = doc["schemes"].to<JsonArray>();
    binding_schema_emit(&schemes);

    JsonObject finite_json = schemes[0];
    expect(std::strcmp(finite_json["name"], "finite") == 0, "finite name emitted");
    expect(finite_json["min_params"] == 1, "minimum parameter count emitted");
    expect(finite_json["format_param"] == 1, "format parameter emitted");
    expect(!finite_json["free_form"], "finite scheme emitted as constrained");
    expect(finite_json["keys"].size() == finite_key_count(), "every finite key emitted");
    expect(std::strcmp(finite_json["keys"][1], "two") == 0, "finite key value emitted");

    JsonObject free_json = schemes[2];
    expect(std::strcmp(free_json["name"], "free") == 0, "free-form name emitted");
    expect(free_json["free_form"], "free-form flag emitted");
    expect(!free_json["keys"].is<JsonArray>(), "free-form scheme omits finite keys");

    std::printf("=== Results: %d failure(s) ===\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}