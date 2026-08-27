#include <cstdio>
#include <cstring>

#include "binding_finite_schemes.h"
#include "binding_template.h"
#include "list_binding.h"
#include "list_provider.h"
#include "time_binding.h"

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

int main() {
    list_provider_register(&kFixtureProvider);
    list_binding_set_selected("fixture", "selected-item");
    time_binding_init();
    list_binding_init();
    binding_finite_schemes_init();
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

    std::printf("=== Results: %d failure(s) ===\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}