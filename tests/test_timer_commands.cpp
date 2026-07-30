#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits.h>
#include <thread>
#include <freertos/semphr.h>
#include <ArduinoJson.h>

#include "timer_command.h"
#include "timer_config.h"
#include "timer_mcp_adapter.h"

static unsigned long g_now = 0;
extern "C" unsigned long millis() { return g_now; }

static int g_dispatched = 0;
static bool g_restart_on_first = false;
static bool g_snapshot_ok = false;
static TimerExpirySnapshot g_snapshot = {};

void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char*) {
    for (uint8_t index = 0; index < count; index++) {
        (void)actions[index];
        g_dispatched++;
        if (g_restart_on_first && index == 0) {
            timer_configure_and_start(1, TIMER_MODE_UP, 0, nullptr, 0);
        }
    }
}

bool timer_config_snapshot_expiry(uint8_t, TimerExpirySnapshot* out) {
    if (!g_snapshot_ok || !out) return false;
    *out = g_snapshot;
    return true;
}

static TimerPayload payload(uint8_t id, const char* command,
                            const char* mode = "", const char* value = "") {
    TimerPayload result = {};
    result.timer_id = id;
    strlcpy(result.timer_command, command, sizeof(result.timer_command));
    strlcpy(result.timer_mode, mode, sizeof(result.timer_mode));
    strlcpy(result.timer_value, value, sizeof(result.timer_value));
    return result;
}

static PreparedTimerCommand prepare_ok(const TimerPayload& input) {
    PreparedTimerCommand command = {};
    char error[96] = {};
    assert(timer_command_prepare(input, &command, error, sizeof(error)));
    assert(error[0] == '\0');
    return command;
}

static void prepare_fails(const TimerPayload& input) {
    PreparedTimerCommand command = {};
    char error[96] = {};
    assert(!timer_command_prepare(input, &command, error, sizeof(error)));
    assert(error[0] != '\0');
}

static void execute_ok(const PreparedTimerCommand& command,
                       const TimerExpirySnapshot* snapshot = nullptr) {
    char error[96] = {};
    assert(timer_command_execute(command, snapshot, error, sizeof(error)));
}

static void test_validation_contract() {
    timer_engine_init();
    prepare_fails(payload(0, "start", "up"));
    prepare_fails(payload(4, "start", "up"));
    prepare_fails(payload(1, "lap"));
    prepare_fails(payload(1, "start"));
    prepare_fails(payload(1, "toggle", "sideways"));
    prepare_fails(payload(1, "start", "up", "1"));

    const char* invalid_down[] = {
        "", "0", "-1", "+1", "1.5", "1x", "NaN", "inf", "[mqtt:x]",
        "4294968"
    };
    for (const char* value : invalid_down) {
        prepare_fails(payload(1, "start", "down", value));
        prepare_fails(payload(1, "toggle", "down", value));
    }
    assert(prepare_ok(payload(1, "start", "down", "4294967")).value_ms
           == 4294967000U);

    prepare_fails(payload(1, "set", "down", "1"));
    prepare_fails(payload(1, "adjust", "up", "1"));
    prepare_fails(payload(1, "stop", "", "1"));
    prepare_fails(payload(1, "pause", "up"));
    prepare_fails(payload(1, "resume", "", "1"));
    prepare_fails(payload(1, "reset", "down"));

    timer_configure_and_start(1, TIMER_MODE_DOWN, 10000, nullptr, 0);
    assert(prepare_ok(payload(1, "set", "", "0")).value_ms == 0);
    assert(prepare_ok(payload(1, "adjust", "", "-2147483648")).delta_seconds
           == INT32_MIN);
    assert(prepare_ok(payload(1, "adjust", "", "2147483647")).delta_seconds
           == INT32_MAX);
    prepare_fails(payload(1, "set", "", "-1"));
    prepare_fails(payload(1, "set", "", "1.0"));
    prepare_fails(payload(1, "adjust", "", "2147483648"));
    prepare_fails(payload(1, "adjust", "", "--1"));

    const char* controls[] = {"stop", "pause", "resume", "reset"};
    for (const char* control : controls) prepare_ok(payload(1, control));
}

static void test_start_and_toggle_states() {
    timer_engine_init();
    g_now = 10;
    PreparedTimerCommand start = prepare_ok(payload(1, "start", "down", "30"));
    TimerExpirySnapshot snapshot = {};
    snapshot.count = 1;
    strlcpy(snapshot.actions[0].type, ACTION_TYPE_BEEP,
            sizeof(snapshot.actions[0].type));
    execute_ok(start, &snapshot);
    assert(timer_get_state(1) == TIMER_RUNNING);
    assert(timer_get_mode(1) == TIMER_MODE_DOWN);
    assert(timer_get_ms(1) == 30000);

    g_now = 5010;
    PreparedTimerCommand restart = prepare_ok(payload(1, "start", "up"));
    execute_ok(restart);
    assert(timer_get_mode(1) == TIMER_MODE_UP);
    assert(timer_get_ms(1) == 0);

    PreparedTimerCommand toggle = prepare_ok(payload(1, "toggle", "down", "9"));
    execute_ok(toggle);
    assert(timer_get_state(1) == TIMER_PAUSED);
    assert(timer_get_mode(1) == TIMER_MODE_UP);

    PreparedTimerCommand stale = prepare_ok(payload(1, "toggle", "down", "9"));
    timer_resume(1);
    char error[96] = {};
    assert(!timer_command_execute(stale, nullptr, error, sizeof(error)));
    assert(timer_get_state(1) == TIMER_RUNNING);

    PreparedTimerCommand pause = prepare_ok(payload(1, "toggle", "down", "9"));
    execute_ok(pause);
    PreparedTimerCommand resume = prepare_ok(payload(1, "toggle", "up"));
    execute_ok(resume);
    assert(timer_get_state(1) == TIMER_RUNNING);
    assert(timer_get_mode(1) == TIMER_MODE_UP);

    timer_stop(1);
    PreparedTimerCommand stopped = prepare_ok(payload(1, "toggle", "down", "7"));
    assert(stopped.needs_expiry_snapshot);
    execute_ok(stopped, &snapshot);
    assert(timer_get_state(1) == TIMER_RUNNING);
    assert(timer_get_mode(1) == TIMER_MODE_DOWN);
    assert(timer_get_ms(1) == 7000);
}

static void test_set_adjust_and_expiry() {
    timer_engine_init();
    g_now = 0;
    timer_configure_and_start(1, TIMER_MODE_UP, 0, nullptr, 0);
    prepare_fails(payload(1, "set", "", "5"));
    prepare_fails(payload(1, "adjust", "", "5"));
    assert(timer_get_mode(1) == TIMER_MODE_UP);
    assert(timer_get_state(1) == TIMER_RUNNING);

    TimerExpirySnapshot snapshot = {};
    snapshot.count = 3;
    for (uint8_t index = 0; index < snapshot.count; index++) {
        strlcpy(snapshot.actions[index].type, ACTION_TYPE_BEEP,
                sizeof(snapshot.actions[index].type));
    }
    execute_ok(prepare_ok(payload(1, "start", "down", "1")), &snapshot);
    memset(&snapshot, 0, sizeof(snapshot));
    g_dispatched = 0;
    g_restart_on_first = true;
    g_now = 1000;
    timer_engine_tick();
    assert(g_dispatched == 3);
    assert(timer_get_mode(1) == TIMER_MODE_UP);
    timer_engine_tick();
    assert(g_dispatched == 3);
    g_restart_on_first = false;

    execute_ok(prepare_ok(payload(1, "start", "down", "1")), &snapshot);
    g_now = 2000;
    timer_engine_tick();
    assert(timer_is_expired(1));
    execute_ok(prepare_ok(payload(1, "set", "", "5")));
    assert(!timer_is_expired(1));
    g_now = 6000;
    timer_engine_tick();
    assert(timer_is_expired(1));

    timer_stop(1);
    execute_ok(prepare_ok(payload(1, "start", "down", "1")), &snapshot);
    execute_ok(prepare_ok(payload(1, "adjust", "", "2147483647")));
    assert(timer_get_ms(1) == UINT32_MAX);
    execute_ok(prepare_ok(payload(1, "adjust", "", "-2147483648")));
    assert(timer_get_ms(1) == 0);
}

static void test_countdown_target_lifecycle() {
    timer_engine_init();
    g_now = 0;
    assert(timer_get_target_seconds(1) == 0);

    assert(timer_configure_and_start(1, TIMER_MODE_DOWN, 120000, nullptr, 0));
    assert(timer_get_target_seconds(1) == 120);
    g_now = 45000;
    assert(timer_get_target_seconds(1) == 120);

    assert(timer_set_countdown_ms(1, 90000));
    assert(timer_get_target_seconds(1) == 90);
    assert(timer_adjust(1, 15));
    assert(timer_get_target_seconds(1) == 105);

    timer_stop(1);
    assert(timer_get_target_seconds(1) == 105);
    timer_reset(1);
    assert(timer_get_target_seconds(1) == 105);

    assert(timer_configure_and_start(1, TIMER_MODE_UP, 0, nullptr, 0));
    assert(timer_get_target_seconds(1) == 0);
    assert(timer_get_target_seconds(4) == 0);
}

static void test_basic_control_outcomes() {
    timer_engine_init();
    g_now = 0;

    assert(!timer_stop(0));
    assert(!timer_pause(4));
    assert(!timer_resume(0));
    assert(!timer_reset(4));

    // Valid state no-ops are accepted by a ready engine.
    assert(timer_stop(1));
    assert(timer_pause(1));
    assert(timer_resume(1));
    assert(timer_reset(1));
    assert(timer_get_state(1) == TIMER_STOPPED);

    assert(timer_configure_and_start(1, TIMER_MODE_UP, 0, nullptr, 0));
    g_now = 100;
    assert(timer_pause(1));
    assert(timer_get_state(1) == TIMER_PAUSED);
    assert(timer_resume(1));
    assert(timer_get_state(1) == TIMER_RUNNING);
    assert(timer_reset(1));
    assert(timer_get_ms(1) == 0);
    assert(timer_stop(1));
    assert(timer_get_state(1) == TIMER_STOPPED);
}

static void test_run_entry_point() {
    timer_engine_init();
    char error[96] = {};
    assert(timer_command_run(payload(1, "start", "up"), error, sizeof(error)));
    assert(timer_get_mode(1) == TIMER_MODE_UP);

    g_snapshot = {};
    g_snapshot.count = 1;
    strlcpy(g_snapshot.actions[0].type, ACTION_TYPE_BEEP,
            sizeof(g_snapshot.actions[0].type));
    g_snapshot_ok = true;
    assert(timer_command_run(payload(1, "start", "down", "2"),
                             error, sizeof(error)));
    g_snapshot_ok = false;
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_dispatched = 0;
    g_now += 2000;
    timer_engine_tick();
    assert(g_dispatched == 1);
}

static void test_concurrent_engine_access() {
    timer_engine_init();
    std::thread writer([]() {
        for (int iteration = 0; iteration < 1000; iteration++) {
            assert(timer_configure_and_start(1, TIMER_MODE_DOWN, 1000,
                                             nullptr, 0));
            timer_pause(1);
            timer_resume(1);
            timer_stop(1);
        }
    });
    std::thread reader([]() {
        for (int iteration = 0; iteration < 4000; iteration++) {
            (void)timer_get_state(1);
            (void)timer_get_mode(1);
            (void)timer_get_ms(1);
            (void)timer_is_expired(1);
            char formatted[24];
            timer_format(1, "mm:ss", formatted, sizeof(formatted));
        }
    });
    writer.join();
    reader.join();
}

static void test_mcp_adapter_and_execution() {
    timer_engine_init();
    g_snapshot = {};
    g_snapshot_ok = true;

    JsonDocument doc;
    doc["timer_id"] = 2;
    doc["command"] = "start";
    doc["mode"] = "down";
    doc["value"] = 12;
    TimerPayload parsed = {};
    char error[96] = {};
    assert(timer_mcp_parse_args(doc.as<JsonObjectConst>(), &parsed,
                                error, sizeof(error)));
    assert(timer_command_run(parsed, error, sizeof(error)));
    assert(timer_get_state(2) == TIMER_RUNNING);
    assert(timer_get_mode(2) == TIMER_MODE_DOWN);
    assert(timer_get_ms(2) == 12000);

    doc["value"] = "1234567890123456";
    assert(!timer_mcp_parse_args(doc.as<JsonObjectConst>(), &parsed,
                                 error, sizeof(error)));
    doc["value"] = "12";
    doc["mode"] = "downx";
    assert(!timer_mcp_parse_args(doc.as<JsonObjectConst>(), &parsed,
                                 error, sizeof(error)));
    doc["mode"] = "down";
    doc["command"] = "lap";
    assert(!timer_mcp_parse_args(doc.as<JsonObjectConst>(), &parsed,
                                 error, sizeof(error)));
    g_snapshot_ok = false;
}

static void test_mutex_allocation_failure() {
    timer_test_mutex_fail_on_call = 1;
    timer_engine_init();
    assert(!timer_configure_and_start(1, TIMER_MODE_UP, 0, nullptr, 0));
    const char* controls[] = {"stop", "pause", "resume", "reset"};
    for (const char* control : controls) {
        char error[96] = {};
        assert(!timer_command_run(payload(1, control), error, sizeof(error)));
        assert(error[0] != '\0');
    }
    assert(!timer_stop(1));
    assert(!timer_pause(1));
    assert(!timer_resume(1));
    assert(!timer_reset(1));
    assert(timer_get_state(1) == TIMER_STOPPED);
    assert(timer_get_ms(1) == 0);
    timer_engine_tick();
}

static void test_pre_initialization_controls() {
    const char* controls[] = {"stop", "pause", "resume", "reset"};
    for (const char* control : controls) {
        char error[96] = {};
        assert(!timer_command_run(payload(1, control), error, sizeof(error)));
        assert(error[0] != '\0');
    }
    assert(!timer_stop(1));
    assert(!timer_pause(1));
    assert(!timer_resume(1));
    assert(!timer_reset(1));
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "pre-init-controls") == 0) {
        test_pre_initialization_controls();
        std::puts("timer_commands pre-init-controls: PASS");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "mutex-failure") == 0) {
        test_mutex_allocation_failure();
        std::puts("timer_commands mutex-failure: PASS");
        return 0;
    }
    test_validation_contract();
    test_start_and_toggle_states();
    test_set_adjust_and_expiry();
    test_countdown_target_lifecycle();
    test_basic_control_outcomes();
    test_run_entry_point();
    test_concurrent_engine_access();
    test_mcp_adapter_and_execution();
    std::puts("timer_commands: PASS");
    return 0;
}
