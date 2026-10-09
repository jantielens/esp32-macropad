#include <cstdio>
#include <cstring>
#include <string>

#include "binding_builtin_schemes.h"
#include "binding_template.h"
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
    expect(scheme_is_registered("mqtt") && scheme_is_registered("pad"),
           "mqtt and pad schemes registered on MQTT display profiles");
    expect_key_patterns();
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