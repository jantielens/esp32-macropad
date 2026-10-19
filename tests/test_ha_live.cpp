#include "ha_live_state.h"
#include <gtest/gtest.h>
#include <string>

namespace {
using namespace ArduinoJson;
const char* entity = "sensor.power";

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
}
