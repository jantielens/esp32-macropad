// Unit tests for the fixed action continuation slot.

#include <cstdio>
#include <cstring>

#include "action_continuation.h"
#include "action_dispatch.h"
#include "action_list.h"

static unsigned long s_millis = 0;
extern "C" unsigned long millis() { return s_millis; }

static int g_pass = 0;
static int g_fail = 0;
static uint32_t g_pending_token = 0;
static char g_dispatched[4][CONFIG_ACTION_TYPE_MAX_LEN] = {};
static uint8_t g_dispatched_count = 0;

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

ActionResult action_dispatch(const ButtonAction& action, const char*,
                             uint32_t continuation_token) {
    strlcpy(g_dispatched[g_dispatched_count++], action.type,
            sizeof(g_dispatched[0]));
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
    ASSERT_TRUE(action_continuation_complete(token, false));
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0),
              ACTION_CONTINUATION_FAILED);
}

TEST(timeout_discards_suffix) {
    s_millis = 100;
    const uint32_t token = begin_with_suffix(ACTION_TYPE_NOTIFY);
    ASSERT_TRUE(token != 0);
    action_continuation_mark_pending(token);
    s_millis += ACTION_CONTINUATION_TIMEOUT_MS;
    ASSERT_EQ(action_continuation_take(nullptr, nullptr, nullptr, 0),
              ACTION_CONTINUATION_TIMED_OUT);
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

TEST(active_slot_rejects_second_and_stale_token) {
    const uint32_t token = begin_with_suffix(ACTION_TYPE_KEY);
    ASSERT_TRUE(token != 0);
    ButtonAction suffix[1] = {};
    uint32_t second_token = 0;
    ASSERT_TRUE(!action_continuation_begin(suffix, 1, "Second", &second_token));
    action_continuation_release(token);
    ASSERT_TRUE(!action_continuation_complete(token, true));
}

TEST(pending_action_pauses_and_resumes_copied_suffix) {
    reset_dispatch();
    ButtonAction actions[3] = {};
    strlcpy(actions[0].type, "pending", sizeof(actions[0].type));
    strlcpy(actions[1].type, ACTION_TYPE_MQTT, sizeof(actions[1].type));
    strlcpy(actions[2].type, ACTION_TYPE_NOTIFY, sizeof(actions[2].type));

    action_list_dispatch(actions, 3, "Test");
    ASSERT_EQ(g_dispatched_count, 1);
    ASSERT_TRUE(g_pending_token != 0);

    // The continuation must not observe mutable source storage after dispatch.
    strlcpy(actions[1].type, ACTION_TYPE_KEY, sizeof(actions[1].type));
    ASSERT_TRUE(action_continuation_complete(g_pending_token, true));
    action_list_dispatch_continuation();

    ASSERT_EQ(g_dispatched_count, 3);
    ASSERT_TRUE(strcmp(g_dispatched[1], ACTION_TYPE_MQTT) == 0);
    ASSERT_TRUE(strcmp(g_dispatched[2], ACTION_TYPE_NOTIFY) == 0);
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

int main() {
    RUN(success_copies_saved_suffix);
    RUN(failure_discards_suffix);
    RUN(timeout_discards_suffix);
    RUN(scheduled_success_waits_for_due_time);
    RUN(completed_suffix_stays_with_originating_owner);
    RUN(active_slot_rejects_second_and_stale_token);
    RUN(pending_action_pauses_and_resumes_copied_suffix);
    RUN(failed_pending_action_does_not_resume_suffix);
    std::printf("action continuation: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}