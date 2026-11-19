// Typed action binding discovery tests.

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "binding_template.h"
#include "action_dispatch.h"
#include "pad_config.h"
#include "action_registry.h"
#include "action_parse.h"
#include "action_storage.h"
#include "binding_sources.h"
#include "music_analysis.h"
#include "pad_binding.h"
#include "widgets/widget_registry.h"
#include <string>
#include <vector>

ActionDispatchContext action_dispatch_context() { return {}; }
void display_manager_request_binding_refresh() {}
void display_manager_lock_if_needed(bool* locked) { *locked = false; }
void display_manager_unlock_if_needed(bool) {}
ActionResult action_dispatch(const ButtonAction&, const char*, uint32_t, bool) {
    return ACTION_COMPLETE;
}
static const PadConfig* scan_pads[2] = {};
const PadConfig* pad_config_acquire(uint8_t index) {
    return index < 2 ? scan_pads[index] : nullptr;
}
void pad_config_release(const PadConfig*) {}
const WidgetType* widget_find(const char*) { return nullptr; }
extern "C" void mqtt_sub_store_ensure_binding_subscribed(const char*) {}

extern "C" unsigned long millis() { return 0; }

static BindingResolverStatus mock_resolve(const char* params, char* out, size_t out_len) {
    (void)params;
    strlcpy(out, "RESOLVED", out_len);
    return BINDING_RESOLVER_RESOLVED;
}

static int g_pass = 0;
static int g_fail = 0;

static void check_true(bool cond, const char* label) {
    if (!cond) {
        printf("  FAIL [%s]\n", label);
        g_fail++;
        return;
    }
    g_pass++;
}

static void collect_scanned_topic(const char* params, void* context) {
    static_cast<std::vector<std::string>*>(context)->emplace_back(params);
}

static void test_action_only_binding_discovery() {
    static PadConfig pads[2] = {};
    static ScreenButtonConfig button = {};
    ActionStorage storage = {};
    JsonDocument document;
    const char* actions[] = {
        R"({"op":"variable.set","args":{"name":"first","value":{"$template":"[pad:chosen]"}}})",
        R"({"op":"variable.set","args":{"name":"second","value":{"$template":"[pad:chosen]"}}})",
        R"({"op":"variable.set","args":{"name":"audio","value":{"$template":"[music:analysis.rms]"}}})",
        R"({"op":"variable.set","args":{"name":"fixed","value":"[music:analysis.peak] [mqtt:fixed]"}})",
        R"({"op":"mqtt.publish","args":{"topic":"out","payload":{"$template":"[mqtt:other]"}}})"
    };
    ButtonAction* targets[] = {&pads[0].pad_actions[0], &pads[1].pad_actions[0],
                               &button.actions[0], &button.lp_actions[0], &pads[0].pad_actions[1]};
    for (unsigned i = 0; i < 5; ++i) {
        document.clear();
        check_true(!deserializeJson(document, actions[i]), "action fixture parses");
        ActionStorageScope scope(&storage);
        action_parse(document.as<JsonObject>(), *targets[i]);
        check_true(targets[i]->type[0] == '@' && !storage.failed, "typed action stored");
    }
    pads[0].binding_count = pads[1].binding_count = 1;
    strlcpy(pads[0].bindings[0].name, "chosen", sizeof(pads[0].bindings[0].name));
    strlcpy(pads[1].bindings[0].name, "chosen", sizeof(pads[1].bindings[0].name));
    strlcpy(pads[0].bindings[0].value, "[mqtt:one]", sizeof(pads[0].bindings[0].value));
    strlcpy(pads[1].bindings[0].value, "[ha:sensor.two]", sizeof(pads[1].bindings[0].value));
    pads[0].pad_action_count = 2;
    pads[1].pad_action_count = 1;
    button.action_count = button.lp_action_count = 1;
    pads[0].button_count = 1;
    pads[0].buttons = &button;
    scan_pads[0] = &pads[0];
    scan_pads[1] = &pads[1];
    const PadBinding* original = pads[1].bindings;
    pad_binding_set_bindings(original, 1);
    PadBinding override_binding = {};
    strlcpy(override_binding.name, "chosen", sizeof(override_binding.name));
    strlcpy(override_binding.value, "[mqtt:wrong]", sizeof(override_binding.value));
    std::vector<std::string> topics;
    {
        PadBindingScope override_scope(&override_binding, 1);
        binding_sources_visit(binding_template_collect_topics, &topics);
        const PadBinding* restored = nullptr;
        uint8_t restored_count = 0;
        pad_binding_get_bindings(&restored, &restored_count);
        check_true(restored == &override_binding && restored_count == 1, "override context restored");
    }
    check_true(topics.size() == 3 && topics[0] == "one" && topics[1] == "other" &&
               topics[2] == "sensor.two",
               "action-only dynamic arguments and aliases are scanned, not fixed text");
    const PadBinding* restored = nullptr;
    uint8_t restored_count = 0;
    pad_binding_get_bindings(&restored, &restored_count);
    check_true(restored == original && restored_count == 1, "pad context restored");
    uint8_t demand = MUSIC_ANALYSIS_NONE;
    music_analysis_collect_pad_demand(&pads[0], &demand);
    check_true(demand == MUSIC_ANALYSIS_RMS, "action-only music demand excludes fixed text");
    pad_binding_set_bindings(nullptr, 0);
    scan_pads[0] = scan_pads[1] = nullptr;
    action_storage_clear(storage);
}

int main() {
    const BindingSchemeSpec free_form = {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr, nullptr};
    binding_template_register("mqtt", mock_resolve, collect_scanned_topic, free_form);
    binding_template_register("ha", mock_resolve, collect_scanned_topic, free_form);
    pad_binding_init();
    test_action_only_binding_discovery();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
