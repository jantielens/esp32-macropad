#define HAS_BUTTON 1
#define NUM_HW_BUTTONS 2
#define HW_BUTTON_HOLD_MS 100
#define HW_BUTTON_DEFAULTS_JSON "{\"buttons\":[{\"hold_actions\":[{\"type\":\"screen\"}]}]}"
#include "mqtt_sub_store_test_support.h"
#undef PAD_CONFIG_H
#include "pad_config.h"
#include "LittleFS.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

struct HwButtonDef { uint8_t pin; bool active_low; const char* label; };
static constexpr HwButtonDef HW_BUTTON_DEFS[NUM_HW_BUTTONS] = {
    {1, true, "First"}, {2, true, "Second"}
};

#define hw_button_config_snapshot unused_global_hw_button_config_snapshot
#include "../src/app/hw_button_config.h"
#undef hw_button_config_snapshot
#include "../src/app/hw_buttons.h"
#include "../src/app/device_dialog.h"
#include "../src/app/action_dispatch.h"
#include "../src/app/mqtt_triggers.cpp"
#include "../src/app/action_storage.cpp"
#include "../src/app/action_parse.cpp"

#define IRAM_ATTR
#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define CHANGE 4
#define portENTER_CRITICAL_ISR(mux) portENTER_CRITICAL(mux)
#define portEXIT_CRITICAL_ISR(mux) portEXIT_CRITICAL(mux)
static int pin_levels[3] = {HIGH, HIGH, HIGH};
static unsigned long clock_ms = 0;
extern "C" unsigned long millis() { return clock_ms; }
static int digitalRead(int pin) { return pin_levels[pin]; }
static void pinMode(int, int) {}
static int digitalPinToInterrupt(int pin) { return pin; }
static void attachInterrupt(int, void (*)(), int) {}

#undef TAG
namespace hw_host {
#include "../src/app/hw_button_config.cpp"
#undef TAG
#include "../src/app/hw_buttons.cpp"
}

MqttManager mqtt_manager;
FakeFontStorage LittleFS;
FakeFontStorage SD_MMC;

namespace {
bool allocation_fails = false;
bool mutex_fails = false;
bool atomic_write_fails = false;
unsigned allocations = 0;
unsigned mutex_creations = 0;
unsigned dispatches = 0;
std::vector<MqttSubStoreTestOwnerCall> owner_calls;
std::string dispatched_argument;
const char* replacement_on_dispatch = nullptr;
const char* hw_replacement_on_dispatch = nullptr;
ActionContinuationOwner dispatched_owner = ACTION_CONTINUATION_OWNER_LOOP;
bool dispatched_interactive = false;

void expect_unlocked() {
    ASSERT_NE(g_lock, nullptr);
    EXPECT_TRUE(g_lock->signaled);
}
}

void* config_psram_alloc(size_t bytes, const char*) {
    ++allocations;
    expect_unlocked();
    return allocation_fails ? nullptr : calloc(1, bytes);
}

SemaphoreHandle_t test_freertos_create_mutex() {
    ++mutex_creations;
    if (mutex_fails) return nullptr;
    auto* semaphore = new StaticSemaphore_t{};
    semaphore->signaled = true;
    return semaphore;
}
void test_freertos_delete_mutex(SemaphoreHandle_t semaphore) { delete semaphore; }

bool storage_mount() { expect_unlocked(); return true; }
void storage_publish_usage(bool) { expect_unlocked(); }

void mqtt_sub_store_test_record_owner(const char* topic,
                                      MqttSubscriptionOwner owner, bool active) {
    expect_unlocked();
    MqttSubStoreTestOwnerCall call{};
    strlcpy(call.topic, topic, sizeof(call.topic));
    call.owner = owner;
    call.active = active;
    owner_calls.push_back(call);
}

const ActionArgumentDef argument = {"value", "Value", ACTION_ARGUMENT_STRING, 0, 0, 127,
                                    true, "", "text"};
const ActionOperationDef operation = {"test.set", "Test", &argument, 1, nullptr};

const char* action_operation_validate_json(JsonObjectConst action, bool) {
    if (strcmp(action["op"] | "", "test.set")) return "unknown operation";
    JsonVariantConst value = action["args"]["value"];
    if (value.is<const char*>()) return nullptr;
    if (value.is<JsonObjectConst>() && value["$template"].is<const char*>()) return nullptr;
    return "invalid argument";
}
const char* action_validate_json(JsonObjectConst action, bool, const char*, const char*) {
    if (!action.containsKey("type") && action.containsKey("op")) {
        return action_operation_validate_json(action, true);
    }
    if (action.containsKey("type") && action.containsKey("args")) return "mixed action";
    return action["type"].is<const char*>() ? nullptr : "missing type";
}
bool action_type_is_supported(const char* type) {
    return !strcmp(type, "screen") || !strcmp(type, ACTION_TYPE_VISUAL_ALERT);
}
const ActionTypeDef* action_type_find(const char*) { return nullptr; }
const ActionOperationDef* action_operation_find(const char* id, const ActionTypeDef**) {
    return id && !strcmp(id, operation.id) ? &operation : nullptr;
}
const ActionOperationDef* action_operation_for_action(const ButtonAction& action, const ActionTypeDef**) {
    return action.type[0] == '@' ? action.payload.operation.descriptor : nullptr;
}
bool device_dialog_active() { return false; }
uint32_t device_dialog_input_generation() { return 0; }
ActionResult action_dispatch(const ButtonAction&, const char*, uint32_t, bool) { return ACTION_COMPLETE; }
bool storage_write_atomic(const char* path, const uint8_t* data, size_t length) {
    if (atomic_write_fails) return false;
    LittleFS.files[path] = std::vector<uint8_t>(data, data + length);
    return true;
}
void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char*,
                          ActionContinuationOwner owner, bool interactive) {
    expect_unlocked();
    if (hw_host::g_lock) { EXPECT_TRUE(hw_host::g_lock->signaled); }
    dispatched_owner = owner;
    dispatched_interactive = interactive;
    if (count) {
        if (replacement_on_dispatch) {
            const char* replacement = replacement_on_dispatch;
            replacement_on_dispatch = nullptr;
            EXPECT_TRUE(mqtt_triggers_save_raw(
                reinterpret_cast<const uint8_t*>(replacement), strlen(replacement)));
        }
        if (hw_replacement_on_dispatch) {
            const char* replacement = hw_replacement_on_dispatch;
            hw_replacement_on_dispatch = nullptr;
            EXPECT_TRUE(hw_host::hw_button_config_save_raw(
                reinterpret_cast<const uint8_t*>(replacement), strlen(replacement)));
        }
        if (actions[0].type[0] == '@') {
            const auto* block = actions[0].payload.operation.arguments;
            dispatched_argument = action_storage_text(block, *action_storage_arguments(block));
        } else EXPECT_STREQ(actions[0].type, "screen");
        ++dispatches;
    }
}

class MqttTriggers : public testing::Test {
protected:
    void SetUp() override {
        allocation_fails = mutex_fails = atomic_write_fails = false;
        allocations = mutex_creations = dispatches = 0;
        dispatched_argument.clear();
        replacement_on_dispatch = nullptr;
        hw_replacement_on_dispatch = nullptr;
        clock_ms = 0;
        pin_levels[1] = pin_levels[2] = HIGH;
        owner_calls.clear();
        LittleFS = {};
        LittleFS.read_hook = expect_unlocked;
    }
    void TearDown() override {
        action_storage_clear(hw_host::g_storage);
        free(hw_host::g_config);
        hw_host::g_config = nullptr;
        delete hw_host::g_lock;
        hw_host::g_lock = nullptr;
        action_storage_clear(g_action_storage);
        free(g_triggers);
        g_triggers = nullptr;
        g_registered_topics = nullptr;
        delete g_lock;
        g_lock = nullptr;
        g_pending_mask = 0;
        g_generation = 0;
    }
    static void seed(const char* json) {
        LittleFS.files[MQTT_TRIGGERS_PATH] = std::vector<uint8_t>(json, json + strlen(json));
    }
    static bool save(const char* json) {
        return mqtt_triggers_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json));
    }
    static void expect_unavailable() {
        EXPECT_EQ(g_triggers, nullptr);
        mqtt_triggers_on_connected();
        const uint8_t payload[] = {'o', 'n'};
        mqtt_triggers_on_message("old", payload, sizeof(payload));
        mqtt_triggers_loop();
        EXPECT_TRUE(owner_calls.empty());
        EXPECT_EQ(dispatches, 0U);
        EXPECT_TRUE(LittleFS.open_calls.empty());
    }
};

TEST_F(MqttTriggers, MutexFailureDoesNotPublishStorageAndRetrySucceeds) {
    mutex_fails = true;
    mqtt_triggers_init();
    expect_unavailable();
    EXPECT_EQ(allocations, 0U);
    mutex_fails = false;
    mqtt_triggers_init();
    EXPECT_NE(g_triggers, nullptr);
    EXPECT_EQ(mutex_creations, 2U);
    EXPECT_EQ(allocations, 1U);
}

TEST_F(MqttTriggers, AllocationFailureLeavesUnavailableStateAndRetryReusesMutex) {
    allocation_fails = true;
    mqtt_triggers_init();
    expect_unavailable();
    allocation_fails = false;
    mqtt_triggers_init();
    EXPECT_NE(g_triggers, nullptr);
    EXPECT_EQ(allocations, 2U);
    EXPECT_EQ(mutex_creations, 1U);
}

TEST_F(MqttTriggers, LegacyVisualAlertCommandIsNotAMixedEnvelope) {
    for (bool validate_bindings : {false, true}) {
        JsonDocument trigger_doc;
        ASSERT_FALSE(deserializeJson(trigger_doc,
            R"({"triggers":[{"topic":"event","actions":[{"type":"visual_alert","op":"stop"}]}]})"));
        std::vector<MqttTriggerConfig> triggers(MAX_MQTT_TRIGGERS);
        ActionStorage trigger_storage = {};
        EXPECT_TRUE(parse_config(trigger_doc, triggers.data(), trigger_storage, validate_bindings));
        EXPECT_STREQ(triggers[0].actions[0].type, "visual_alert");
        action_storage_clear(trigger_storage);

        JsonDocument button_doc;
        ASSERT_FALSE(deserializeJson(button_doc,
            R"({"buttons":[{"tap_actions":[{"type":"visual_alert","op":"stop"}]}]})"));
        HwButtonConfig buttons[NUM_HW_BUTTONS] = {};
        ActionStorage button_storage = {};
        EXPECT_TRUE(hw_host::parse_config(button_doc, buttons, button_storage, validate_bindings));
        EXPECT_STREQ(buttons[0].tap_actions[0].type, "visual_alert");
        action_storage_clear(button_storage);

        trigger_doc["triggers"][0]["actions"][0]["args"].to<JsonObject>();
        EXPECT_FALSE(parse_config(trigger_doc, triggers.data(), trigger_storage, validate_bindings));
        button_doc["buttons"][0]["tap_actions"][0]["args"].to<JsonObject>();
        EXPECT_FALSE(hw_host::parse_config(button_doc, buttons, button_storage, validate_bindings));
    }
}

TEST_F(MqttTriggers, ReinitAndSaveKeepPointersAndReconcileRegisteredTopics) {
    seed(R"({"triggers":[{"topic":"old"},{"topic":"keep"}]})");
    mqtt_triggers_init();
    const auto* original = g_triggers;
    ASSERT_NE(original, nullptr);
    mqtt_triggers_on_connected();
    owner_calls.clear();

    seed(R"({"triggers":[{"topic":"new"},{"topic":"keep"}]})");
    mqtt_triggers_init();
    EXPECT_EQ(g_triggers, original);
    EXPECT_STREQ(original->topic, "new");
    EXPECT_EQ(allocations, 3U);
    EXPECT_EQ(mutex_creations, 1U);
    mqtt_triggers_on_connected();
    ASSERT_EQ(owner_calls.size(), 3U);
    EXPECT_STREQ(owner_calls[0].topic, "old");
    EXPECT_FALSE(owner_calls[0].active);
    EXPECT_STREQ(owner_calls[1].topic, "new");
    EXPECT_TRUE(owner_calls[1].active);
    EXPECT_STREQ(owner_calls[2].topic, "keep");
    EXPECT_TRUE(owner_calls[2].active);

    owner_calls.clear();
    ASSERT_TRUE(save(R"({"triggers":[{"topic":"keep"},{"topic":"added"}]})"));
    EXPECT_EQ(g_triggers, original);
    EXPECT_STREQ(original->topic, "keep");
    ASSERT_EQ(owner_calls.size(), 3U);
    EXPECT_STREQ(owner_calls[0].topic, "new");
    EXPECT_FALSE(owner_calls[0].active);
    EXPECT_STREQ(owner_calls[1].topic, "keep");
    EXPECT_TRUE(owner_calls[1].active);
    EXPECT_STREQ(owner_calls[2].topic, "added");
    EXPECT_TRUE(owner_calls[2].active);
    owner_calls.clear();
    mqtt_triggers_on_connected();
    ASSERT_EQ(owner_calls.size(), 2U);
    EXPECT_TRUE(owner_calls[0].active);
    EXPECT_TRUE(owner_calls[1].active);
    EXPECT_EQ(allocations, 4U);
}

TEST_F(MqttTriggers, MatchingFanoutCoalescesAndDispatchesOutsideMutex) {
    seed(R"({"triggers":[{"topic":"event","value":"on","actions":[{"type":"screen"}]},
                           {"topic":"event","actions":[{"type":"screen"}]}]})");
    mqtt_triggers_init();
    const uint8_t off[] = {'o', 'f', 'f'};
    mqtt_triggers_on_message("event", off, sizeof(off));
    mqtt_triggers_loop();
    EXPECT_EQ(dispatches, 1U);
    const uint8_t on[] = {'o', 'n'};
    mqtt_triggers_on_message("event", on, sizeof(on));
    mqtt_triggers_on_message("event", on, sizeof(on));
    EXPECT_EQ(dispatches, 1U);
    mqtt_triggers_loop();
    EXPECT_EQ(dispatches, 3U);
    mqtt_triggers_loop();
    EXPECT_EQ(dispatches, 3U);
}

TEST_F(MqttTriggers, TypedActionsRoundTripAndInvalidSavesKeepCacheAndFile) {
    mqtt_triggers_init();
    const char* valid = R"({"triggers":[{"topic":"event","actions":[{"op":"test.set","args":{"value":{"$template":"[var:x]"}}}]}]})";
    ASSERT_TRUE(save(valid));
    DynamicJsonDocument response(MQTT_TRIGGERS_JSON_CAP);
    ASSERT_TRUE(mqtt_triggers_to_json(response.to<JsonArray>()));
    ASSERT_STREQ(response[0]["actions"][0]["args"]["value"]["$template"], "[var:x]");
    const auto original = LittleFS.files[MQTT_TRIGGERS_PATH];
    EXPECT_FALSE(save(R"({"triggers":[{"topic":"bad/+","actions":[]}]})"));
    EXPECT_FALSE(save(R"({"triggers":[{"topic":"event","actions":[{"op":"unknown","args":{}}]}]})"));
    atomic_write_fails = true;
    EXPECT_FALSE(save(R"({"triggers":[]})"));
    EXPECT_EQ(LittleFS.files[MQTT_TRIGGERS_PATH], original);
    ASSERT_STREQ(response[0]["actions"][0]["args"]["value"]["$template"], "[var:x]");
    atomic_write_fails = false;
    ASSERT_TRUE(save(R"({"triggers":[{"topic":"changed"}]})"));
    EXPECT_STREQ(response[0]["topic"], "event");
    EXPECT_STREQ(response[0]["actions"][0]["args"]["value"]["$template"], "[var:x]");
    ASSERT_TRUE(save(valid));
    mqtt_triggers_on_message("event", nullptr, 0);
    mqtt_triggers_loop();
    EXPECT_EQ(dispatched_argument, "[var:x]");
}

TEST_F(MqttTriggers, SaveCancelsQueuedMatchesRatherThanRunningNewSlot) {
    mqtt_triggers_init();
    ASSERT_TRUE(save(R"({"triggers":[{"topic":"old","actions":[{"type":"screen"}]}]})"));
    mqtt_triggers_on_message("old", nullptr, 0);
    ASSERT_TRUE(save(R"({"triggers":[{"topic":"new","actions":[{"type":"screen"}]}]})"));
    mqtt_triggers_loop();
    EXPECT_EQ(dispatches, 0U);
}

TEST_F(MqttTriggers, SnapshotSurvivesReplacementAndRemainingMatchesAreCancelled) {
    mqtt_triggers_init();
    ASSERT_TRUE(save(R"({"triggers":[{"topic":"event","actions":[{"op":"test.set","args":{"value":"original"}}]},{"topic":"event","actions":[{"type":"screen"}]}]})"));
    mqtt_triggers_on_message("event", nullptr, 0);
    replacement_on_dispatch = R"({"triggers":[{"topic":"new","actions":[{"type":"screen"}]}]})";
    mqtt_triggers_loop();
    EXPECT_EQ(dispatched_argument, "original");
    EXPECT_EQ(dispatches, 1U);
}

TEST_F(MqttTriggers, TypedActionsReloadAndDispatchOnMainLoop) {
    mqtt_triggers_init();
    ASSERT_TRUE(save(R"({"triggers":[{"topic":"event","actions":[{"op":"test.set","args":{"value":"reloaded"}}]}]})"));
    mqtt_triggers_init();
    mqtt_triggers_on_message("event", nullptr, 0);
    EXPECT_EQ(dispatches, 0U);
    mqtt_triggers_loop();
    EXPECT_EQ(dispatched_argument, "reloaded");
    EXPECT_EQ(dispatches, 1U);
    EXPECT_EQ(dispatched_owner, ACTION_CONTINUATION_OWNER_LOOP);
    EXPECT_FALSE(dispatched_interactive);
}

TEST_F(MqttTriggers, HardwareTypedSaveReloadAndDispatchUsesOwnedSnapshots) {
    mqtt_triggers_init();
    hw_host::hw_button_config_init();
    const char* config = R"({"buttons":[{"tap_actions":[{"op":"test.set","args":{"value":{"$template":"[var:tap]"}}}],"hold_actions":[{"op":"test.set","args":{"value":"hold"}}]}]})";
    ASSERT_TRUE(hw_host::hw_button_config_save_raw(reinterpret_cast<const uint8_t*>(config), strlen(config)));
    hw_host::hw_button_config_init();
    DynamicJsonDocument response(4096);
    ASSERT_TRUE(hw_host::hw_button_config_to_json(response.to<JsonArray>()));
    ASSERT_STREQ(response[0]["tap_actions"][0]["args"]["value"]["$template"], "[var:tap]");
    ASSERT_STREQ(response[0]["hold_actions"][0]["args"]["value"], "hold");

    hw_host::hw_buttons_init();
    pin_levels[1] = LOW;
    hw_host::hw_button_isr();
    hw_host::hw_buttons_loop();
    clock_ms += 21;
    hw_host::hw_buttons_loop();
    pin_levels[1] = HIGH;
    hw_host::hw_buttons_loop();
    clock_ms += 21;
    hw_replacement_on_dispatch = R"({"buttons":[{"tap_actions":[]}]})";
    hw_host::hw_buttons_loop();
    EXPECT_EQ(dispatched_argument, "[var:tap]");
    EXPECT_EQ(dispatches, 1U);
    EXPECT_EQ(dispatched_owner, ACTION_CONTINUATION_OWNER_LOOP);
    EXPECT_TRUE(dispatched_interactive);
    EXPECT_STREQ(response[0]["tap_actions"][0]["args"]["value"]["$template"], "[var:tap]");

    ASSERT_TRUE(hw_host::hw_button_config_save_raw(reinterpret_cast<const uint8_t*>(config), strlen(config)));
    pin_levels[1] = LOW;
    hw_host::hw_button_isr();
    hw_host::hw_buttons_loop();
    clock_ms += 21;
    hw_host::hw_buttons_loop();
    clock_ms += HW_BUTTON_HOLD_MS;
    hw_host::hw_buttons_loop();
    EXPECT_EQ(dispatched_argument, "hold");
    EXPECT_EQ(dispatches, 2U);
}

TEST_F(MqttTriggers, HardwareInvalidOrFailedWritePreservesFileAndCache) {
    mqtt_triggers_init();
    hw_host::hw_button_config_init();
    auto save_hw = [](const char* json) {
        return hw_host::hw_button_config_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json));
    };
    ASSERT_TRUE(save_hw(R"({"buttons":[{"tap_actions":[{"op":"test.set","args":{"value":"kept"}}]}]})"));
    const auto original = LittleFS.files[hw_host::HW_BUTTONS_PATH];
    EXPECT_FALSE(save_hw(R"({"buttons":[{"tap_actions":[{"op":"unknown","args":{}}]}]})"));
    EXPECT_FALSE(save_hw(R"({"buttons":[{"tap_actions":[{"op":"test.set","args":{"value":3}}]}]})"));
    atomic_write_fails = true;
    EXPECT_FALSE(save_hw(R"({"buttons":[]})"));
    EXPECT_EQ(LittleFS.files[hw_host::HW_BUTTONS_PATH], original);
    DynamicJsonDocument response(4096);
    ASSERT_TRUE(hw_host::hw_button_config_to_json(response.to<JsonArray>()));
    EXPECT_STREQ(response[0]["tap_actions"][0]["args"]["value"], "kept");
    EXPECT_STREQ(response[0]["hold_actions"][0]["type"], "screen");
}
