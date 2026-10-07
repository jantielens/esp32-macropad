#include <cstdio>
#include <cstring>

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

int main() {
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

    expect(binding_template_scheme_count() > 0, "production profile registered binding schemes");
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
    binding_test_alarm = {false, 0, 5, ALARM_SNOOZED, false, false, false, false, 0};
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