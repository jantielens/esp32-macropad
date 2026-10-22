#include "ha_live_state.h"
#include "ha_lookup_data.h"
#include "ha_lookup.h"
#include "bounded_psram_json_allocator.h"
#include <gtest/gtest.h>
#include <string>
#include "binding_template.h"
#include "expr_binding.h"
#include "ha_binding.h"
#include "ha_live.h"

static bool thermostat_connected = true;
static std::string thermostat_snapshot;

void ha_live_status(HaLiveStatus& status) {
    status = {true, thermostat_connected, 1, 0, 0, "test", ""};
}

bool ha_live_valid_entity(const char* value) {
    return strcmp(value, "climate.bureau_boven_thermostaat") == 0;
}

bool ha_live_read(const char*, char* out, size_t size) {
    if (!thermostat_connected) return false;
    ArduinoJson::JsonDocument snapshot;
    if (deserializeJson(snapshot, thermostat_snapshot)) return false;
    if (!ha_live_state_available(snapshot.as<ArduinoJson::JsonVariantConst>())) return false;
    snprintf(out, size, "%s", thermostat_snapshot.c_str());
    return true;
}

namespace {
using namespace ArduinoJson;
const char* entity = "sensor.power";

TEST(HaLive, BureauProductionExpressionAndNestedDiscovery) {
    ha_binding_init();
    expr_binding_init();
    const char* source = "[expr:[ha:climate.bureau_boven_thermostaat;attributes.temperature]>20?\"on\":\"off\"]";
    char resolved[BINDING_TEMPLATE_MAX_LEN];
    for (int temperature : {10, 19, 20, 21, 22}) {
        thermostat_snapshot = "{\"state\":\"heat\",\"attributes\":{\"temperature\":" + std::to_string(temperature) + "}}";
        EXPECT_EQ(binding_template_resolve_status(source, resolved, sizeof(resolved)), BINDING_RESOLVER_RESOLVED);
        EXPECT_STREQ(resolved, temperature > 20 ? "on" : "off");
    }
    for (const char* invalid : {"null", "\"\"", "\"unknown\"", "\"unavailable\"", "\"nonnumeric\""}) {
        thermostat_snapshot = std::string("{\"state\":\"heat\",\"attributes\":{\"temperature\":") + invalid + "}}";
        EXPECT_NE(binding_template_resolve_status(source, resolved, sizeof(resolved)), BINDING_RESOLVER_RESOLVED);
        EXPECT_STRNE(resolved, "off");
    }
    for (const char* snapshot : {"{\"state\":\"heat\",\"attributes\":{}}", "{\"state\":\"unknown\"}", "{\"state\":\"unavailable\"}"}) {
        thermostat_snapshot = snapshot;
        EXPECT_NE(binding_template_resolve_status(source, resolved, sizeof(resolved)), BINDING_RESOLVER_RESOLVED);
    }
    thermostat_connected = false;
    EXPECT_NE(binding_template_resolve_status(source, resolved, sizeof(resolved)), BINDING_RESOLVER_RESOLVED);
    thermostat_connected = true;
    thermostat_snapshot = "{\"state\":\"heat\",\"attributes\":{\"temperature\":22}}";
    EXPECT_EQ(binding_template_resolve_status(source, resolved, sizeof(resolved)), BINDING_RESOLVER_RESOLVED);
    EXPECT_STREQ(resolved, "on");
    unsigned count = 0;
    binding_template_visit_tokens(source, [](const char* scheme, size_t length, const char* params, void* context) {
        if (length == 2 && strncmp(scheme, "ha", length) == 0) {
            EXPECT_STREQ(params, "climate.bureau_boven_thermostaat;attributes.temperature");
            ++*static_cast<unsigned*>(context);
        }
    }, &count);
    EXPECT_EQ(count, 1u);
}

const char* apply(JsonDocument& state, const std::string& payload) {
    JsonDocument event;
    const auto error = deserializeJson(event, payload);
    if (error) return "test input invalid";
    return ha_live_apply_state(state, event.as<JsonObjectConst>(), entity);
}

TEST(HaLive, SnapshotThenStateAndAttributeDeltas) {
    JsonDocument state;
    ASSERT_EQ(apply(state, R"({"a":{"sensor.power":{"s":"12.5","a":{"unit":"W","nested":{"value":1},"keep":true},"lc":10,"lu":11}}})"), nullptr);
    EXPECT_TRUE(ha_live_state_available(state.as<JsonVariantConst>()));
    EXPECT_STREQ(state["entity_id"], entity);
    ASSERT_EQ(apply(state, R"({"c":{"sensor.power":{"+":{"s":"14","a":{"nested":{"value":2},"new":42},"lu":12}}}})"), nullptr);
    EXPECT_STREQ(state["state"], "14");
    EXPECT_STREQ(state["attributes"]["unit"], "W");
    EXPECT_TRUE(state["attributes"]["keep"].as<bool>());
    EXPECT_EQ(state["attributes"]["nested"]["value"], 2);
    EXPECT_EQ(state["attributes"]["new"], 42);
    EXPECT_EQ(state["last_changed"], 10);
    EXPECT_EQ(state["last_updated"], 12);
    ASSERT_EQ(apply(state, R"({"c":{"sensor.power":{"-":{"a":["unit","missing"]}}}})"), nullptr);
    EXPECT_TRUE(state["attributes"]["unit"].isUnbound());
    EXPECT_EQ(state["attributes"]["new"], 42);
}

TEST(HaLive, AttributeOnlyUpdatesAndUnavailableStates) {
    JsonDocument state;
    ASSERT_EQ(apply(state, R"({"a":{"sensor.power":{"s":"1","a":{"brightness":10}}}})"), nullptr);
    ASSERT_EQ(apply(state, R"({"c":{"sensor.power":{"+":{"a":{"brightness":20}}}}})"), nullptr);
    EXPECT_STREQ(state["state"], "1");
    EXPECT_EQ(state["attributes"]["brightness"], 20);
    ASSERT_EQ(apply(state, R"({"c":{"sensor.power":{"+":{"s":"unavailable"}}}})"), nullptr);
    EXPECT_FALSE(ha_live_state_available(state.as<JsonVariantConst>()));
    ASSERT_EQ(apply(state, R"({"c":{"sensor.power":{"+":{"s":"unknown"}}}})"), nullptr);
    EXPECT_FALSE(ha_live_state_available(state.as<JsonVariantConst>()));
    ASSERT_EQ(apply(state, R"({"c":{"sensor.power":{"+":{"s":"0"}}}})"), nullptr);
    EXPECT_TRUE(ha_live_state_available(state.as<JsonVariantConst>()));
}

TEST(HaLive, RemovalAndNewSnapshotDoNotRetainOldAttributes) {
    JsonDocument state;
    ASSERT_EQ(apply(state, R"({"a":{"sensor.power":{"s":"1","a":{"old":10}}}})"), nullptr);
    ASSERT_EQ(apply(state, R"({"r":["sensor.other"]})"), nullptr);
    EXPECT_TRUE(ha_live_state_available(state.as<JsonVariantConst>()));
    ASSERT_EQ(apply(state, R"({"r":["sensor.power"]})"), nullptr);
    EXPECT_FALSE(ha_live_state_available(state.as<JsonVariantConst>()));
    EXPECT_NE(apply(state, R"({"c":{"sensor.power":{"+":{"s":"2"}}}})"), nullptr);
    ASSERT_EQ(apply(state, R"({"a":{"sensor.power":{"s":"3","a":{}}}})"), nullptr);
    EXPECT_TRUE(state["attributes"]["old"].isUnbound());
}

TEST(HaLive, RejectMalformedStateAndAttributeChanges) {
    JsonDocument state;
    EXPECT_NE(apply(state, R"({"a":{"sensor.power":{"s":1,"a":{}}}})"), nullptr);
    EXPECT_NE(apply(state, R"({"a":{"sensor.power":{"s":"1","a":[]}}})"), nullptr);
    ASSERT_EQ(apply(state, R"({"a":{"sensor.power":{"s":"1","a":{}}}})"), nullptr);
    EXPECT_NE(apply(state, R"({"c":{"sensor.power":{"+":{"a":123}}}})"), nullptr);
    EXPECT_NE(apply(state, R"({"c":{"sensor.power":{"-":{"a":[123]}}}})"), nullptr);
    EXPECT_NE(apply(state, R"({"c":{"sensor.power":{"+":{"s":"a\u0000b"}}}})"), nullptr);
}

TEST(HaLive, FragmentedMessagesAreBoundedAndResetBetweenMessages) {
    std::string storage(15 * 1024 + 1, '\0');
    HaLiveMessage frame{storage.data(), storage.size() - 1};
    const std::string body(15 * 1024, 'x');
    ASSERT_EQ(frame.append(body.data(), 10, true, true), nullptr);
    EXPECT_TRUE(frame.fragmented);
    ASSERT_EQ(frame.append(body.data() + 10, body.size() - 10, false, false), nullptr);
    EXPECT_FALSE(frame.fragmented);
    EXPECT_EQ(frame.size, body.size());
    EXPECT_EQ(storage[frame.size], '\0');
    EXPECT_NE(frame.append("x", 1, false, false), nullptr);
    ASSERT_EQ(frame.append("ok", 2, true, false), nullptr);
    EXPECT_STREQ(frame.data, "ok");
    EXPECT_NE(frame.append(body.data(), body.size() + 1, true, false), nullptr);
    ASSERT_EQ(frame.append("first", 5, true, true), nullptr);
    EXPECT_NE(frame.append("overlap", 7, true, false), nullptr);
    EXPECT_NE(frame.append(body.data(), body.size(), false, false), nullptr);
}

TEST(HaLookup, AttributePathsMatchResolverAndExcludeUnsupportedKeys) {
    JsonDocument state, detail;
    ASSERT_FALSE(deserializeJson(state, R"({"entity_id":"light.kitchen","state":"on","attributes":{"brightness":128,"nested":{"number":2.5,"enabled":true},"bad.key":1,"bad|key":2,"list":[1,2],"empty":null}})"));
    ASSERT_EQ(ha_lookup_describe_entity(state.as<JsonObjectConst>(), "light.kitchen", detail), nullptr);
    EXPECT_STREQ(detail["state"], "on");
    const auto paths = detail["paths"].as<JsonArrayConst>();
    ASSERT_EQ(paths.size(), 3U);
    EXPECT_STREQ(paths[0]["path"], "attributes.brightness");
    EXPECT_STREQ(paths[0]["value"], "128");
    EXPECT_STREQ(paths[1]["path"], "attributes.nested.number");
    EXPECT_STREQ(paths[2]["value"], "true");
    EXPECT_TRUE(detail["more"].as<bool>());
    EXPECT_NE(ha_lookup_describe_entity(state.as<JsonObjectConst>(), "sensor.other", detail), nullptr);
    state.clear();
    EXPECT_STREQ(paths[1]["path"], "attributes.nested.number");
    EXPECT_STREQ(paths[1]["value"], "2.5");
}

TEST(HaLookup, BoundsAttributeCountAndSamples) {
    JsonDocument state, detail;
    state["entity_id"] = "sensor.power";
    state["state"] = std::string(300, 'x');
    for (unsigned i = 0; i < 40; ++i)
        state["attributes"][std::string("value_") + std::to_string(i)] = std::string(300, 'y');
    ASSERT_EQ(ha_lookup_describe_entity(state.as<JsonObjectConst>(), "sensor.power", detail), nullptr);
    EXPECT_EQ(strlen(detail["state"].as<const char*>()), 192U);
    EXPECT_TRUE(detail["state_truncated"].as<bool>());
    ASSERT_EQ(detail["paths"].size(), 32U);
    EXPECT_EQ(strlen(detail["paths"][0]["value"].as<const char*>()), 192U);
    EXPECT_TRUE(detail["paths"][0]["truncated"].as<bool>());
    EXPECT_TRUE(detail["more"].as<bool>());
    EXPECT_LE(measureJson(detail), 12U * 1024U);
}

TEST(HaLookup, TruncationPreservesUtf8AndOmitsUnaddressablePaths) {
    JsonDocument state, detail;
    state["entity_id"] = "sensor.power";
    state["state"] = std::string(191, 'x') + "\xc2\xb0";
    state["attributes"][std::string(120, 'a')] = 1;
    state["attributes"]["a"]["b"]["c"]["d"]["e"] = 2;
    ASSERT_EQ(ha_lookup_describe_entity(state.as<JsonObjectConst>(), "sensor.power", detail), nullptr);
    EXPECT_EQ(strlen(detail["state"].as<const char*>()), 191U);
    EXPECT_TRUE(detail["paths"].as<JsonArrayConst>().size() == 0);
    EXPECT_TRUE(detail["more"].as<bool>());
}

TEST(HaLookup, PsramJsonBudgetSurvivesFailedGrowthAndReleasesMemory) {
    BoundedPsramJsonAllocator allocator(256);
    void* first = allocator.allocate(64);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(allocator.allocate(256), nullptr);
    EXPECT_EQ(allocator.reallocate(first, 256), nullptr);
    first = allocator.reallocate(first, 32);
    ASSERT_NE(first, nullptr);
    allocator.deallocate(first);
    void* reused = allocator.allocate(192);
    ASSERT_NE(reused, nullptr);
    allocator.deallocate(reused);
    EXPECT_LE(allocator.peak, 256U);
}

TEST(HaLookup, ConfigurationAndOtaInvalidateRunningAndCompletedLookups) {
    HaLookupContext context{10, 20};
    EXPECT_TRUE(context.current(10, 20, false));
    EXPECT_FALSE(context.current(10, 20, true));
    EXPECT_FALSE(context.current(10, 21, false)); // OTA finished before the next checkpoint.
    EXPECT_FALSE(context.current(11, 20, false));
    context = {11, 21};
    EXPECT_TRUE(context.current(11, 21, false));
}
}
