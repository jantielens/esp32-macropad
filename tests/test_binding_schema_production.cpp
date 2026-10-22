#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>

#include "binding_builtin_schemes.h"
#include "binding_template.h"
#include "pad_binding.h"
#include "list_binding.h"
#include "list_provider.h"
#include "time_binding.h"
#include "alarm_manager.h"
#include "psram_json_allocator.h"

#if IS_VOICE_ASSISTANT
#include "device_classes/voice_assistant/voice_binding.h"
#endif
#if IS_DARKROOM_TIMER
#include "device_classes/darkroom_timer/expose_timer.h"
#include "device_classes/darkroom_timer/meter.h"
#include "device_classes/darkroom_timer/print_log.h"
#include "device_classes/darkroom_timer/test_strip.h"
#endif
#if IS_COFFEE_SCALE
#include "device_classes/coffee_scale/brew/brew_binding.h"
#include "device_classes/coffee_scale/scale_binding.h"
#endif
#if IS_SHUTTER_TESTER
#include "device_classes/shutter_tester/shutter_binding.h"
#endif

static int g_failures = 0;

static uint8_t fixture_provider_items(ListItem*, uint8_t) { return 0; }

static const ListProvider kFixtureProvider = {
    "fixture",
    "Fixture provider",
    fixture_provider_items,
};

static void expect(bool condition, const char* label) {
    if (!condition) {
        std::printf("  FAIL [%s]\n", label);
        ++g_failures;
    }
}

static bool all_registered_finite_keys_are_recognized() {
    bool all_recognized = true;
    char resolved[256] = {};
    for (uint8_t scheme_index = 0; scheme_index < binding_template_scheme_count(); ++scheme_index) {
        const char* scheme = binding_template_scheme_name(scheme_index);
        const BindingSchemeSpec* spec = binding_template_scheme_spec(scheme_index);
        if (!scheme || !spec || spec->free_form) continue;

        for (uint8_t key_index = 0; key_index < spec->key_count(); ++key_index) {
            const char* key = spec->key_at(key_index);
            const BindingResolverStatus status = binding_template_resolve_registered(
                scheme, std::strlen(scheme), key, resolved, sizeof(resolved));
            if (status == BINDING_RESOLVER_UNKNOWN) {
                std::printf("  UNKNOWN [%s:%s]\n", scheme, key ? key : "<null>");
                all_recognized = false;
            }
        }
    }
    return all_recognized;
}

static void expect_structural_resolvers_are_invoked() {
    char resolved[256] = {};
    expect(binding_template_resolve_registered("time", 4, "%ums", resolved, sizeof(resolved))
               != BINDING_RESOLVER_UNKNOWN,
           "time resolver accepts a free-form format");
    expect(binding_template_resolve_registered("list", 4, "fixture.selected", resolved, sizeof(resolved))
               != BINDING_RESOLVER_UNKNOWN,
           "list resolver accepts a fixture provider selection");
#if IS_SHUTTER_TESTER
    expect(binding_template_resolve_registered("shutter", 7, "available", resolved, sizeof(resolved))
               != BINDING_RESOLVER_UNKNOWN,
           "shutter resolver accepts a structural measurement key");
#endif
}

static bool scheme_is_registered(const char* expected) {
    for (uint8_t index = 0; index < binding_template_scheme_count(); ++index) {
        const char* scheme = binding_template_scheme_name(index);
        if (scheme && std::strcmp(scheme, expected) == 0) return true;
    }
    return false;
}

static bool has_text(const char* value) { return value && value[0]; }

static bool is_pattern(const char* key) {
    return std::strchr(key, '#') || std::strchr(key, '*');
}

static const BindingSchemeDoc* scheme_doc(const char* scheme, size_t length) {
    for (uint8_t index = 0; index < binding_template_scheme_count(); ++index) {
        const char* name = binding_template_scheme_name(index);
        if (name && std::strlen(name) == length && std::strncmp(name, scheme, length) == 0) {
            return binding_template_scheme_spec(index)->doc;
        }
    }
    return nullptr;
}

static bool pattern_documents(const BindingSchemeDoc* doc, const char* key, size_t key_length) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.*s", (int)key_length, key);
    for (uint8_t index = 0; doc && index < doc->key_doc_count; ++index) {
        if (is_pattern(doc->keys[index].key) && binding_key_doc_matches(doc->keys[index].key, buffer)) return true;
    }
    return false;
}

// Validates every [scheme:params] token in text, including nested ones.
static bool example_is_valid(const char* text, const char* context) {
    bool valid = true;
    for (const char* p = std::strchr(text, '['); p; p = std::strchr(p + 1, '[')) {
        const char* scheme = p + 1;
        const char* colon = scheme;
        while ((*colon >= 'a' && *colon <= 'z') || *colon == '_') ++colon;
        if (*colon != ':' || colon == scheme) continue;
        int depth = 1;
        const char* end = colon + 1;
        for (; *end && depth; ++end) {
            if (*end == '[') ++depth;
            else if (*end == ']') --depth;
        }
        const size_t scheme_length = (size_t)(colon - scheme);
        std::string params(colon + 1, (size_t)(end - colon - 2));
        if (depth || !binding_template_scheme_known(scheme, scheme_length)) {
            std::printf("  BAD EXAMPLE [%s]: %s\n", context, text);
            valid = false;
            continue;
        }
        const char* error = binding_template_validate_params(scheme, scheme_length, params.c_str());
        size_t key_length = 0;
        while (params[key_length] && params[key_length] != ';' && params[key_length] != '|') ++key_length;
        if (error && !(std::strcmp(error, "unknown binding key") == 0 &&
                       pattern_documents(scheme_doc(scheme, scheme_length), params.c_str(), key_length))) {
            std::printf("  BAD EXAMPLE [%s]: %s (%s)\n", context, text, error);
            valid = false;
        }
    }
    return valid;
}

static void expect_scheme_docs_complete() {
    for (uint8_t scheme_index = 0; scheme_index < binding_template_scheme_count(); ++scheme_index) {
        const char* scheme = binding_template_scheme_name(scheme_index);
        const BindingSchemeSpec* spec = binding_template_scheme_spec(scheme_index);
        const BindingSchemeDoc* doc = spec->doc;
        if (!doc) {
            std::printf("  UNDOCUMENTED [%s]\n", scheme);
            ++g_failures;
            continue;
        }
        bool ok = has_text(doc->category) && has_text(doc->summary) && doc->example_count > 0 &&
                  doc->param_count == spec->max_params;
        for (uint8_t i = 0; i < doc->param_count; ++i) {
            ok = ok && has_text(doc->params[i].name) && has_text(doc->params[i].desc);
        }
        for (uint8_t i = 0; i < doc->key_doc_count; ++i) {
            ok = ok && has_text(doc->keys[i].key) && has_text(doc->keys[i].desc);
        }
        for (uint8_t i = 0; i < doc->example_count; ++i) {
            ok = ok && has_text(doc->examples[i].desc) && example_is_valid(doc->examples[i].code, scheme);
        }
        for (uint8_t i = 0; i < doc->reference_count; ++i) {
            const BindingReferenceDoc& row = doc->reference[i];
            ok = ok && has_text(row.syntax) && has_text(row.desc) &&
                 (!row.example || example_is_valid(row.example, scheme));
        }
        ok = ok && (doc->reference_count == 0 || has_text(doc->reference_title));
        if (!ok) {
            std::printf("  INCOMPLETE DOC [%s]\n", scheme);
            ++g_failures;
        }

        if (spec->free_form) continue;
        for (uint8_t key_index = 0; key_index < spec->key_count(); ++key_index) {
            const char* key = spec->key_at(key_index);
            bool documented = false;
            for (uint8_t i = 0; i < doc->key_doc_count && !documented; ++i) {
                documented = binding_key_doc_matches(doc->keys[i].key, key);
            }
            if (!documented) {
                std::printf("  UNDOCUMENTED KEY [%s:%s]\n", scheme, key);
                ++g_failures;
            }
        }
        for (uint8_t i = 0; i < doc->key_doc_count; ++i) {
            if (is_pattern(doc->keys[i].key)) continue;
            bool exists = false;
            for (uint8_t key_index = 0; key_index < spec->key_count() && !exists; ++key_index) {
                exists = std::strcmp(doc->keys[i].key, spec->key_at(key_index)) == 0;
            }
            if (!exists) {
                std::printf("  STALE KEY DOC [%s:%s]\n", scheme, doc->keys[i].key);
                ++g_failures;
            }
        }
    }
}

static void expect_key_patterns() {
    expect(binding_key_doc_matches("#_state", "12_state"), "# matches digits");
    expect(!binding_key_doc_matches("#_state", "_state"), "# needs a digit");
    expect(!binding_key_doc_matches("#", "1_state"), "# does not span non-digits");
    expect(binding_key_doc_matches("*.selected", "pads.selected"), "* matches an id");
    expect(!binding_key_doc_matches("*.selected", "a.b.selected"), "* stops at a dot");
    expect(binding_key_doc_matches("seg_time:#", "seg_time:11"), "pattern suffix digits");
}

static void expect_health_authoring_contract() {
#if HAS_DISPLAY
    const BindingSchemeSpec* spec = nullptr;
    for (uint8_t index = 0; index < binding_template_scheme_count(); ++index) {
        const char* name = binding_template_scheme_name(index);
        if (name && std::strcmp(name, "health") == 0) {
            spec = binding_template_scheme_spec(index);
            break;
        }
    }
    expect(spec && spec->authoring, "health scheme has explicit authoring metadata");
    if (!spec || !spec->authoring) return;

    const BindingAuthoringSpec& authoring = *spec->authoring;
    static const char* const expected_keys[] = {
        "cpu", "rssi", "uptime", "heap_free", "wifi_ssid", "wifi_connected",
    };
    static const char* const expected_kinds[] = {
        "integer", "integer", "integer", "integer", "text", "text",
    };
    static const char* const expected_units[] = {"%", "dBm", "s", "B", "", ""};
    static const char* const expected_examples[] = {
        "42", "-62", "12345", "245760", "Home WiFi", "ON",
    };
    for (size_t index = 0; index < 6 && index < authoring.key_count; ++index) {
        const BindingAuthoringKey& key = authoring.keys[index];
        const char* kind = key.kind == BINDING_AUTHORING_KIND_INTEGER ? "integer" :
                           key.kind == BINDING_AUTHORING_KIND_TEXT ? "text" : "";
        expect(std::strcmp(key.key, expected_keys[index]) == 0,
               "health authoring key preserves its existing contract");
        expect(std::strcmp(kind, expected_kinds[index]) == 0,
               "health authoring kind matches the resolver value");
        expect(key.unit != nullptr, "health unit is known");
        expect(key.unit && std::strcmp(key.unit, expected_units[index]) == 0,
               "health authoring unit matches the catalog");
        expect(key.example_value &&
                   std::strcmp(key.example_value, expected_examples[index]) == 0,
               "health authoring example matches its value kind");
        expect(key.read_only, "health authoring values are read-only");
        if (index < 4) {
            expect(key.formats == (BINDING_AUTHORING_FORMAT_RAW |
                                   BINDING_AUTHORING_FORMAT_INTEGER),
                   "numeric health keys expose only raw and integer formats");
            expect(key.integer_format && std::strcmp(key.integer_format, "%ld") == 0,
                   "health integer formatting matches the resolver's long argument");
            expect(key.capabilities == (BINDING_AUTHORING_CAP_DISPLAY |
                                        BINDING_AUTHORING_CAP_NUMERIC_WIDGET |
                                        BINDING_AUTHORING_CAP_LIVE_SAMPLING),
                   "numeric health keys advertise display and live numeric use");
        } else {
            expect(key.formats == BINDING_AUTHORING_FORMAT_RAW,
                   "text health keys expose raw formatting only");
            expect(key.integer_format == nullptr,
                   "text health keys do not expose an integer format");
            expect(key.capabilities == BINDING_AUTHORING_CAP_DISPLAY,
                   "text health keys advertise display only");
        }
    }

    char value[64] = {};
    expect(binding_template_resolve_registered("health", 6, "cpu", value, sizeof(value)) ==
               BINDING_RESOLVER_RESOLVED && std::strcmp(value, "73") == 0,
           "health CPU raw output matches integer resolver data");
    expect(binding_template_resolve_registered("health", 6, "cpu;%ld", value, sizeof(value)) ==
               BINDING_RESOLVER_RESOLVED && std::strcmp(value, "73") == 0,
           "health CPU integer format resolves safely");
    expect(binding_template_resolve_registered("health", 6, "rssi", value, sizeof(value)) ==
               BINDING_RESOLVER_RESOLVED && std::strcmp(value, "-55") == 0,
           "health RSSI raw output matches integer resolver data");
    expect(binding_template_resolve_registered("health", 6, "uptime", value, sizeof(value)) ==
               BINDING_RESOLVER_RESOLVED && std::strcmp(value, "10") == 0,
           "health uptime is emitted in whole seconds");
    expect(binding_template_resolve_registered("health", 6, "uptime;%ld s", value, sizeof(value)) ==
               BINDING_RESOLVER_RESOLVED && std::strcmp(value, "10 s") == 0,
           "health uptime accepts the advertised integer format");
    expect(binding_template_resolve_registered("health", 6, "heap_free", value, sizeof(value)) ==
               BINDING_RESOLVER_RESOLVED && std::strcmp(value, "0") == 0,
           "health free heap is emitted as an integer");
    expect(binding_template_resolve_registered("health", 6, "heap_free;%ld B", value, sizeof(value)) ==
               BINDING_RESOLVER_RESOLVED && std::strcmp(value, "0 B") == 0,
           "health free heap accepts the advertised integer format");
    expect(binding_template_resolve_registered("health", 6, "wifi_ssid", value, sizeof(value)) ==
               BINDING_RESOLVER_RESOLVED && std::strcmp(value, "?") == 0,
           "health SSID preserves the resolver's unavailable text output");
    expect(binding_template_resolve_registered("health", 6, "wifi_connected", value, sizeof(value)) ==
               BINDING_RESOLVER_RESOLVED && std::strcmp(value, "OFF") == 0,
           "health connection state is raw text");
#endif
}

static const BindingAuthoringKey* authoring_for_key(const BindingSchemeSpec& spec, const char* key) {
    const BindingAuthoringKey* match = nullptr;
    for (uint8_t i = 0; spec.authoring && i < spec.authoring->key_count; ++i) {
        const auto& entry = spec.authoring->keys[i];
        if (std::strcmp(entry.key, key) == 0) return &entry;
        if (!match && binding_key_doc_matches(entry.key, key)) match = &entry;
    }
    return match;
}

static void expect_static_authoring_complete() {
    for (uint8_t i = 0; i < binding_template_scheme_count(); ++i) {
        const char* name = binding_template_scheme_name(i);
        if (!std::strcmp(name, "ha") || !std::strcmp(name, "mqtt") ||
            !std::strcmp(name, "expr") || !std::strcmp(name, "pad")) continue;
        const auto& spec = *binding_template_scheme_spec(i);
        if (!spec.authoring || !spec.authoring->key_count) {
            std::printf("  MISSING AUTHORING [%s]\n", name);
            ++g_failures;
            continue;
        }
        const uint8_t key_count = spec.free_form ? spec.doc->key_doc_count : spec.key_count();
        for (uint8_t j = 0; j < key_count; ++j) {
            const char* key = spec.free_form ? spec.doc->keys[j].key : spec.key_at(j);
            const auto* entry = authoring_for_key(spec, key);
            if (!entry) {
                std::printf("  MISSING AUTHORING KEY [%s:%s]\n", name, key);
                ++g_failures;
            } else if (!spec.free_form && entry->kind != BINDING_AUTHORING_KIND_TEXT) {
                char raw[256] = {};
                const auto status = binding_template_resolve_registered(
                    name, std::strlen(name), key, raw, sizeof(raw));
                if (status == BINDING_RESOLVER_RESOLVED && raw[0] &&
                    std::strcmp(raw, "?") && std::strcmp(raw, "---")) {
                    char* end = nullptr;
                    const double number = std::strtod(raw, &end);
                    if (end == raw || *end || !std::isfinite(number) ||
                        (entry->kind == BINDING_AUTHORING_KIND_INTEGER && std::trunc(number) != number)) {
                        std::printf("  NONNUMERIC RESOLVER [%s:%s]: %s\n", name, key, raw);
                        ++g_failures;
                    }
                }
            }
        }
        for (uint8_t j = 0; j < spec.authoring->key_count; ++j) {
            const auto& entry = spec.authoring->keys[j];
            expect(has_text(entry.example_value), "every static value has a representative sample");
            expect(entry.unit != nullptr, "every static value declares a unit or known unitless output");
            expect(entry.read_only && (entry.capabilities & BINDING_AUTHORING_CAP_DISPLAY),
                   "local sample metadata describes a read-only display value");
            expect(entry.formats & BINDING_AUTHORING_FORMAT_RAW, "raw output remains available");
            if (entry.kind != BINDING_AUTHORING_KIND_TEXT && entry.example_value) {
                char* end = nullptr;
                const double number = std::strtod(entry.example_value, &end);
                expect(end != entry.example_value && !*end && std::isfinite(number),
                       "numeric samples are complete finite numbers");
                expect(entry.kind != BINDING_AUTHORING_KIND_INTEGER || std::trunc(number) == number,
                       "integer samples are whole numbers");
            }
            if (entry.formats & BINDING_AUTHORING_FORMAT_INTEGER) {
                expect(entry.kind == BINDING_AUTHORING_KIND_INTEGER && entry.integer_format,
                       "whole-number mode declares an integer value and its native format");
                if (entry.integer_format && entry.integer_format[0])
                    expect(spec.format_param >= 0 && spec.format_param < spec.max_params,
                           "nonempty integer formats have a native format parameter");
            }
            if (entry.formats & BINDING_AUTHORING_FORMAT_NUMBER)
                expect(entry.kind == BINDING_AUTHORING_KIND_NUMBER &&
                           spec.format_param >= 0 && spec.format_param < spec.max_params,
                       "decimal formatting is explicitly supported by the resolver");
            if (entry.kind == BINDING_AUTHORING_KIND_TEXT)
                expect(entry.capabilities == BINDING_AUTHORING_CAP_DISPLAY,
                       "text samples never authorize numeric widgets or sampling");
        }
        for (uint8_t j = 0; spec.doc->reference_is_formats && j < spec.doc->reference_count; ++j) {
            const auto& row = spec.doc->reference[j];
            expect(has_text(row.sample) && row.unit && has_text(row.example),
                   "every offered format preset declares a sample, unit, and complete token");
        }
    }
}

void binding_schema_dump_json();

int main(int argc, char** argv) {
    list_provider_register(&kFixtureProvider);
    list_binding_set_selected("fixture", "selected-item");
    binding_builtin_schemes_init();
#if IS_VOICE_ASSISTANT
    voice_binding_init();
#endif
#if IS_DARKROOM_TIMER
    print_log_init();
    meter_init();
    expose_timer_init();
    test_strip_init();
#endif
#if IS_COFFEE_SCALE
    scale_binding_init();
    brew_binding_init();
#endif
#if IS_SHUTTER_TESTER
    shutter_binding_init();
#endif

    if (argc > 1 && std::strcmp(argv[1], "--emit-json") == 0) {
        binding_schema_dump_json();
        return 0;
    }

    expect(binding_template_scheme_count() > 0, "production profile registered binding schemes");
    expect(scheme_is_registered("mqtt") == bool(HAS_MQTT), "MQTT registration matches gate");
    expect(scheme_is_registered("pad") == bool(HAS_DISPLAY),
           "named bindings are registered whenever display is enabled");
    expect(scheme_is_registered("ha") == bool(HAS_HA_LIVE), "HA registration matches gate");
#if HAS_HA_LIVE
    {
        char value[64] = {};
        binding_template_resolve("[ha:sensor.power]", value, sizeof(value));
        expect(std::strcmp(value, "12.5") == 0, "HA state resolves through production scheme");
        binding_template_resolve("[ha:light.kitchen;attributes.brightness]", value, sizeof(value));
        expect(std::strcmp(value, "42") == 0, "HA attribute path resolves through production scheme");
        binding_template_resolve("[ha:sensor.power;.;%.1f W]", value, sizeof(value));
        expect(std::strcmp(value, "12.5 W") == 0, "HA formatting uses shared value formatting");
        binding_template_resolve("[ha:sensor.power;attributes.missing|Offline]", value, sizeof(value));
        expect(std::strcmp(value, "Offline") == 0, "missing HA attributes use outer fallback");
        binding_template_resolve("[expr:[ha:sensor.power]*2;%.0f]", value, sizeof(value));
        expect(std::strcmp(value, "25") == 0, "HA bindings compose with expressions");
        PadBinding named{};
        std::snprintf(named.name, sizeof(named.name), "power");
        std::snprintf(named.value, sizeof(named.value), "[ha:sensor.power]");
        pad_binding_set_bindings(&named, 1);
        binding_template_resolve("[pad:power;%.1f W]", value, sizeof(value));
        expect(std::strcmp(value, "12.5 W") == 0, "HA bindings compose with named pad values");
        pad_binding_set_bindings(nullptr, 0);
    }
#endif
    expect_key_patterns();
    expect_health_authoring_contract();
    expect_static_authoring_complete();
#if defined(BINDING_SCHEMA_PROFILE_FULL)
    {
        const BindingSchemeSpec* sensor = nullptr;
        for (uint8_t index = 0; index < binding_template_scheme_count(); ++index) {
            if (std::strcmp(binding_template_scheme_name(index), "sensor") == 0)
                sensor = binding_template_scheme_spec(index);
        }
        expect(sensor && sensor->authoring && sensor->authoring->key_count == 4,
               "local sensors declare authoring metadata for each registered field");
        if (sensor && sensor->authoring) {
            const BindingAuthoringKey& temperature = sensor->authoring->keys[0];
            expect(temperature.kind == BINDING_AUTHORING_KIND_NUMBER &&
                       temperature.formats == (BINDING_AUTHORING_FORMAT_RAW |
                                                BINDING_AUTHORING_FORMAT_NUMBER),
                   "sensor formatting matches the floating-point resolver");
            expect(temperature.example_value &&
                       std::strcmp(temperature.example_value, "21.37") == 0 &&
                       temperature.unit && std::strcmp(temperature.unit, "\xC2\xB0" "C") == 0,
                   "sensor definition supplies a representative sample and unit");
            expect(temperature.read_only &&
                       (temperature.capabilities & BINDING_AUTHORING_CAP_NUMERIC_WIDGET),
                   "sensor numeric support is explicit rather than inferred from its sample");
        }
        char value[64] = {};
        binding_template_resolve("[sensor:temperature|Offline]", value, sizeof(value));
        expect(std::strcmp(value, "Offline") == 0,
               "authoring examples never replace unavailable runtime readings");
    }
#endif
    {
        const BindingSchemeSpec* spec = nullptr;
        for (uint8_t index = 0; index < binding_template_scheme_count(); ++index) {
            const char* name = binding_template_scheme_name(index);
            if (name && std::strcmp(name, "time") == 0) {
                spec = binding_template_scheme_spec(index);
                break;
            }
        }
        expect(spec && spec->doc && spec->doc->param_count &&
                   spec->doc->params[0].default_value &&
                   std::strcmp(spec->doc->params[0].default_value, "%H:%M") == 0,
               "time binding provides a usable default format");
    }
#if HAS_DISPLAY
    {
        char value[64] = {};
        PadBinding named{};
        std::snprintf(named.name, sizeof(named.name), "local");
        std::snprintf(named.value, sizeof(named.value), "[health:cpu]");
        pad_binding_set_bindings(&named, 1);
        binding_template_resolve("[pad:local]", value, sizeof(value));
        expect(std::strcmp(value, "73") == 0,
               "display-only named bindings resolve local health values");
        pad_binding_set_bindings(nullptr, 0);
    }
#endif
    expect_scheme_docs_complete();
    expect(all_registered_finite_keys_are_recognized(),
           "every production finite key is not unknown to its real resolver");
    expect_structural_resolvers_are_invoked();
#if ALARM_ENABLED
    expect(scheme_is_registered("alarm"), "alarm registered when enabled");
    extern AlarmSnapshot binding_test_alarm;
    auto alarm_expect = [](const char* key, const char* expected) {
        char value[32] = {};
        expect(binding_template_resolve_registered("alarm", 5, key, value, sizeof(value)) == BINDING_RESOLVER_RESOLVED,
               key);
        expect(!strcmp(value, expected), expected);
    };
    alarm_expect("1_time", "07:30");
    alarm_expect("1_enabled", "ON");
    alarm_expect("1_ready", "ON");
    alarm_expect("1_state", "ringing");
    alarm_expect("active_id", "1");
    binding_test_alarm.weekdays = 62;
    binding_test_alarm.next_epoch = 1704094200;
    binding_test_alarm.next_seconds = 60;
    binding_test_alarm.next_ring_seconds = 0;
    binding_test_alarm.pending_commands = 2;
    alarm_expect("1_day_0", "OFF");
    alarm_expect("1_day_1", "ON");
    alarm_expect("1_minutes", "450");
    alarm_expect("1_next_seconds", "60");
    alarm_expect("1_next_available", "ON");
    alarm_expect("1_next_ring_seconds", "0");
    alarm_expect("1_save_state", "pending");
    binding_test_alarm.pending_commands = 0;
    binding_test_alarm.completed_commands = 3;
    binding_test_alarm.save_pending = true;
    alarm_expect("1_save_state", "pending");
    binding_test_alarm.save_failed = true;
    alarm_expect("1_save_state", "failed");
    binding_test_alarm.save_pending = binding_test_alarm.save_failed = false;
    alarm_expect("1_save_state", "saved");
    alarm_expect("1_snooze_seconds", "");
    binding_test_alarm = {false, 0, 5, ALARM_SNOOZED, false, false, false, false, 0};
    binding_test_alarm.snooze_remaining = 539;
    binding_test_alarm.next_ring_seconds = 539;
    alarm_expect("1_snooze_seconds", "539");
    alarm_expect("1_next_ring_seconds", "539");
    alarm_expect("1_next_seconds", "");
    alarm_expect("1_next_available", "OFF");
    alarm_expect("1_time", "00:05");
    alarm_expect("1_enabled", "OFF");
    alarm_expect("1_ready", "OFF");
    alarm_expect("1_state", "snoozed");
    alarm_expect("active_id", "1");
    binding_test_alarm.state = ALARM_IDLE;
    alarm_expect("1_state", "idle");
    alarm_expect("active_id", "0");
    char value[32] = {};
    expect(binding_template_resolve_registered("alarm", 5, "2_time", value, sizeof(value)) == BINDING_RESOLVER_UNKNOWN,
           "unknown alarm key rejected");
#else
    expect(!scheme_is_registered("alarm"), "alarm absent when disabled");
#endif
#if !HAS_PSRAM
    expect(!psramFound(), "no PSRAM present in the no-PSRAM profile");
    PsramJsonAllocator allocator;
    void* memory = allocator.allocate(128);
    expect(memory != nullptr, "JSON allocation falls back to internal heap");
    allocator.deallocate(memory);
#endif
#if HAS_CAMERA && HAS_DISPLAY
    expect(scheme_is_registered("camera"), "camera scheme registered on camera display profile");
#endif

    std::printf("=== Results: %d failure(s) ===\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}