// Unit tests for the fixed action continuation pool.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "action_continuation.h"
#include "action_dispatch.h"
#include "action_list.h"
#include "action_registry.h"
#include <string>

#define MQTT_MANAGER_H
class MqttManager {
public:
    bool accepted = true;
    bool retained = true;
    unsigned calls = 0;
    std::string topic;
    std::string payload;
    bool publish(const char* new_topic, const char* new_payload, bool new_retained) {
        ++calls;
        topic = new_topic;
        payload = new_payload;
        retained = new_retained;
        return accepted;
    }
};
static MqttManager mqtt_manager;
void action_type_register(const ActionTypeDef*) {}
#define ARDUINO 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "../src/app/actions/mqtt_action.cpp"
#pragma GCC diagnostic pop
#undef ARDUINO

#if HAS_PSRAM
#include <esp_heap_caps.h>

static bool s_fail_allocation = false;
static unsigned s_live_allocations = 0;
void* heap_caps_malloc(size_t size, uint32_t caps) {
    if (s_fail_allocation || caps != (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return nullptr;
    void* memory = std::malloc(size);
    if (memory) ++s_live_allocations;
    return memory;
}
void heap_caps_free(void* memory) {
    if (memory) --s_live_allocations;
    std::free(memory);
}
#endif

static unsigned long s_millis = 0;
extern "C" unsigned long millis() { return s_millis; }

static int g_pass = 0;
static int g_fail = 0;
static uint32_t g_pending_token = 0;
static char g_dispatched[4][CONFIG_ACTION_TYPE_MAX_LEN] = {};
static uint8_t g_dispatched_count = 0;
static bool g_interactive[4] = {};
static char g_notification[CONFIG_DIALOG_MESSAGE_MAX_LEN] = {};
static ActionResolvedArgument g_mqtt_arguments[] = {{"home/room", 0, 0}, {"on", 0, 0}};

#define TEST(name) static void test_##name()
#define RUN(name) do { test_##name(); g_pass++; } while (0)
#define ASSERT_TRUE(value) do { \
    if (!(value)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
        g_fail++; return; } \
} while (0)
#define ASSERT_EQ(actual, expected) do { \
    if ((actual) != (expected)) { std::printf("FAIL %s:%d\n", __FILE__, __LINE__); \
        g_fail++; return; } \
} while (0)

void action_parse(const JsonObject&, ButtonAction&) {}

const ActionOperationDef* action_operation_for_action(const ButtonAction&, const ActionTypeDef** owner) {
    if (owner) *owner = nullptr;
    return nullptr;
}

bool action_may_pause(const ButtonAction& action) {
    return !strcmp(action.type, "pending");
}

ActionResult action_dispatch(const ButtonAction& action, const char*,
                             uint32_t continuation_token, bool interactive) {
    g_interactive[g_dispatched_count] = interactive;
    strlcpy(g_dispatched[g_dispatched_count++], action.type,
            sizeof(g_dispatched[0]));
    if (!strcmp(action.type, ACTION_TYPE_NOTIFY))
        strlcpy(g_notification, action.payload.notify.message, sizeof(g_notification));
    if (!strcmp(action.type, "mqtt_op_test"))
        return kMqttOperations[0].dispatch(g_mqtt_arguments, "Test", continuation_token);
    if (!strcmp(action.type, "failed")) return ACTION_FAILED;
    if (strcmp(action.type, "pending") == 0) {
        g_pending_token = continuation_token;
        return continuation_token ? ACTION_PENDING : ACTION_FAILED;
    }
    return ACTION_COMPLETE;
}

static void reset_dispatch() {
    g_pending_token = 0;
    g_dispatched_count = 0;
    memset(g_dispatched, 0, sizeof(g_dispatched));
}

static uint32_t begin_with_suffix(const char* suffix_type) {
    ButtonAction suffix[1] = {};
    strlcpy(suffix[0].type, suffix_type, sizeof(suffix[0].type));
    uint32_t token = 0;
    if (!action_continuation_begin(suffix, 1, "Test", &token)) return 0;
    return token;
}

TEST(mqtt_publish_rejection_stops_suffix) {
    ButtonAction actions[2] = {};
    strlcpy(actions[0].type, "mqtt_op_test", sizeof(actions[0].type));
    strlcpy(actions[1].type, ACTION_TYPE_SCREEN, sizeof(actions[1].type));
    mqtt_manager.accepted = true;
    mqtt_manager.calls = 0;
    reset_dispatch();
    ASSERT_EQ(action_list_dispatch_result(actions, 2, "Test"), ACTION_COMPLETE);
    ASSERT_EQ(g_dispatched_count, 2);
    ASSERT_EQ(mqtt_manager.calls, 1);
    ASSERT_TRUE(mqtt_manager.topic == "home/room" && mqtt_manager.payload == "on");
    ASSERT_TRUE(!mqtt_manager.retained);

    g_mqtt_arguments[1].text = "";
    ASSERT_EQ(kMqttOperations[0].dispatch(g_mqtt_arguments, "Test", 0), ACTION_COMPLETE);
    ASSERT_TRUE(mqtt_manager.payload.empty());
    g_mqtt_arguments[1].text = "on";
    g_mqtt_arguments[0].text = "";
    ASSERT_EQ(kMqttOperations[0].dispatch(g_mqtt_arguments, "Test", 0), ACTION_FAILED);
    ASSERT_EQ(mqtt_manager.calls, 2);
    g_mqtt_arguments[0].text = "home/room";

    mqtt_manager.accepted = false;
    reset_dispatch();
    ASSERT_EQ(action_list_dispatch_result(actions, 2, "Test"), ACTION_FAILED);
    ASSERT_EQ(g_dispatched_count, 1);
    ASSERT_EQ(mqtt_manager.calls, 3);

}

TEST(success_copies_saved_suffix) {
    const uint32_t token = begin_with_suffix(ACTION_TYPE_MQTT);
    ASSERT_TRUE(token != 0);
    ASSERT_TRUE(action_continuation_complete(token, true));
    action_continuation_mark_pending(token);

    ButtonAction out[MAX_BUTTON_ACTIONS - 1] = {};
    uint8_t count = 0;
    char label[ACTION_CONTINUATION_LABEL_MAX_LEN];
    ASSERT_EQ(action_continuation_take(out, &count, label, sizeof(label)),
              ACTION_CONTINUATION_SUCCESS);
    ASSERT_EQ(count, 1);
    ASSERT_TRUE(strcmp(out[0].type, ACTION_TYPE_MQTT) == 0);
    ASSERT_TRUE(strcmp(label, "Test") == 0);
}

TEST(failure_discards_suffix) {
    const uint32_t token = begin_with_suffix(ACTION_TYPE_SCREEN);
    ASSERT_TRUE(token != 0);
    action_continuation_mark_pending(token);
    ASSERT_TRUE(action_continuation_is_pending(token));
    ASSERT_TRUE(action_continuation_complete(token, false));
    ASSERT_TRUE(!action_continuation_is_pending(token));
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0),
              ACTION_CONTINUATION_FAILED);
}

TEST(timeout_discards_suffix) {
    s_millis = 100;
    const uint32_t token = begin_with_suffix(ACTION_TYPE_NOTIFY);
    ASSERT_TRUE(token != 0);
    action_continuation_mark_pending(token);
    s_millis += ACTION_CONTINUATION_TIMEOUT_MS;
    ASSERT_TRUE(!action_continuation_is_pending(token));
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0),
              ACTION_CONTINUATION_TIMED_OUT);
}

TEST(late_completion_cannot_override_timeout) {
    s_millis = 100;
    const uint32_t token = begin_with_suffix(ACTION_TYPE_NOTIFY);
    ASSERT_TRUE(token != 0);
    action_continuation_mark_pending(token);
    s_millis += ACTION_CONTINUATION_TIMEOUT_MS;
    ASSERT_TRUE(!action_continuation_complete(token, true));
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0),
              ACTION_CONTINUATION_TIMED_OUT);
}

TEST(captured_context_released_on_terminal_outcomes) {
    int references = 1;
    ActionContinuationContext context = {
        &references,
        [](void* value) { ++*static_cast<int*>(value); },
        [](void* value) { --*static_cast<int*>(value); }
    };
    for (int outcome = 0; outcome < 3; ++outcome) {
        s_millis = 0;
        uint32_t token = 0;
        ASSERT_TRUE(action_continuation_begin(nullptr, 0, "Test", &token,
                                              ACTION_CONTINUATION_OWNER_LOOP, false, context));
        ASSERT_EQ(references, 2);
        action_continuation_mark_pending(token);
        if (outcome == 0) {
            action_continuation_release(token);
        } else {
            if (outcome == 1) ASSERT_TRUE(action_continuation_complete(token, false));
            else s_millis += ACTION_CONTINUATION_TIMEOUT_MS;
            ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0),
                      outcome == 1 ? ACTION_CONTINUATION_FAILED : ACTION_CONTINUATION_TIMED_OUT);
        }
        ASSERT_EQ(references, 1);
        ASSERT_TRUE(!action_continuation_complete(token, true));
    }
}

TEST(scheduled_success_waits_for_due_time) {
    s_millis = 100;
    const uint32_t token = begin_with_suffix(ACTION_TYPE_MQTT);
    ASSERT_TRUE(token != 0);
    action_continuation_mark_pending(token);
    ASSERT_TRUE(action_continuation_schedule_success(token, 500));
    s_millis = 599;
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0),
              ACTION_CONTINUATION_NONE);
    s_millis = 600;
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0),
              ACTION_CONTINUATION_SUCCESS);
}

TEST(completed_suffix_stays_with_originating_owner) {
    const uint32_t token = begin_with_suffix(ACTION_TYPE_MQTT);
    ASSERT_TRUE(token != 0);
    action_continuation_mark_pending(token);
    ASSERT_TRUE(action_continuation_complete(token, true));
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0,
                                       ACTION_CONTINUATION_OWNER_LVGL),
              ACTION_CONTINUATION_NONE);
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0),
              ACTION_CONTINUATION_SUCCESS);
}

TEST(pool_accepts_capacity_and_rejects_overflow) {
    ButtonAction suffix[1] = {};
    uint32_t tokens[ACTION_CONTINUATION_SLOTS] = {};
    for (uint8_t index = 0; index < ACTION_CONTINUATION_SLOTS; ++index) {
        ASSERT_TRUE(action_continuation_begin(suffix, 1, "Pool", &tokens[index]));
    }
    uint32_t overflow_token = 0;
    ASSERT_TRUE(!action_continuation_begin(suffix, 1, "Overflow", &overflow_token));
    ASSERT_TRUE(action_continuation_is_full());
    action_continuation_release(tokens[0]);
    ASSERT_TRUE(!action_continuation_is_full());
    for (uint8_t index = 1; index < ACTION_CONTINUATION_SLOTS; ++index) {
        action_continuation_release(tokens[index]);
    }
}

TEST(ready_slot_bypasses_earlier_unfinished_slot) {
    ButtonAction first_suffix[1] = {};
    ButtonAction second_suffix[1] = {};
    strlcpy(first_suffix[0].type, ACTION_TYPE_KEY, sizeof(first_suffix[0].type));
    strlcpy(second_suffix[0].type, ACTION_TYPE_MQTT, sizeof(second_suffix[0].type));
    uint32_t first_token = 0;
    uint32_t second_token = 0;
    ASSERT_TRUE(action_continuation_begin(first_suffix, 1, "First", &first_token));
    ASSERT_TRUE(action_continuation_begin(second_suffix, 1, "Second", &second_token));
    action_continuation_mark_pending(first_token);
    action_continuation_mark_pending(second_token);
    ASSERT_TRUE(action_continuation_complete(second_token, true));

    ButtonAction out[MAX_BUTTON_ACTIONS - 1] = {};
    uint8_t count = 0;
    char label[ACTION_CONTINUATION_LABEL_MAX_LEN];
    ASSERT_EQ(action_continuation_take(out, &count, label, sizeof(label)),
              ACTION_CONTINUATION_SUCCESS);
    ASSERT_EQ(count, 1);
    ASSERT_TRUE(strcmp(label, "Second") == 0);
    ASSERT_TRUE(strcmp(out[0].type, ACTION_TYPE_MQTT) == 0);
    action_continuation_release(first_token);
    ASSERT_TRUE(!action_continuation_complete(first_token, true));
}

TEST(pending_action_pauses_and_resumes_copied_suffix) {
    reset_dispatch();
    ButtonAction actions[3] = {};
    strlcpy(actions[0].type, "pending", sizeof(actions[0].type));
    strlcpy(actions[1].type, ACTION_TYPE_MQTT, sizeof(actions[1].type));
    strlcpy(actions[2].type, ACTION_TYPE_NOTIFY, sizeof(actions[2].type));
    strlcpy(actions[2].payload.notify.message, "Brightness set to 20%", sizeof(actions[2].payload.notify.message));

    action_list_dispatch(actions, 3, "Test");
    ASSERT_EQ(g_dispatched_count, 1);
    ASSERT_TRUE(g_pending_token != 0);

    // The continuation must not observe mutable source storage after dispatch.
    strlcpy(actions[1].type, ACTION_TYPE_KEY, sizeof(actions[1].type));
    strlcpy(actions[2].payload.notify.message, "Brightness set to 80%", sizeof(actions[2].payload.notify.message));
    ASSERT_TRUE(action_continuation_complete(g_pending_token, true));
    action_list_dispatch_continuation();

    ASSERT_EQ(g_dispatched_count, 3);
    ASSERT_TRUE(strcmp(g_dispatched[1], ACTION_TYPE_MQTT) == 0);
    ASSERT_TRUE(strcmp(g_dispatched[2], ACTION_TYPE_NOTIFY) == 0);
    ASSERT_TRUE(strcmp(g_notification, "Brightness set to 20%") == 0);
}

TEST(failed_pending_action_does_not_resume_suffix) {
    reset_dispatch();
    ButtonAction actions[2] = {};
    strlcpy(actions[0].type, "pending", sizeof(actions[0].type));
    strlcpy(actions[1].type, ACTION_TYPE_MQTT, sizeof(actions[1].type));

    action_list_dispatch(actions, 2, "Test");
    ASSERT_TRUE(action_continuation_complete(g_pending_token, false));
    action_list_dispatch_continuation();
    ASSERT_EQ(g_dispatched_count, 1);
}

TEST(failure_before_confirmation_stops_entire_suffix) {
    reset_dispatch();
    ButtonAction actions[3]{};
    strcpy(actions[0].type, "failed");
    strcpy(actions[1].type, ACTION_TYPE_CONFIRM);
    strcpy(actions[2].type, ACTION_TYPE_MQTT);
    action_list_dispatch(actions, 3, "Failed binding");
    action_list_dispatch_continuation();
    ASSERT_EQ(g_dispatched_count, 1);
    ASSERT_TRUE(!action_continuation_is_full());
}

TEST(explicit_input_wait_and_completion_are_single_use) {
    s_millis = 0;
    uint32_t token = begin_with_suffix(ACTION_TYPE_MQTT);
    ASSERT_TRUE(action_continuation_set_timeout(token, 0));
    action_continuation_mark_pending(token);
    s_millis = 301000;
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0), ACTION_CONTINUATION_NONE);
    ASSERT_TRUE(action_continuation_complete(token, true));
    ASSERT_TRUE(!action_continuation_complete(token, false));
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0), ACTION_CONTINUATION_SUCCESS);
    ASSERT_TRUE(!action_continuation_complete(token, true));
    token = begin_with_suffix(ACTION_TYPE_MQTT);
    action_continuation_mark_pending(token);
    ASSERT_TRUE(!action_continuation_set_timeout(token, 0));
    s_millis += ACTION_CONTINUATION_TIMEOUT_MS;
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0), ACTION_CONTINUATION_TIMED_OUT);
}

TEST(dispatch_reports_initial_result_without_changing_suffix_policy) {
    reset_dispatch();
    ButtonAction actions[2] = {};
    strlcpy(actions[0].type, "failed", sizeof(actions[0].type));
    strlcpy(actions[1].type, "next", sizeof(actions[1].type));
    ASSERT_EQ(action_list_dispatch_result(actions, 2, "Test"), ACTION_FAILED);
    ASSERT_EQ(g_dispatched_count, 1);
    ASSERT_EQ(action_list_dispatch_result(nullptr, 0, "Test"), ACTION_COMPLETE);
    reset_dispatch();
    strlcpy(actions[0].type, "pending", sizeof(actions[0].type));
    ASSERT_EQ(action_list_dispatch_result(actions, 2, "Test"), ACTION_PENDING);
    ASSERT_EQ(g_dispatched_count, 1);
    ASSERT_TRUE(action_continuation_complete(g_pending_token, false));
    action_list_dispatch_continuation();
}

TEST(interaction_origin_survives_pause_but_not_next_background_list) {
    reset_dispatch();
    ButtonAction actions[2] = {};
    strcpy(actions[0].type, "pending");
    strcpy(actions[1].type, "next");
    action_list_dispatch(actions, 2, "User", ACTION_CONTINUATION_OWNER_LVGL, true);
    ASSERT_TRUE(g_interactive[0]);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    ASSERT_EQ(g_dispatched_count, 1);
    ASSERT_TRUE(action_continuation_complete(g_pending_token, true));
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LOOP);
    ASSERT_EQ(g_dispatched_count, 1);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    ASSERT_EQ(g_dispatched_count, 2);
    ASSERT_TRUE(g_interactive[1]);
    action_list_dispatch(actions + 1, 1, "Background");
    ASSERT_TRUE(!g_interactive[2]);
}

TEST(reservations_block_shutdown_until_consumed) {
    ASSERT_TRUE(!action_continuation_is_active());
    uint32_t token = 0;
    ASSERT_TRUE(action_continuation_begin(nullptr, 0, "Shutdown", &token));
    ASSERT_TRUE(action_continuation_is_active());
    ASSERT_TRUE(!action_continuation_is_pending(token));
    action_continuation_mark_pending(token);
    ASSERT_TRUE(action_continuation_is_pending(token));
    ASSERT_TRUE(action_continuation_complete(token, false));
    ASSERT_TRUE(action_continuation_is_active());
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0), ACTION_CONTINUATION_FAILED);
    ASSERT_TRUE(!action_continuation_is_active());
    ASSERT_TRUE(!action_continuation_is_pending(token));
}

#if HAS_PSRAM
TEST(allocation_failure_does_not_reserve_or_spill) {
    ButtonAction suffix[1] = {};
    uint32_t token = 1;
    s_fail_allocation = true;
    const bool reserved = action_continuation_begin(suffix, 1, "Allocation", &token);
    s_fail_allocation = false;
    ASSERT_TRUE(!reserved && token == 0);
    ASSERT_TRUE(!action_continuation_is_active());
    ASSERT_EQ(s_live_allocations, 0);

    reset_dispatch();
    ButtonAction actions[2] = {};
    strcpy(actions[0].type, "pending");
    strcpy(actions[1].type, "next");
    s_fail_allocation = true;
    const ActionResult result = action_list_dispatch_result(actions, 2, "Allocation");
    s_fail_allocation = false;
    ASSERT_EQ(result, ACTION_FAILED);
    ASSERT_EQ(g_dispatched_count, 0);
    ASSERT_TRUE(!g_pending_token && !action_continuation_is_active());
}
#endif

int main() {
    RUN(mqtt_publish_rejection_stops_suffix);
    RUN(reservations_block_shutdown_until_consumed);
    RUN(success_copies_saved_suffix);
    RUN(failure_discards_suffix);
    RUN(timeout_discards_suffix);
    RUN(late_completion_cannot_override_timeout);
    RUN(captured_context_released_on_terminal_outcomes);
    RUN(scheduled_success_waits_for_due_time);
    RUN(completed_suffix_stays_with_originating_owner);
    RUN(pool_accepts_capacity_and_rejects_overflow);
    RUN(ready_slot_bypasses_earlier_unfinished_slot);
    RUN(pending_action_pauses_and_resumes_copied_suffix);
    RUN(failed_pending_action_does_not_resume_suffix);
    RUN(failure_before_confirmation_stops_entire_suffix);
    RUN(explicit_input_wait_and_completion_are_single_use);
    RUN(dispatch_reports_initial_result_without_changing_suffix_policy);
    RUN(interaction_origin_survives_pause_but_not_next_background_list);
#if HAS_PSRAM
    RUN(allocation_failure_does_not_reserve_or_spill);
    if (s_live_allocations != 0) {
        std::puts("FAIL: continuation snapshots leaked");
        ++g_fail;
    }
#endif
    std::printf("action continuation: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}