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

static const BindingAuthoringKey kFiniteAuthoringKeys[] = {
    {"one", BINDING_AUTHORING_KIND_INTEGER, "", BINDING_AUTHORING_FORMAT_RAW |
        BINDING_AUTHORING_FORMAT_INTEGER, "%ld", BINDING_AUTHORING_CAP_DISPLAY, true, "42"},
    {"two", BINDING_AUTHORING_KIND_TEXT, nullptr, BINDING_AUTHORING_FORMAT_RAW,
        nullptr, BINDING_AUTHORING_CAP_DISPLAY, true},
    {"unregistered", BINDING_AUTHORING_KIND_TEXT, nullptr, BINDING_AUTHORING_FORMAT_RAW,
        nullptr, BINDING_AUTHORING_CAP_DISPLAY, true},
};
static const BindingAuthoringSpec kFiniteAuthoring = {
    kFiniteAuthoringKeys,
    static_cast<uint8_t>(sizeof(kFiniteAuthoringKeys) / sizeof(kFiniteAuthoringKeys[0])),
};

static const BindingParamDoc kFreeParams[] = {
    {"format", "Display format.", "%H:%M"},
};
static const BindingExampleDoc kFreeExamples[] = {
    {"[free:%H:%M]", "Example."},
};
static const BindingSchemeDoc kFreeDoc = {
    "Test", "Free-form test scheme.", kFreeParams, 1, nullptr, 0,
    kFreeExamples, 1, nullptr, false, nullptr, 0, nullptr, nullptr,
};
static uint8_t patterned_key_count() { return 2; }
static const char* patterned_key_at(uint8_t index) {
    static char key[24];
    std::snprintf(key, sizeof(key), "sensor_%u", unsigned(index + 1));
    return index < patterned_key_count() ? key : nullptr;
}
static const BindingAuthoringKey kPatternedKeys[] = {
    binding_authoring_integer("sensor_#", "ms", "125"),
    binding_authoring_integer("sensor_2", "s", "2"),
};
static const BindingAuthoringSpec kPatternedAuthoring = {BINDING_DOC_LIST(kPatternedKeys)};

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
        1, 2, 1, 1, BINDING_VALIDATION_STANDARD, false, finite_key_count, finite_key_at, nullptr,
        &kFiniteAuthoring,
    };
    const BindingSchemeSpec free_form = {
        1, 1, 1, -1, BINDING_VALIDATION_STRUCTURAL_ONLY, true, nullptr, nullptr, &kFreeDoc
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
    expect(!finite_json["authoring"].is<JsonObject>(),
           "schemes without optional authoring metadata remain valid");

    JsonObject free_json = schemes[2];
    expect(std::strcmp(free_json["name"], "free") == 0, "free-form name emitted");
    expect(free_json["free_form"], "free-form flag emitted");
    expect(!free_json["keys"].is<JsonArray>(), "free-form scheme omits finite keys");
    expect(!free_json["authoring"].is<JsonObject>(),
           "free-form schemes without authoring metadata remain manual-only");

    JsonDocument docs_doc;
    JsonArray docs_schemes = docs_doc["schemes"].to<JsonArray>();
    binding_schema_emit(&docs_schemes, true);
    JsonObject finite_docs = docs_schemes[0];
    expect(finite_docs["authoring"].is<JsonObject>(),
           "docs-enabled schema emits optional authoring metadata");
    JsonArray authoring_keys = finite_docs["authoring"]["keys"].as<JsonArray>();
    expect(authoring_keys.size() == 2 &&
               std::strcmp(authoring_keys[0]["key"], "one") == 0,
           "authoring metadata emits only registered finite keys");
    expect(authoring_keys[0]["unit"].is<const char*>() &&
               std::strcmp(authoring_keys[0]["unit"], "") == 0,
           "known unit absence remains distinct from unknown unit");
    expect(std::strcmp(authoring_keys[1]["key"], "two") == 0 &&
               !authoring_keys[1]["unit"].is<const char*>(),
           "unknown units are omitted from authoring metadata");
    expect(authoring_keys[0]["formats"].size() == 2 &&
               std::strcmp(authoring_keys[0]["formats"][1], "integer") == 0 &&
               std::strcmp(authoring_keys[0]["integer_format"], "%ld") == 0,
           "integer mode carries its provider-verified format");
    expect(std::strcmp(authoring_keys[0]["example_value"], "42") == 0,
           "authoring key emits its representative example value");
    JsonArray free_params = docs_schemes[2]["params"].as<JsonArray>();
    expect(std::strcmp(free_params[0]["default"], "%H:%M") == 0,
           "binding parameter emits its authoring default");
    expect(!docs_schemes[2]["authoring"].is<JsonObject>(),
           "docs-enabled schema accepts schemes without optional authoring metadata");

    expect(binding_template_register("patterned", resolve, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false, patterned_key_count, patterned_key_at,
         nullptr, &kPatternedAuthoring}), "patterned scheme registered");
    expect(binding_template_register("structural", resolve, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr,
         nullptr, &kPatternedAuthoring}), "structural scheme registered");
    JsonDocument patterns;
    JsonArray pattern_schemes = patterns["schemes"].to<JsonArray>();
    binding_schema_emit(&pattern_schemes, true);
    JsonArray expanded = pattern_schemes[4]["authoring"]["keys"];
    patterned_key_at(0);
    expect(expanded.size() == 2 && std::strcmp(expanded[0]["key"], "sensor_1") == 0 &&
               std::strcmp(expanded[1]["key"], "sensor_2") == 0,
           "pattern expansion copies keys backed by a reusable provider buffer");
    expect(std::strcmp(expanded[0]["unit"], "ms") == 0 &&
               std::strcmp(expanded[1]["unit"], "s") == 0,
           "exact authoring keys take precedence over matching patterns");
    expect(std::strcmp(expanded[0]["integer_format"], "") == 0,
           "raw whole-number output needs no invented printf parameter");
    JsonArray structural = pattern_schemes[5]["authoring"]["keys"];
    expect(structural.size() == 2 && std::strcmp(structural[0]["key"], "sensor_#") == 0,
           "structural schemes preserve declared authoring patterns");

    std::printf("=== Results: %d failure(s) ===\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}