#include "action_dispatch.h"
#include "action_list.h"
#include "action_registry.h"
#include "binding_template.h"
#include "expr_binding.h"
#include "event_binding.h"
#include "action_parse.h"
#include "action_storage.h"
#include "action_validate.h"
#include "runtime_variables.h"
#include "var_binding.h"
#if HAS_MCP
#include "ha_service.h"
#include "mcp_press_button.h"
#endif

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

static bool outer_guard(uint32_t) { return true; }
static bool inner_guard(uint32_t) { return true; }

#if HAS_DISPLAY
static thread_local bool test_display_locked = false;
static thread_local unsigned test_display_locks = 0;
static bool check_dispatch_lock = false;
#endif

static ButtonAction sync_action = {};
static ActionStorage test_action_storage = {};

static ButtonAction make_test_operation(JsonObject action) {
    ButtonAction result = {};
    ActionStorageScope scope(&test_action_storage);
    action_parse(action, result);
    return result;
}

static ActionResult inspect_sync_context(const ActionResolvedArgument*, const char*, uint32_t) {
    const ActionDispatchContext context = action_dispatch_context();
    if (!context.synchronous || context.interactive) return ACTION_FAILED;
    if (context.generation == 202) {
        return context.work_guard == inner_guard && !context.alarm_audio
            ? ACTION_COMPLETE : ACTION_FAILED;
    }
    if (context.generation != 101 || context.work_guard != outer_guard || !context.alarm_audio ||
        action_dispatch_synchronous(sync_action, "nested", inner_guard, 202, false) != ACTION_COMPLETE)
        return ACTION_FAILED;
    const ActionDispatchContext restored = action_dispatch_context();
    return restored.synchronous && restored.work_guard == outer_guard &&
           restored.generation == 101 && restored.alarm_audio && !restored.interactive
        ? ACTION_COMPLETE : ACTION_FAILED;
}

static const ActionOperationDef sync_context_operations[] = {
    {"test.sync_context", "Test synchronous context", nullptr, 0, inspect_sync_context}
};
static const ActionTypeDef sync_context_action = {
    "sync_context", nullptr, nullptr, ACTION_EXECUTION_SYNC, true, false,
    sync_context_operations, 1
};

static unsigned wide_calls = 0;
static char wide_target[81] = {};
static int32_t wide_number = 0;
static int32_t wide_enabled = 0;
static ActionResult capture_wide(const ActionResolvedArgument* args, const char*, uint32_t) {
#if HAS_DISPLAY
    if (check_dispatch_lock)
        return test_display_locked && action_dispatch_context().synchronous
            ? ACTION_FAILED : ACTION_COMPLETE;
#endif
    ++wide_calls;
    strlcpy(wide_target, args[0].text, sizeof(wide_target));
    wide_number = args[1].integer;
    wide_enabled = args[2].integer;
    return ACTION_COMPLETE;
}
static const ActionArgumentDef wide_arguments[] = {
    {"target", "Target", ACTION_ARGUMENT_SCREEN, 0, 0, 80, true, "", "text"},
    {"number", "Number", ACTION_ARGUMENT_INTEGER, INT32_MIN, INT32_MAX, 0, true, "", "number"},
    {"enabled", "Enabled", ACTION_ARGUMENT_BOOLEAN, 0, 0, 0, true, "", "toggle"}
};
static const ActionOperationDef wide_operations[] = {
    {"test.wide", "Wide", wide_arguments, 3, capture_wide}
};
static const ActionTypeDef wide_type = {
    "test_wide", nullptr, nullptr,
    ACTION_EXECUTION_SYNC, true, false, wide_operations, 1
};

static float captured_number = 0;
static unsigned number_calls = 0;
static ActionResult capture_number(const ActionResolvedArgument* args, const char*, uint32_t) {
#if HAS_DISPLAY
    if (check_dispatch_lock && (test_display_locked || !action_dispatch_context().synchronous))
        return ACTION_FAILED;
#endif
    captured_number = args[0].number;
    ++number_calls;
    return ACTION_COMPLETE;
}
static const ActionArgumentDef number_arguments[] = {
    {"value", "Value", ACTION_ARGUMENT_NUMBER, -10, 10, 0, true, "[var:number]", "number"},
};
static const ActionArgumentDef number_wide_arguments[] = {
    {"value", "Value", ACTION_ARGUMENT_NUMBER, INT32_MIN, INT32_MAX, 0, true, "[var:number]", "number"},
};
static const ActionOperationDef number_operations[] = {
    {"test.number", "Number", number_arguments, 1, capture_number},
    {"test.number_wide", "Wide number", number_wide_arguments, 1, capture_number},
};
static const ActionTypeDef number_type = {
    "test_number", nullptr, nullptr,
    ACTION_EXECUTION_SYNC, false, false, number_operations, 2
};
static bool number_operation_dispatch() {
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"test.number","args":{"value":1.25}})")) return false;
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    JsonDocument output;
    action_to_json(action, output.to<JsonObject>());
    bool ok = !storage.failed && output["args"]["value"].is<float>() &&
        output["args"]["value"].as<float>() == 1.25f &&
        action_dispatch(action, "number") == ACTION_COMPLETE && captured_number == 1.25f;
    action_storage_clear(storage);
    input["args"]["value"].to<JsonObject>()["$template"] = "[var:number]";
    runtime_variable_set("number", "-0.5");
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    ok = ok && !storage.failed && action_dispatch(action, "number") == ACTION_COMPLETE &&
        captured_number == -0.5f;
    const unsigned before = number_calls;
    for (const char* invalid : {"nan", "1x", "11", "1e999"}) {
        runtime_variable_set("number", invalid);
        ok = ok && action_dispatch(action, "number") == ACTION_FAILED && number_calls == before;
    }
    action_storage_clear(storage);
    input["args"]["value"] = true;
    ok = ok && action_operation_validate_json(input.as<JsonObjectConst>(), false);
    input["args"]["value"] = 2;
    ok = ok && !action_operation_validate_json(input.as<JsonObjectConst>(), false);
    input["op"] = "test.number_wide";
    input["args"]["value"] = 2147483647;
    ok = ok && action_operation_validate_json(input.as<JsonObjectConst>(), false);
    float normalized = 0;
    ok = ok && action_argument_normalize_text(number_wide_arguments[0], "2147483647",
                                              nullptr, nullptr, &normalized);
    return ok;
}

static bool wide_operation_dispatch() {
    char target[81];
    std::memset(target, 'x', 80);
    target[80] = '\0';
    JsonDocument input;
    input["op"] = "test.wide";
    input["args"]["target"]["$template"] = target;
    input["args"]["number"]["$template"] = "-15";
    input["args"]["enabled"] = true;
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    bool ok = !storage.failed &&
        action_list_dispatch_result(&action, 1, "wide") == ACTION_COMPLETE &&
        wide_calls == 1 && !std::strcmp(wide_target, target) &&
        wide_number == -15 && wide_enabled == 1;
    action_storage_clear(storage);

    input["args"]["target"] = "[event:value]";
    input["args"]["number"] = 3;
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    ok = ok && !storage.failed && action_dispatch(action, "literal") == ACTION_COMPLETE &&
         wide_calls == 2 && !std::strcmp(wide_target, "[event:value]") && wide_number == 3;
    action_storage_clear(storage);

    for (const char* invalid : {"1.5", "2147483648"}) {
        input["args"]["number"].to<JsonObject>()["$template"] = invalid;
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        ok = ok && !storage.failed && action_dispatch(action, "wide") == ACTION_FAILED &&
             wide_calls == 2;
        action_storage_clear(storage);
    }
    char too_long[82];
    std::memset(too_long, 'x', 81);
    too_long[81] = '\0';
    input["args"]["target"] = too_long;
    input["args"]["number"] = 1;
    ok = ok && action_operation_validate_json(input.as<JsonObjectConst>(), false);
    input["args"]["target"].to<JsonObject>()["$template"] = too_long;
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    ok = ok && !storage.failed && action_dispatch(action, "wide") == ACTION_FAILED &&
         wide_calls == 2;
    action_storage_clear(storage);
    ActionArgumentDef invalid_argument = wide_arguments[0];
    invalid_argument.max_length = ACTION_OPERATION_MAX_TEXT_LENGTH + 1;
    ActionOperationDef invalid_descriptor = {"test.invalid", "Invalid", &invalid_argument, 1, capture_wide};
    ok = ok && action_operation_validate_descriptor(invalid_descriptor);
    invalid_descriptor.argument_count = ACTION_OPERATION_MAX_ARGUMENTS + 1;
    return ok && action_operation_validate_descriptor(invalid_descriptor);
}

static bool malformed_operation_templates() {
    JsonDocument document;
    if (deserializeJson(document, R"([
        {"op":"test.wide","args":{"target":{"$template":"[event:value]"},"number":1,"enabled":true}},
        {"op":"test.wide","args":{"target":"suffix","number":2,"enabled":false}}
    ])")) return false;
    JsonObject first = document[0].as<JsonObject>();
    first["args"]["target"]["$template"] = "[event:value";
    if (!action_operation_validate_json(first, false) || !action_operation_validate_json(first, true))
        return false;
    first["args"]["target"]["$template"] = "[var:x] [event:value";
    if (!action_operation_validate_json(first, false)) return false;
    first["args"]["target"]["$template"] = "[expr:[var:wellformed]+1|0]";
    if (action_operation_validate_json(first, false) || action_operation_validate_json(first, true))
        return false;
    first["args"]["target"]["$template"] = "[event:value]";
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(first, actions[0]);
        action_parse(document[1].as<JsonObject>(), actions[1]);
    }
    if (storage.failed || !actions[0].payload.operation.arguments) {
        action_storage_clear(storage);
        return false;
    }
    ActionArgumentBlock* block = actions[0].payload.operation.arguments;
    const ActionStoredArgument* arguments = action_storage_arguments(block);
    char* target = reinterpret_cast<char*>(block) + arguments[0].offset;
    target[std::strlen(target) - 1] = '\0';
    const unsigned before = wide_calls;
    bool ok = action_list_dispatch_result(actions, 2, "malformed") == ACTION_FAILED &&
              wide_calls == before;
    action_storage_clear(storage);

    first["args"]["target"] = "[event:value";
    ok = ok && !action_operation_validate_json(first, true);
    {
        ActionStorageScope scope(&storage);
        action_parse(first, actions[0]);
    }
    ok = ok && !storage.failed && action_dispatch(actions[0], "literal") == ACTION_COMPLETE &&
         wide_calls == before + 1 && !std::strcmp(wide_target, "[event:value");
    action_storage_clear(storage);

    runtime_variable_set("wellformed", "2");
    first["args"]["target"].to<JsonObject>()["$template"] = "[expr:[var:wellformed]+1|0]";
    {
        ActionStorageScope scope(&storage);
        action_parse(first, actions[0]);
    }
    ok = ok && !storage.failed && action_dispatch(actions[0], "nested") == ACTION_COMPLETE &&
         wide_calls == before + 2 && !std::strcmp(wide_target, "3");
    action_storage_clear(storage);
    first["args"]["target"]["$template"] = "[var:not_set|safe]";
    {
        ActionStorageScope scope(&storage);
        action_parse(first, actions[0]);
    }
    ok = ok && !storage.failed && action_dispatch(actions[0], "fallback") == ACTION_COMPLETE &&
         wide_calls == before + 3 && !std::strcmp(wide_target, "safe");
    action_storage_clear(storage);
    return ok;
}

#if HAS_DISPLAY && HAS_PSRAM
#include "audio.h"
#include "camera.h"
#include "config_manager.h"
#include "device_dialog.h"
#include "gamepad_hid.h"
#include "item_binding.h"
#include "keyboard_hid.h"
#include "main_loop_bridge.h"
#include "mouse_hid.h"
#include "pad_binding.h"
#include "reboot_service.h"
#include "voice_notes.h"
#include "voice_notes_file.h"
#include "visual_alert.h"
#include "voice_notes_processing.h"
#include "wifi_manager.h"
#include <cstdlib>

#define DISPLAY_MANAGER_H
enum DisplayTaskDispatchResult { DISPLAY_TASK_DISPATCH_OK, DISPLAY_TASK_DISPATCH_INVALID };
using DisplayTaskExec = void (*)(const void*, bool*, char*, size_t);
using DisplayTaskCleanup = void (*)(const void*);

static bool navigation_available = true;
static bool list_capture_available = true;
static unsigned button_navigation_calls = 0;
static unsigned list_navigation_calls = 0;
static unsigned pad_cycle_calls = 0;
static int8_t cycle_direction = 0;
static bool cycle_wrap = false;
static uint32_t cycle_exclusions = 0;
static char navigation_command[32] = {};
static char navigation_target[33] = {};

struct DisplayManager {
    bool isInLvglTask() const;
    bool captureListNavigation(uint32_t* generation) {
        *generation = 7;
        return list_capture_available;
    }
    bool navigateButtons(const char* command) {
        ++button_navigation_calls;
        strlcpy(navigation_command, command, sizeof(navigation_command));
        return navigation_available;
    }
    bool navigateList(const char* target, const char* command, uint32_t generation) {
        ++list_navigation_calls;
        strlcpy(navigation_target, target, sizeof(navigation_target));
        strlcpy(navigation_command, command, sizeof(navigation_command));
        return generation == 7 && navigation_available;
    }
};
bool DisplayManager::isInLvglTask() const { return false; }

static DisplayManager navigation_manager;
DisplayManager* displayManager = &navigation_manager;
uint32_t device_dialog_input_generation() { return 1; }
uint32_t pad_config_get_generation() { return 1; }
bool display_manager_cycle_pad(int8_t direction, bool wrap, uint32_t exclusions, bool wake) {
    ++pad_cycle_calls;
    cycle_direction = direction;
    cycle_wrap = wrap;
    cycle_exclusions = exclusions;
    return wake && navigation_available;
}
DisplayTaskDispatchResult display_manager_dispatch(
        DisplayTaskExec exec, DisplayTaskCleanup, const void* context, size_t,
        uint32_t, bool* ok, char*, size_t) {
    exec(context, ok, nullptr, 0);
    return DISPLAY_TASK_DISPATCH_OK;
}

#define ARDUINO 1
#include "../src/app/pad_cycle.cpp"
#include "../src/app/actions/cycle_pad_action.cpp"
#include "../src/app/actions/button_navigation_action.cpp"
#include "../src/app/actions/list_navigation_action.cpp"
#include "../src/app/actions/bthome_action.cpp"
#undef HAS_MQTT
#define HAS_MQTT 1
#define MQTT_MANAGER_H
struct MqttManager {
    bool accepted = true;
    bool retained = true;
    unsigned calls = 0;
    char topic[CONFIG_MQTT_TOPIC_MAX_LEN] = {};
    char payload[CONFIG_MQTT_PAYLOAD_MAX_LEN] = {};
    bool publish(const char* new_topic, const char* new_payload, bool new_retained) {
        ++calls;
        strlcpy(topic, new_topic, sizeof(topic));
        strlcpy(payload, new_payload, sizeof(payload));
        retained = new_retained;
        return accepted;
    }
};
static MqttManager mqtt_manager;
#include "../src/app/actions/mqtt_action.cpp"
#include "../src/app/actions/sound_alert_action.cpp"
#include "../src/app/actions/alarm_tone_action.cpp"
#include "../src/app/actions/alarm_mp3_action.cpp"
#include "../src/app/actions/alarm_action.cpp"
#undef MQTT_MANAGER_H
#undef HAS_MQTT
#define HAS_MQTT 0
#undef ARDUINO
#define HAS_IMAGE_LIBRARY 1
#include "../src/app/actions/image_next_action.cpp"
#include "../src/app/actions/image_previous_action.cpp"
#undef HAS_IMAGE_LIBRARY

static bool image_available = true;
static unsigned image_next_calls = 0;
static unsigned image_previous_calls = 0;
bool image_library_runtime_next(char*, size_t) {
    ++image_next_calls;
    return image_available;
}
bool image_library_runtime_previous(char*, size_t) {
    ++image_previous_calls;
    return image_available;
}

static bool hid_keyboard_accepted = true;
static bool hid_keyboard_busy = false;
static KeyboardTransport hid_transport = KeyboardTransport::Ble;
static unsigned hid_keyboard_calls = 0;
static char hid_sequence[CONFIG_KEY_SEQ_MAX_LEN] = {};
static uint32_t hid_keyboard_token = 0;
bool keyboard_hid_request_sequence(const char* sequence, uint32_t token) {
    ++hid_keyboard_calls;
    strlcpy(hid_sequence, sequence, sizeof(hid_sequence));
    hid_keyboard_token = token;
    return hid_keyboard_accepted;
}
bool keyboard_hid_is_busy() { return hid_keyboard_busy; }
KeyboardTransport keyboard_hid_transport() { return hid_transport; }
static std::string hid_text;
static std::string hid_text_source;
static bool hid_controls = false;
static BindingResolverStatus resolve_hid_text(const char*, char* out, size_t capacity) {
    return strlcpy(out, hid_text_source.c_str(), capacity) < capacity
        ? BINDING_RESOLVER_RESOLVED : BINDING_RESOLVER_UNAVAILABLE;
}
bool keyboard_hid_request_text(const char* text, uint32_t token, bool allow_controls) {
    hid_text = text;
    hid_controls = allow_controls;
    hid_keyboard_token = token;
    return hid_keyboard_accepted;
}
void keyboard_hid_cancel() {}

static bool hid_mouse_accepted = true;
static unsigned hid_mouse_calls = 0;
static uint32_t hid_mouse_epoch = 0;
static uint8_t hid_mouse_button = 0;
uint32_t mouse_hid_epoch() { return 7; }
bool mouse_hid_click(uint32_t epoch, uint8_t buttons) {
    ++hid_mouse_calls;
    hid_mouse_epoch = epoch;
    hid_mouse_button = buttons;
    return hid_mouse_accepted;
}

static bool hid_gamepad_accepted = true;
static unsigned hid_gamepad_calls = 0;
static GamepadControl hid_gamepad_control{};
static bool hid_gamepad_down = false;
static uint32_t hid_gamepad_token = 0;
bool gamepad_hid_hold(GamepadControl control, bool down) {
    ++hid_gamepad_calls;
    hid_gamepad_control = control;
    hid_gamepad_down = down;
    return hid_gamepad_accepted;
}
bool gamepad_hid_tap(GamepadControl control, uint32_t token) {
    ++hid_gamepad_calls;
    hid_gamepad_control = control;
    hid_gamepad_token = token;
    return hid_gamepad_accepted;
}

static bool hid_ble_initialized = true;
static unsigned hid_pair_requests = 0;
bool ble_hid_is_initialized() { return hid_ble_initialized; }
void ble_hid_request_pairing() { ++hid_pair_requests; }

static bool bthome_accepted = true;
static unsigned bthome_calls = 0;
static uint8_t bthome_button = 0;
static uint8_t bthome_event = 0;
bool bthome_events_send(uint8_t button, uint8_t event) {
    ++bthome_calls;
    bthome_button = button;
    bthome_event = event;
    return bthome_accepted;
}

static unsigned wifi_requests = 0;
static WifiNetworkCommand wifi_command = WifiNetworkCommand::None;
static bool wifi_admitted = true;
bool wifi_manager_request_network_command(WifiNetworkCommand command) {
    ++wifi_requests;
    wifi_command = command;
    return wifi_admitted;
}
bool power_manager_request_setup_ap_restart() { return false; }
static unsigned reboot_requests = 0;
static unsigned sleep_requests = 0;
static unsigned wake_requests = 0;
void reboot_request(uint32_t, RebootOwnership) { ++reboot_requests; }
void screen_saver_manager_sleep_now() { ++sleep_requests; }
void screen_saver_manager_wake() { ++wake_requests; }
static unsigned dialog_notify_calls = 0;
static unsigned dialog_confirm_calls = 0;
static bool dialog_accepted = true;
static char dialog_message[CONFIG_DIALOG_MESSAGE_MAX_LEN] = {};
static uint32_t dialog_duration = 0;
static uint32_t dialog_token = 0;
static ConfirmPayload dialog_content = {};
bool device_dialog_notify(const char* message, uint32_t duration, uint32_t*) {
    ++dialog_notify_calls;
    strlcpy(dialog_message, message, sizeof(dialog_message));
    dialog_duration = duration;
    return dialog_accepted;
}
bool device_dialog_confirm(const ConfirmPayload& content, uint32_t token) {
    ++dialog_confirm_calls;
    dialog_content = content;
    dialog_token = token;
    return dialog_accepted;
}
static unsigned alert_show_calls = 0;
static unsigned alert_stop_calls = 0;
static unsigned alert_wake_calls = 0;
static VisualAlertParams alert_params = {};
void screen_saver_manager_notify_activity(bool wake) { if (wake) ++alert_wake_calls; }
uint8_t visual_alert_pattern_from_str(const char* pattern) {
    if (!std::strcmp(pattern, "blink")) return VA_PATTERN_BLINK;
    if (!std::strcmp(pattern, "solid")) return VA_PATTERN_SOLID;
    return VA_PATTERN_BREATHE;
}
void visual_alert_show(const VisualAlertParams* params) {
    ++alert_show_calls;
    alert_params = *params;
}
void visual_alert_stop() { ++alert_stop_calls; }
enum DisplayRefreshRequestResult : uint8_t {
    DISPLAY_REFRESH_QUEUED, DISPLAY_REFRESH_UNAVAILABLE, DISPLAY_REFRESH_FAILED,
};
static unsigned refresh_calls = 0;
static DisplayRefreshRequestResult refresh_result = DISPLAY_REFRESH_QUEUED;
DisplayRefreshRequestResult display_manager_request_full_refresh() {
    ++refresh_calls;
    return refresh_result;
}
DeviceConfig device_config = {};
static bool save_level_allowed = true;
static unsigned save_brightness_requests = 0;
static unsigned save_volume_requests = 0;
static uint8_t saved_level = 0;
bool config_manager_save_brightness(DeviceConfig* config, uint8_t value) {
    ++save_brightness_requests;
    saved_level = value;
    return config == &device_config && save_level_allowed;
}
bool config_manager_save_volume(DeviceConfig* config, uint8_t value) {
    ++save_volume_requests;
    saved_level = value;
    return config == &device_config && save_level_allowed;
}
static uint8_t test_brightness = 50;
static unsigned brightness_requests = 0;
void screen_saver_manager_set_brightness(uint8_t value) {
    test_brightness = value;
    ++brightness_requests;
}
uint8_t screen_saver_manager_get_brightness() { return test_brightness; }
static uint8_t test_volume = 50;
static unsigned volume_requests = 0;
void audio_set_volume(uint8_t value) {
    test_volume = value;
    ++volume_requests;
}
uint8_t audio_get_volume() { return test_volume; }
static unsigned music_requests = 0;
static MusicCommand last_music_command = MUSIC_COMMAND_STOP;
static AudioMusicSubmitResult music_result = AUDIO_MUSIC_SUBMIT_QUEUED;
static AudioPlaybackGuard music_guard = nullptr;
static uint32_t music_generation = 0;
AudioMusicSubmitResult audio_music_command(MusicCommand command, AudioPlaybackGuard guard, uint32_t generation) {
    ++music_requests;
    last_music_command = command;
    music_guard = guard;
    music_generation = generation;
    return music_result;
}

static bool alarm_command_accepted = true;
static unsigned alarm_command_calls = 0;
static char alarm_command_name[32] = {};
static uint8_t alarm_command_id = 0;
static int alarm_command_value = 0;
static uint8_t alarm_command_day = 0;
bool alarm_command_submit(const char* command, uint8_t id, int value, uint8_t day) {
    ++alarm_command_calls;
    strlcpy(alarm_command_name, command, sizeof(alarm_command_name));
    alarm_command_id = id;
    alarm_command_value = value;
    alarm_command_day = day;
    return alarm_command_accepted;
}

static bool alert_accepted = true;
static unsigned tone_requests = 0;
static unsigned sound_requests = 0;
static unsigned audio_stop_requests = 0;
static unsigned audio_stop_all_requests = 0;
static unsigned audio_stop_owned_requests = 0;
static char alert_source[CONFIG_BEEP_PATTERN_MAX_LEN] = {};
static uint8_t alert_volume = 0;
static bool alert_loop = false;
static bool alert_alarm = false;
static AudioPlaybackGuard alert_guard = nullptr;
static uint32_t alert_generation = 0;
static void capture_alert(const char* source, uint8_t volume, bool loop,
                          AudioPlaybackGuard guard, uint32_t generation, bool alarm) {
    strlcpy(alert_source, source, sizeof(alert_source));
    alert_volume = volume;
    alert_loop = loop;
    alert_guard = guard;
    alert_generation = generation;
    alert_alarm = alarm;
}
bool audio_submit_tone(const char* pattern, uint8_t volume, bool loop,
                       AudioPlaybackGuard guard, uint32_t generation, bool alarm,
                       AudioPlaybackFinished) {
    ++tone_requests;
    capture_alert(pattern, volume, loop, guard, generation, alarm);
    return alert_accepted;
}
bool audio_submit_sound(const char* file, uint8_t volume, AudioPlaybackGuard guard,
                        uint32_t generation, bool loop, bool alarm) {
    ++sound_requests;
    capture_alert(file, volume, loop, guard, generation, alarm);
    return alert_accepted;
}
void audio_stop() { ++audio_stop_requests; }
void audio_stop_all() { ++audio_stop_all_requests; }
void audio_stop_owned(AudioPlaybackGuard guard, uint32_t generation) {
    ++audio_stop_owned_requests;
    alert_guard = guard;
    alert_generation = generation;
}

static bool fail_psram = false;
void* heap_caps_malloc(size_t size, uint32_t) { return fail_psram ? nullptr : std::malloc(size); }
void* heap_caps_realloc(void* ptr, size_t size, uint32_t) { return fail_psram ? nullptr : std::realloc(ptr, size); }
void heap_caps_free(void* ptr) { std::free(ptr); }
bool psramFound() { return true; }

static bool argument_allocation_failure() {
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"test.wide","args":{"target":"pad_1","number":1,"enabled":true}})"))
        return false;
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    const unsigned before = wide_calls;
    fail_psram = true;
    const ActionResult result = action_dispatch(action, "allocation");
    fail_psram = false;
    action_storage_clear(storage);
    return result == ACTION_FAILED && wide_calls == before;
}

static char shown_screen[CONFIG_SCREEN_ID_MAX_LEN] = {};
static TimerPayload last_timer = {};
static unsigned screen_calls = 0;
static unsigned back_calls = 0;
static bool back_available = true;
static unsigned timer_calls = 0;
static bool timer_command_allowed = true;
static ActionStorage* replaced_storage = nullptr;
static ButtonAction* replaced_actions = nullptr;
void display_manager_show_screen(const char* screen, bool* success, bool) {
    ++screen_calls;
    strlcpy(shown_screen, screen, sizeof(shown_screen));
    *success = std::strcmp(screen, "missing") != 0;
    if (replaced_storage) {
        action_storage_clear(*replaced_storage);
        std::memset(replaced_actions, 0, 2 * sizeof(ButtonAction));
        replaced_storage = nullptr;
        replaced_actions = nullptr;
    }
}
bool display_manager_go_back(bool wake) {
    ++back_calls;
    return wake && back_available;
}
bool timer_command_run(const TimerPayload& payload, char* error, size_t error_size) {
    ++timer_calls;
    last_timer = payload;
    if (!timer_command_allowed) strlcpy(error, "timer rejected", error_size);
    return timer_command_allowed;
}

static bool run_navigation_sequence(const char* json, const char* event_value, ActionResult expected) {
    JsonDocument input;
    if (deserializeJson(input, json) || action_operation_validate_json(input.as<JsonObjectConst>()))
        return false;
    JsonDocument suffix;
    if (deserializeJson(suffix, R"({"op":"screen.open","args":{"target":"pad_1"}})"))
        return false;
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
        action_parse(suffix.as<JsonObject>(), actions[1]);
    }
    const bool parsed = !storage.failed && action_operation_for_action(actions[0]) &&
        action_operation_for_action(actions[1]);
    const unsigned before = screen_calls;
    ActionEventData event = {event_value, nullptr, BINDING_VALUE_KIND_UNKNOWN};
    ActionResult result;
    {
        EventBindingScope scope(event_value ? &event : nullptr);
        result = !parsed ? ACTION_FAILED :
            action_list_dispatch_result(actions, 2, "navigation");
    }
    action_storage_clear(storage);
    return parsed && result == expected &&
        screen_calls == before + (expected == ACTION_COMPLETE ? 1u : 0u);
}

static bool run_hid_pending(const char* json, const char* event_value,
                            uint32_t& token, bool completion_success) {
    JsonDocument input;
    JsonDocument suffix;
    if (deserializeJson(input, json) || action_operation_validate_json(input.as<JsonObjectConst>()) ||
        deserializeJson(suffix, R"({"op":"screen.open","args":{"target":"pad_1"}})"))
        return false;
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
        action_parse(suffix.as<JsonObject>(), actions[1]);
    }
    const bool parsed = !storage.failed && action_operation_for_action(actions[0]) &&
        action_operation_for_action(actions[1]);
    const unsigned before = screen_calls;
    token = 0;
    ActionEventData event = {event_value, nullptr, BINDING_VALUE_KIND_UNKNOWN};
    ActionResult result;
    {
        EventBindingScope scope(&event);
        result = parsed ? action_list_dispatch_result(actions, 2, "HID") : ACTION_FAILED;
    }
    action_storage_clear(storage);
    if (result != ACTION_PENDING || !token || screen_calls != before ||
        !action_continuation_complete(token, completion_success)) return false;
    action_list_dispatch_continuation();
    return screen_calls == before + (completion_success ? 1u : 0u);
}

static bool hid_operation_boundaries() {
    const unsigned saved_screen_calls = screen_calls;
    char saved_screen[sizeof(shown_screen)];
    strlcpy(saved_screen, shown_screen, sizeof(saved_screen));

    const char* keyboard = R"({"op":"keyboard.send","args":{"sequence":{"$template":"[event:value]"}}})";
    bool ok = run_hid_pending(keyboard, "a 50ms b", hid_keyboard_token, true) &&
        hid_keyboard_calls == 1 && !std::strcmp(hid_sequence, "a 50ms b");
    ok = ok && run_hid_pending(keyboard, "b", hid_keyboard_token, false) &&
        hid_keyboard_calls == 2 && !std::strcmp(hid_sequence, "b");
    hid_keyboard_accepted = false;
    ok = ok && run_navigation_sequence(keyboard, "c", ACTION_FAILED) && hid_keyboard_calls == 3;
    hid_keyboard_accepted = true;
    ok = ok && run_navigation_sequence(keyboard, nullptr, ACTION_FAILED) && hid_keyboard_calls == 3;

    binding_template_register("longtext", resolve_hid_text, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true});
    const char* type_text = R"({"op":"keyboard.type","args":{"text":{"$template":"[longtext:x]"}}})";
    std::string long_text(KEYBOARD_TEXT_MAX_BYTES, 'a');
    hid_text_source = long_text;
    ok = ok && run_hid_pending(type_text, nullptr, hid_keyboard_token, true) &&
        hid_text == long_text && !hid_controls;
    long_text.push_back('b');
    hid_text_source = long_text;
    ok = ok && run_navigation_sequence(type_text, nullptr, ACTION_FAILED) &&
        hid_text.size() == KEYBOARD_TEXT_MAX_BYTES;
    std::string composite;
    for (unsigned i = 0; i < 16; ++i) composite += "[var:x]";
    composite += std::string(16, 'a');
    runtime_variable_set("x", std::string(127, 'a').c_str());
    JsonDocument composed;
    composed["op"] = "keyboard.type";
    composed["args"]["text"]["$template"] = composite;
    std::string composed_json;
    serializeJson(composed, composed_json);
    ok = ok && run_hid_pending(composed_json.c_str(), nullptr, hid_keyboard_token, true) &&
        hid_text == std::string(KEYBOARD_TEXT_MAX_BYTES, 'a');
    runtime_variable_set("x", "");
    hid_text_source.clear();

    const char* mouse = R"({"op":"mouse.click","args":{"button":{"$template":"[event:value]"}}})";
    ok = ok && run_navigation_sequence(mouse, "right", ACTION_COMPLETE) &&
        hid_mouse_calls == 1 && hid_mouse_epoch == 7 && hid_mouse_button == mouse_hid_buttons::right;
    ok = ok && run_navigation_sequence(mouse, "bad", ACTION_FAILED) && hid_mouse_calls == 1;
    hid_mouse_accepted = false;
    ok = ok && run_navigation_sequence(mouse, "left", ACTION_FAILED) && hid_mouse_calls == 2;
    hid_mouse_accepted = true;

    const struct {
        const char* json;
        const char* event;
        GamepadControlKind kind;
        uint8_t index;
        bool down;
    } holds[] = {
        {R"({"op":"gamepad.button.down","args":{"button":{"$template":"[event:value]"}}})", "16", GamepadControlKind::Button, 15, true},
        {R"({"op":"gamepad.button.up","args":{"button":{"$template":"[event:value]"}}})", "1", GamepadControlKind::Button, 0, false},
        {R"({"op":"gamepad.hat.down","args":{"direction":{"$template":"[event:value]"}}})", "right", GamepadControlKind::Hat, 3, true},
        {R"({"op":"gamepad.hat.up","args":{"direction":{"$template":"[event:value]"}}})", "down", GamepadControlKind::Hat, 1, false},
        {R"({"op":"gamepad.trigger.down","args":{"trigger":{"$template":"[event:value]"}}})", "left", GamepadControlKind::Trigger, 0, true},
        {R"({"op":"gamepad.trigger.up","args":{"trigger":{"$template":"[event:value]"}}})", "right", GamepadControlKind::Trigger, 1, false}
    };
    for (const auto& test : holds) {
        const unsigned before = hid_gamepad_calls;
        ok = ok && run_navigation_sequence(test.json, test.event, ACTION_COMPLETE) &&
            hid_gamepad_calls == before + 1 && hid_gamepad_control.kind == test.kind &&
            hid_gamepad_control.index == test.index && hid_gamepad_down == test.down;
    }
    unsigned before_gamepad = hid_gamepad_calls;
    ok = ok && run_navigation_sequence(holds[0].json, "17", ACTION_FAILED) &&
        run_navigation_sequence(holds[2].json, "diagonal", ACTION_FAILED) &&
        hid_gamepad_calls == before_gamepad;
    hid_gamepad_accepted = false;
    ok = ok && run_navigation_sequence(holds[0].json, "1", ACTION_FAILED) &&
        hid_gamepad_calls == before_gamepad + 1;
    hid_gamepad_accepted = true;

    const struct {
        const char* json;
        const char* event;
        GamepadControlKind kind;
        uint8_t index;
    } taps[] = {
        {R"({"op":"gamepad.button.tap","args":{"button":{"$template":"[event:value]"}}})", "8", GamepadControlKind::Button, 7},
        {R"({"op":"gamepad.hat.tap","args":{"direction":{"$template":"[event:value]"}}})", "up", GamepadControlKind::Hat, 0},
        {R"({"op":"gamepad.trigger.tap","args":{"trigger":{"$template":"[event:value]"}}})", "right", GamepadControlKind::Trigger, 1}
    };
    for (const auto& test : taps) {
        before_gamepad = hid_gamepad_calls;
        ok = ok && run_hid_pending(test.json, test.event, hid_gamepad_token, true) &&
            hid_gamepad_calls == before_gamepad + 1 && hid_gamepad_control.kind == test.kind &&
            hid_gamepad_control.index == test.index;
    }
    before_gamepad = hid_gamepad_calls;
    ok = ok && run_hid_pending(taps[0].json, "1", hid_gamepad_token, false) &&
        hid_gamepad_calls == before_gamepad + 1;
    hid_gamepad_accepted = false;
    ok = ok && run_navigation_sequence(taps[1].json, "up", ACTION_FAILED) &&
        hid_gamepad_calls == before_gamepad + 2;
    hid_gamepad_accepted = true;

    const char* pair = R"({"op":"ble.pair","args":{}})";
    ok = ok && run_navigation_sequence(pair, nullptr, ACTION_COMPLETE) && hid_pair_requests == 1;
    hid_ble_initialized = false;
    ok = ok && run_navigation_sequence(pair, nullptr, ACTION_FAILED) && hid_pair_requests == 1;
    hid_ble_initialized = true;
    hid_keyboard_busy = true;
    ok = ok && run_navigation_sequence(pair, nullptr, ACTION_FAILED) && hid_pair_requests == 1;
    hid_keyboard_busy = false;
    hid_transport = KeyboardTransport::Usb;
    ok = ok && run_navigation_sequence(pair, nullptr, ACTION_FAILED) && hid_pair_requests == 1;
    hid_transport = KeyboardTransport::Ble;

    screen_calls = saved_screen_calls;
    strlcpy(shown_screen, saved_screen, sizeof(shown_screen));
    return ok;
}

static bool alarm_operation_boundaries() {
    const unsigned saved_screen_calls = screen_calls;
    char saved_screen[sizeof(shown_screen)];
    strlcpy(saved_screen, shown_screen, sizeof(saved_screen));
    const struct {
        const char* json;
        const char* event;
        const char* command;
        uint8_t id;
        int value;
        uint8_t day;
    } cases[] = {
        {R"({"op":"alarm.cancel","args":{"alarm":{"$template":"[event:value]"}}})", "0", "cancel", 0, 0, 0},
        {R"({"op":"alarm.snooze","args":{"alarm":"1"}})", nullptr, "snooze", 1, 0, 0},
        {R"({"op":"alarm.set_time","args":{"minutes":{"$template":"[event:value]"}}})", "1439", "set_time", 1, 1439, 0},
        {R"({"op":"alarm.adjust_minutes","args":{"minutes":{"$template":"[event:value]"}}})", "-17", "adjust_minutes", 1, -17, 0},
        {R"({"op":"alarm.enable","args":{}})", nullptr, "enable", 1, 0, 0},
        {R"({"op":"alarm.disable","args":{}})", nullptr, "disable", 1, 0, 0},
        {R"({"op":"alarm.toggle","args":{}})", nullptr, "toggle", 1, 0, 0},
        {R"({"op":"alarm.weekday_enable","args":{"day":"0"}})", nullptr, "weekday_enable", 1, 0, 0},
        {R"({"op":"alarm.weekday_disable","args":{"day":"6"}})", nullptr, "weekday_disable", 1, 0, 6},
        {R"({"op":"alarm.weekday_toggle","args":{"day":{"$template":"[event:value]"}}})", "3", "weekday_toggle", 1, 0, 3},
    };
    bool ok = true;
    for (const auto& test : cases) {
        const unsigned before = alarm_command_calls;
        ok = ok && run_navigation_sequence(test.json, test.event, ACTION_COMPLETE) &&
            alarm_command_calls == before + 1 &&
            !std::strcmp(alarm_command_name, test.command) && alarm_command_id == test.id &&
            alarm_command_value == test.value && alarm_command_day == test.day;
        alarm_command_accepted = false;
        ok = ok && run_navigation_sequence(test.json, test.event, ACTION_FAILED) &&
            alarm_command_calls == before + 2;
        alarm_command_accepted = true;
    }
    const unsigned before = alarm_command_calls;
    ok = ok && run_navigation_sequence(cases[0].json, "2", ACTION_FAILED) &&
        run_navigation_sequence(cases[2].json, "1440", ACTION_FAILED) &&
        run_navigation_sequence(cases[2].json, "-1", ACTION_FAILED) &&
        run_navigation_sequence(cases[3].json, "1.5", ACTION_FAILED) &&
        run_navigation_sequence(cases[3].json, "2147483648", ACTION_FAILED) &&
        run_navigation_sequence(cases[9].json, "7", ACTION_FAILED) &&
        alarm_command_calls == before;
    screen_calls = saved_screen_calls;
    strlcpy(shown_screen, saved_screen, sizeof(shown_screen));
    return ok;
}

static bool audio_alert_operation_boundaries() {
    const unsigned saved_screen_calls = screen_calls;
    char saved_screen[sizeof(shown_screen)];
    strlcpy(saved_screen, shown_screen, sizeof(saved_screen));
    const struct { const char* json; const char* source; bool mp3; bool loop; } cases[] = {
        {R"({"op":"audio.alert.tone","args":{"pattern":"800:200","volume":{"$template":"[event:value]"}}})", "800:200", false, false},
        {R"({"op":"audio.alert.mp3","args":{"file":"doorbell","volume":{"$template":"[event:value]"}}})", "doorbell", true, false},
        {R"({"op":"alarm.tone","args":{"pattern":"800:200","volume":{"$template":"[event:value]"}}})", "800:200", false, true},
        {R"({"op":"alarm.mp3","args":{"file":"doorbell","volume":{"$template":"[event:value]"}}})", "doorbell", true, true},
    };
    bool ok = true;
    for (const auto& test : cases) {
        const unsigned before = test.mp3 ? sound_requests : tone_requests;
        ok = ok && run_navigation_sequence(test.json, "37", ACTION_COMPLETE) &&
            (test.mp3 ? sound_requests : tone_requests) == before + 1 &&
            !std::strcmp(alert_source, test.source) && alert_volume == 37 &&
            alert_loop == test.loop && !alert_guard && alert_generation == 0 && !alert_alarm;
        alert_accepted = false;
        ok = ok && run_navigation_sequence(test.json, "100", ACTION_FAILED) &&
            (test.mp3 ? sound_requests : tone_requests) == before + 2 && alert_volume == 100;
        alert_accepted = true;
        ok = ok && run_navigation_sequence(test.json, "101", ACTION_FAILED) &&
            run_navigation_sequence(test.json, "-1", ACTION_FAILED) &&
            run_navigation_sequence(test.json, "not-a-number", ACTION_FAILED) &&
            run_navigation_sequence(test.json, nullptr, ACTION_FAILED) &&
            (test.mp3 ? sound_requests : tone_requests) == before + 2;
    }
    const char* dynamic_tone = R"({"op":"audio.alert.tone","args":{"pattern":{"$template":"[event:value]"},"volume":0}})";
    const char* dynamic_file = R"({"op":"alarm.mp3","args":{"file":{"$template":"[event:value]"},"volume":0}})";
    ok = ok && run_navigation_sequence(dynamic_tone, "400:50 30", ACTION_COMPLETE) &&
        !std::strcmp(alert_source, "400:50 30") && alert_volume == 0 && !alert_loop;
    ok = ok && run_navigation_sequence(dynamic_tone, "", ACTION_COMPLETE) &&
        !alert_source[0] && alert_volume == 0;
    ok = ok && run_navigation_sequence(dynamic_file, "siren", ACTION_COMPLETE) &&
        !std::strcmp(alert_source, "siren") && alert_loop;
    const unsigned before_sound = sound_requests;
    ok = ok && run_navigation_sequence(dynamic_file, "", ACTION_FAILED) && sound_requests == before_sound;
    ok = ok && run_navigation_sequence(R"({"op":"audio.alert.stop","args":{}})", nullptr, ACTION_COMPLETE) &&
        audio_stop_requests == 1 && audio_stop_owned_requests == 0;
    ok = ok && run_navigation_sequence(R"({"op":"audio.stop_all","args":{}})", nullptr, ACTION_COMPLETE) &&
        audio_stop_all_requests == 1;

    JsonDocument input;
    if (deserializeJson(input, R"([
        {"op":"audio.alert.tone","args":{"pattern":"600:100","volume":25}},
        {"op":"audio.alert.mp3","args":{"file":"alarm","volume":0}},
        {"op":"alarm.tone","args":{"pattern":"600:100","volume":25}},
        {"op":"alarm.mp3","args":{"file":"alarm","volume":0}},
        {"op":"audio.alert.stop","args":{}},
        {"op":"audio.stop_all","args":{}}
    ])")) return false;
    ActionStorage storage = {};
    ButtonAction actions[6] = {};
    {
        ActionStorageScope scope(&storage);
        for (unsigned index = 0; index < 6; ++index)
            action_parse(input[index].as<JsonObject>(), actions[index]);
    }
    ok = ok && !storage.failed &&
        action_dispatch_synchronous(actions[0], "alarm", outer_guard, 101, true) == ACTION_COMPLETE &&
        !std::strcmp(alert_source, "600:100") && !alert_loop && alert_alarm &&
        alert_guard == outer_guard && alert_generation == 101;
    ok = ok && action_dispatch_synchronous(actions[1], "alarm", outer_guard, 101, true) == ACTION_COMPLETE &&
        !std::strcmp(alert_source, "alarm") && !alert_loop && alert_alarm &&
        alert_guard == outer_guard && alert_generation == 101;
    ok = ok && action_dispatch_synchronous(actions[2], "alarm", outer_guard, 101, true) == ACTION_COMPLETE &&
        !std::strcmp(alert_source, "600:100") && alert_loop && alert_alarm &&
        alert_guard == outer_guard && alert_generation == 101;
    ok = ok && action_dispatch_synchronous(actions[3], "alarm", outer_guard, 101, true) == ACTION_COMPLETE &&
        !std::strcmp(alert_source, "alarm") && alert_loop && alert_alarm &&
        alert_guard == outer_guard && alert_generation == 101;
    ok = ok && action_dispatch_synchronous(actions[4], "alarm", outer_guard, 101, true) == ACTION_COMPLETE &&
        audio_stop_owned_requests == 1 && audio_stop_requests == 1 &&
        alert_guard == outer_guard && alert_generation == 101;
    ok = ok && action_dispatch_synchronous(actions[5], "alarm", outer_guard, 101, true) == ACTION_COMPLETE &&
        audio_stop_all_requests == 2 && audio_stop_owned_requests == 1;
    action_storage_clear(storage);
    screen_calls = saved_screen_calls;
    strlcpy(shown_screen, saved_screen, sizeof(shown_screen));
    return ok;
}

static bool dialog_and_display_operation_boundaries() {
    const unsigned saved_screen_calls = screen_calls;
    const unsigned saved_wide_calls = wide_calls;
    char saved_screen[sizeof(shown_screen)];
    char saved_wide_target[sizeof(wide_target)];
    strlcpy(saved_screen, shown_screen, sizeof(saved_screen));
    strlcpy(saved_wide_target, wide_target, sizeof(saved_wide_target));
    const int32_t saved_wide_number = wide_number;
    const int32_t saved_wide_enabled = wide_enabled;

    runtime_variable_set("dialog_message", "Resolved message");
    const char* notify = R"({"op":"dialog.notify","args":{"message":{"$template":"[var:dialog_message]"},"duration_ms":{"$template":"[event:value]"}}})";
    bool ok = run_navigation_sequence(notify, "1000", ACTION_COMPLETE) &&
        dialog_notify_calls == 1 && !std::strcmp(dialog_message, "Resolved message") &&
        dialog_duration == 1000;
    ok = ok && run_navigation_sequence(notify, "0", ACTION_COMPLETE) &&
        dialog_notify_calls == 2 && dialog_duration == 0;
    ok = ok && run_navigation_sequence(notify, "999", ACTION_FAILED) &&
        run_navigation_sequence(notify, "invalid", ACTION_FAILED) && dialog_notify_calls == 2;
    dialog_accepted = false;
    ok = ok && run_navigation_sequence(notify, "3000", ACTION_FAILED) && dialog_notify_calls == 3;
    dialog_accepted = true;

    const char* alert = R"({"op":"visual_alert.start","args":{"color":{"$template":"[event:value]"},"pattern":{"$template":"[var:alert_pattern]"},"period_ms":0,"intensity":0,"duration_ms":0}})";
    runtime_variable_set("alert_pattern", "blink");
    ok = ok && run_navigation_sequence(alert, "#1234aa", ACTION_COMPLETE) &&
        alert_show_calls == 1 && alert_wake_calls == 1 && alert_params.color == 0x1234aa &&
        alert_params.pattern == VA_PATTERN_BLINK && alert_params.period_ms == VA_DEFAULT_PERIOD_MS &&
        alert_params.intensity == VA_DEFAULT_INTENSITY && alert_params.duration_ms == 0;
    ok = ok && run_navigation_sequence(alert, "invalid", ACTION_FAILED) && alert_show_calls == 1;
    runtime_variable_set("alert_pattern", "invalid");
    ok = ok && run_navigation_sequence(alert, "#1234aa", ACTION_FAILED) && alert_show_calls == 1;
    ok = ok && run_navigation_sequence(R"({"op":"visual_alert.stop","args":{}})", nullptr, ACTION_COMPLETE) &&
        alert_stop_calls == 1 && alert_wake_calls == 1;

    const char* refresh = R"({"op":"display.refresh_full","args":{}})";
    ok = ok && run_navigation_sequence(refresh, nullptr, ACTION_COMPLETE) && refresh_calls == 1;
    refresh_result = DISPLAY_REFRESH_UNAVAILABLE;
    ok = ok && run_navigation_sequence(refresh, nullptr, ACTION_FAILED) && refresh_calls == 2;
    refresh_result = DISPLAY_REFRESH_FAILED;
    ok = ok && run_navigation_sequence(refresh, nullptr, ACTION_FAILED) && refresh_calls == 3;
    refresh_result = DISPLAY_REFRESH_QUEUED;

    JsonDocument input;
    if (deserializeJson(input, R"([
        {"op":"dialog.confirm","args":{"message":{"$template":"[var:dialog_message]"},"approve_text":{"$template":"[var:approve]"},"cancel_text":"Cancel","timeout_ms":{"$template":"[event:value]"}}},
        {"op":"test.wide","args":{"target":"after confirm","number":{"$template":"[event:value]"},"enabled":true}}
    ])")) return false;
    runtime_variable_set("approve", "Proceed");
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input[0].as<JsonObject>(), actions[0]);
        action_parse(input[1].as<JsonObject>(), actions[1]);
    }
    ok = ok && !storage.failed && action_operation_for_action(actions[0]) &&
        action_operation_for_action(actions[1]);
    char event_value[] = "1000";
    ActionEventData event = {event_value, nullptr, BINDING_VALUE_KIND_UNKNOWN};
    ActionResult result = ACTION_FAILED;
    uint32_t reserved[ACTION_CONTINUATION_SLOTS] = {};
    for (uint32_t& token : reserved)
        ok = action_continuation_begin(nullptr, 0, "full", &token) && ok;
    {
        EventBindingScope scope(&event);
        if (ok) result = action_list_dispatch_result(actions, 2, "full",
                                                     ACTION_CONTINUATION_OWNER_LVGL, true);
    }
    ok = ok && result == ACTION_FAILED && dialog_confirm_calls == 0 &&
        wide_calls == saved_wide_calls;
    for (uint32_t token : reserved) action_continuation_release(token);
    {
        EventBindingScope scope(&event);
        if (ok) result = action_list_dispatch_result(actions, 2, "confirm",
                                                     ACTION_CONTINUATION_OWNER_LVGL, true);
    }
    const uint32_t approved = dialog_token;
    ok = ok && result == ACTION_PENDING && dialog_confirm_calls == 1 && approved &&
        !std::strcmp(dialog_content.message, "Resolved message") &&
        !std::strcmp(dialog_content.approve_text, "Proceed") &&
        !std::strcmp(dialog_content.cancel_text, "Cancel") && dialog_content.timeout_ms == 1000 &&
        wide_calls == saved_wide_calls;
    std::memcpy(event_value, "2000", sizeof(event_value));
    ok = ok && action_continuation_complete(approved, true);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    ok = ok && wide_calls == saved_wide_calls + 1 && wide_number == 1000 &&
        !std::strcmp(wide_target, "after confirm");

    {
        EventBindingScope scope(&event);
        if (ok) result = action_list_dispatch_result(actions, 2, "cancel",
                                                     ACTION_CONTINUATION_OWNER_LVGL, true);
    }
    const uint32_t cancelled = dialog_token;
    ok = ok && result == ACTION_PENDING && dialog_confirm_calls == 2 && cancelled;
    action_continuation_release(cancelled);
    ok = ok && !action_continuation_complete(cancelled, true);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    ok = ok && wide_calls == saved_wide_calls + 1;

    dialog_accepted = false;
    {
        EventBindingScope scope(&event);
        if (ok) result = action_list_dispatch_result(actions, 2, "reject",
                                                     ACTION_CONTINUATION_OWNER_LVGL, true);
    }
    ok = ok && result == ACTION_FAILED && dialog_confirm_calls == 3 &&
        !action_continuation_complete(dialog_token, true) && wide_calls == saved_wide_calls + 1;
    dialog_accepted = true;
    std::memcpy(event_value, "0999", sizeof(event_value));
    {
        EventBindingScope scope(&event);
        if (ok) result = action_list_dispatch_result(actions, 2, "invalid",
                                                     ACTION_CONTINUATION_OWNER_LVGL, true);
    }
    ok = ok && result == ACTION_FAILED && dialog_confirm_calls == 3 && wide_calls == saved_wide_calls + 1;
    action_storage_clear(storage);
    screen_calls = saved_screen_calls;
    wide_calls = saved_wide_calls;
    wide_number = saved_wide_number;
    wide_enabled = saved_wide_enabled;
    strlcpy(shown_screen, saved_screen, sizeof(shown_screen));
    strlcpy(wide_target, saved_wide_target, sizeof(wide_target));
    return ok;
}

static bool run_messaging_sequence(const ButtonAction* actions, const char* event_value,
                                   ActionResult expected) {
    const unsigned before = screen_calls;
    ActionEventData event = {event_value, nullptr, BINDING_VALUE_KIND_UNKNOWN};
    EventBindingScope scope(&event);
    return action_list_dispatch_result(actions, 2, "messaging") == expected &&
        screen_calls == before + (expected == ACTION_COMPLETE ? 1u : 0u);
}

static bool messaging_operation_boundaries() {
    const unsigned saved_screen_calls = screen_calls;
    char saved_screen[sizeof(shown_screen)];
    strlcpy(saved_screen, shown_screen, sizeof(saved_screen));

    JsonDocument input;
    if (deserializeJson(input, R"([
        {"op":"mqtt.publish","args":{"topic":{"$template":"[var:topic]"},"payload":{"$template":"[event:value]"}}},
        {"op":"bthome.send","args":{"button":{"$template":"[var:button]"},"event":{"$template":"[event:value]"}}},
        {"op":"screen.open","args":{"target":"pad_1"}}
    ])")) return false;
    ActionStorage storage = {};
    ButtonAction mqtt[2] = {};
    ButtonAction bthome_actions[2] = {};
    runtime_variable_set("topic", "home/before");
    runtime_variable_set("button", "1");
    {
        ActionStorageScope scope(&storage);
        action_parse(input[0].as<JsonObject>(), mqtt[0]);
        action_parse(input[1].as<JsonObject>(), bthome_actions[0]);
        action_parse(input[2].as<JsonObject>(), mqtt[1]);
        action_parse(input[2].as<JsonObject>(), bthome_actions[1]);
    }
    bool ok = !storage.failed && action_operation_for_action(mqtt[0]) &&
        action_operation_for_action(bthome_actions[0]) && action_operation_for_action(mqtt[1]);
    runtime_variable_set("topic", "home/kitchen");
    ok = ok && run_messaging_sequence(mqtt, "on", ACTION_COMPLETE) &&
        mqtt_manager.calls == 1 && !std::strcmp(mqtt_manager.topic, "home/kitchen") &&
        !std::strcmp(mqtt_manager.payload, "on") && !mqtt_manager.retained;
    runtime_variable_set("topic", "home/other");
    mqtt_manager.accepted = false;
    ok = ok && run_messaging_sequence(mqtt, "off", ACTION_FAILED) &&
        mqtt_manager.calls == 2 && !std::strcmp(mqtt_manager.topic, "home/other") &&
        !std::strcmp(mqtt_manager.payload, "off");
    mqtt_manager.accepted = true;
    runtime_variable_set("topic", "");
    ok = ok && run_messaging_sequence(mqtt, "ignored", ACTION_FAILED) &&
        mqtt_manager.calls == 2;

    runtime_variable_set("button", "10");
    ok = ok && run_messaging_sequence(bthome_actions, "double_press", ACTION_COMPLETE) &&
        bthome_calls == 1 && bthome_button == 10 && bthome_event == bthome::double_press;
    runtime_variable_set("button", "2");
    bthome_accepted = false;
    ok = ok && run_messaging_sequence(bthome_actions, "long_press", ACTION_FAILED) &&
        bthome_calls == 2 && bthome_button == 2 && bthome_event == bthome::long_press;
    bthome_accepted = true;
    runtime_variable_set("button", "11");
    ok = ok && run_messaging_sequence(bthome_actions, "press", ACTION_FAILED) && bthome_calls == 2;
    runtime_variable_set("button", "1");
    ok = ok && run_messaging_sequence(bthome_actions, "invalid", ACTION_FAILED) && bthome_calls == 2;

    action_storage_clear(storage);
    screen_calls = saved_screen_calls;
    strlcpy(shown_screen, saved_screen, sizeof(shown_screen));
    return ok;
}

static bool navigation_operation_boundaries() {
    const unsigned saved_screen_calls = screen_calls;
    char saved_screen[sizeof(shown_screen)];
    strlcpy(saved_screen, shown_screen, sizeof(saved_screen));
    const char* cycle = R"({"op":"pad.cycle","args":{"direction":{"$template":"[event:value]"},"wrap":false,"excluded_pads":"2,5"}})";
    if (!run_navigation_sequence(cycle, "previous", ACTION_COMPLETE) ||
        pad_cycle_calls != 1 || cycle_direction != -1 || cycle_wrap ||
        cycle_exclusions != ((1u << 1) | (1u << 4))) return false;
    if (!run_navigation_sequence(cycle, "sideways", ACTION_FAILED) || pad_cycle_calls != 1)
        return false;
    const char* exclusions = R"({"op":"pad.cycle","args":{"direction":"next","wrap":true,"excluded_pads":{"$template":"[event:value]"}}})";
    if (!run_navigation_sequence(exclusions, "2,no", ACTION_FAILED) || pad_cycle_calls != 1)
        return false;
    const char* wrap = R"({"op":"pad.cycle","args":{"direction":"next","wrap":{"$template":"[event:value]"},"excluded_pads":""}})";
    if (!run_navigation_sequence(wrap, "true", ACTION_COMPLETE) ||
        pad_cycle_calls != 2 || !cycle_wrap ||
        !run_navigation_sequence(wrap, "maybe", ACTION_FAILED) || pad_cycle_calls != 2)
        return false;
    navigation_available = false;
    const bool cycle_rejected = run_navigation_sequence(cycle, "next", ACTION_FAILED) &&
        pad_cycle_calls == 3;
    navigation_available = true;
    if (!cycle_rejected) return false;

    const char* button = R"({"op":"button.navigate","args":{"command":{"$template":"[event:value]"}}})";
    if (!run_navigation_sequence(button, "next", ACTION_COMPLETE) ||
        button_navigation_calls != 1 || std::strcmp(navigation_command, "next") ||
        !run_navigation_sequence(button, "invalid", ACTION_FAILED) || button_navigation_calls != 1)
        return false;
    navigation_available = false;
    const bool button_rejected = run_navigation_sequence(button, "tap", ACTION_FAILED) &&
        button_navigation_calls == 2;
    navigation_available = true;
    if (!button_rejected) return false;

    const char* list = R"({"op":"list.navigate","args":{"target":{"$template":"[event:value]"},"command":"next_page"}})";
    if (!run_navigation_sequence(list, "notes", ACTION_COMPLETE) ||
        list_navigation_calls != 1 || std::strcmp(navigation_target, "notes") ||
        std::strcmp(navigation_command, "next_page") ||
        !run_navigation_sequence(list, "notes/bad", ACTION_FAILED) || list_navigation_calls != 1)
        return false;
    list_capture_available = false;
    const bool list_rejected = run_navigation_sequence(list, "notes", ACTION_FAILED) &&
        list_navigation_calls == 1;
    list_capture_available = true;
    if (!list_rejected) return false;

    const char* next = R"({"op":"image.next","args":{}})";
    const char* previous = R"({"op":"image.previous","args":{}})";
    if (!run_navigation_sequence(next, nullptr, ACTION_COMPLETE) ||
        !run_navigation_sequence(previous, nullptr, ACTION_COMPLETE) ||
        image_next_calls != 1 || image_previous_calls != 1) return false;
    image_available = false;
    const bool images_rejected = run_navigation_sequence(next, nullptr, ACTION_FAILED) &&
        run_navigation_sequence(previous, nullptr, ACTION_FAILED) &&
        image_next_calls == 2 && image_previous_calls == 2;
    image_available = true;
    screen_calls = saved_screen_calls;
    strlcpy(shown_screen, saved_screen, sizeof(shown_screen));
    return images_rejected;
}

static bool operation_boundaries() {
    JsonDocument document;
    ActionStorage storage = {};
    ButtonAction action = {};
    if (deserializeJson(document, R"({"op":"screen.open","args":{"target":{"$template":"[item:id]"}}})"))
        return false;
    {
        ActionStorageScope scope(&storage);
        action_parse(document.as<JsonObject>(), action);
    }
    ListSourceSnapshot* row = ListSourceSnapshot::create();
    JsonDocument item;
    item["id"] = "pad_2";
    if (!row || row->append(item.as<JsonObjectConst>()) || row->seal()) {
        if (row) row->release();
        action_storage_clear(storage);
        return false;
    }
    bool ok;
    {
        ItemBindingScope item_scope(row, 0);
        ok = action_list_dispatch_result(&action, 1, "screen", ACTION_CONTINUATION_OWNER_LVGL, true)
                 == ACTION_COMPLETE && screen_calls == 1 && !std::strcmp(shown_screen, "pad_2");
    }
    row->release();
    ok = ok && action_list_dispatch_result(&action, 1, "screen", ACTION_CONTINUATION_OWNER_LVGL, true)
                    == ACTION_FAILED && screen_calls == 1;
    action_storage_clear(storage);
    document.clear();
    if (deserializeJson(document, R"({"op":"timer.adjust","args":{"timer":2,"seconds":{"$template":"[expr:[event:step]*60]"}}})"))
        return false;
    {
        ActionStorageScope scope(&storage);
        action_parse(document.as<JsonObject>(), action);
    }
    ActionEventData event = {nullptr, "-2", BINDING_VALUE_KIND_UNKNOWN};
    {
        EventBindingScope event_scope(&event);
        ok = ok && action_list_dispatch_result(&action, 1, "timer", ACTION_CONTINUATION_OWNER_LVGL, true)
                       == ACTION_COMPLETE && timer_calls == 1 && last_timer.timer_id == 2 &&
             !std::strcmp(last_timer.timer_command, "adjust") && !std::strcmp(last_timer.timer_value, "-120");
    }
    ok = ok && action_list_dispatch_result(&action, 1, "timer", ACTION_CONTINUATION_OWNER_LVGL, true)
                    == ACTION_FAILED && timer_calls == 1;
    ButtonAction sequence[2] = {};
    sequence[1] = action;
    document.clear();
    if (deserializeJson(document, R"({"op":"screen.open","args":{"target":"missing"}})")) {
        action_storage_clear(storage);
        return false;
    }
    {
        ActionStorageScope scope(&storage);
        action_parse(document.as<JsonObject>(), sequence[0]);
    }
    {
        EventBindingScope event_scope(&event);
        ok = ok && action_list_dispatch_result(sequence, 2, "failure", ACTION_CONTINUATION_OWNER_LVGL, true)
                       == ACTION_FAILED && screen_calls == 2 && timer_calls == 1;
    }
    action_storage_clear(storage);
    return ok;
}

static bool back_operation_boundaries() {
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"screen.back","args":{}})")) return false;
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    const unsigned before = back_calls;
    bool ok = !storage.failed && !action_operation_validate_json(input.as<JsonObjectConst>()) &&
        action_list_dispatch_result(&action, 1, "back") == ACTION_COMPLETE &&
        back_calls == before + 1;
    back_available = false;
    ok = ok && action_list_dispatch_result(&action, 1, "back") == ACTION_COMPLETE &&
        back_calls == before + 2;
    back_available = true;
    action_storage_clear(storage);
    input["args"]["unexpected"] = 1;
    return ok && action_operation_validate_json(input.as<JsonObjectConst>()) &&
        back_calls == before + 2;
}

static bool music_operation_boundaries() {
    const struct { const char* id; MusicCommand command; } cases[] = {
        {"music.play_pause", MUSIC_COMMAND_PLAY_PAUSE},
        {"music.next", MUSIC_COMMAND_NEXT},
        {"music.previous", MUSIC_COMMAND_PREVIOUS},
        {"music.stop", MUSIC_COMMAND_STOP},
    };
    bool ok = true;
    for (const auto& test : cases) {
        JsonDocument input;
        input["op"] = test.id;
        input["args"].to<JsonObject>();
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        const unsigned before = music_requests;
        ok = ok && !storage.failed && !action_operation_validate_json(input.as<JsonObjectConst>()) &&
            action_dispatch_synchronous(action, "music", outer_guard, 101) == ACTION_COMPLETE &&
            music_requests == before + 1 && last_music_command == test.command &&
            music_guard == outer_guard && music_generation == 101;
        music_result = AUDIO_MUSIC_SUBMIT_BUSY;
        ok = ok && action_list_dispatch_result(&action, 1, "music") == ACTION_FAILED &&
            music_requests == before + 2;
        music_result = AUDIO_MUSIC_SUBMIT_QUEUED;
        action_storage_clear(storage);
    }
    return ok;
}

static bool wifi_operation_boundaries() {
    JsonDocument document;
    if (deserializeJson(document, R"json([
        {"op":"wifi.station.set_enabled","args":{"enabled":{"$template":"[expr:[event:value]==\"aan\"?\"true\":[event:value]==\"uit\"?\"false\":\"invalid\"]"}}},
        {"op":"test.wide","args":{"target":"suffix","number":1,"enabled":true}}
    ])json")) return false;
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(document[0].as<JsonObject>(), actions[0]);
        action_parse(document[1].as<JsonObject>(), actions[1]);
    }
    bool ok = !storage.failed;
    for (const char* value : {"aan", "uit", "unknown"}) {
        const unsigned before_wifi = wifi_requests;
        const unsigned before_suffix = wide_calls;
        ActionEventData event = {value, nullptr, BINDING_VALUE_KIND_TEXT};
        EventBindingScope scope(&event);
        const ActionResult result = action_list_dispatch_result(actions, 2, "wifi",
            ACTION_CONTINUATION_OWNER_LVGL, true);
        const bool valid = std::strcmp(value, "unknown") != 0;
        ok = ok && result == (valid ? ACTION_COMPLETE : ACTION_FAILED) &&
             wifi_requests == before_wifi + (valid ? 1U : 0U) &&
             wide_calls == before_suffix + (valid ? 1U : 0U);
        if (valid) ok = ok && wifi_command == (!std::strcmp(value, "aan")
            ? WifiNetworkCommand::Connect : WifiNetworkCommand::Disconnect);
    }
    wifi_admitted = false;
    {
        const unsigned before_suffix = wide_calls;
        ActionEventData event = {"aan", nullptr, BINDING_VALUE_KIND_TEXT};
        EventBindingScope scope(&event);
        ok = ok && action_list_dispatch_result(actions, 2, "wifi",
            ACTION_CONTINUATION_OWNER_LVGL, true) == ACTION_FAILED && wide_calls == before_suffix;
    }
    wifi_admitted = true;
    action_storage_clear(storage);

    struct NetworkCase {
        const char* json;
        WifiNetworkCommand command;
    };
    const NetworkCase cases[] = {
        {R"({"op":"wifi.ap.set_enabled","args":{"enabled":true}})", WifiNetworkCommand::EnableAp},
        {R"({"op":"wifi.ap.set_enabled","args":{"enabled":false}})", WifiNetworkCommand::DisableAp},
        {R"({"op":"wifi.radio.turn_off","args":{}})", WifiNetworkCommand::TurnOff},
    };
    for (const auto& test : cases) {
        document.clear();
        if (deserializeJson(document, test.json)) return false;
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(document.as<JsonObject>(), action);
        }
        const unsigned before = wifi_requests;
        ok = ok && !storage.failed && !action_operation_validate_json(document.as<JsonObjectConst>()) &&
             action_list_dispatch_result(&action, 1, "wifi") == ACTION_COMPLETE &&
             wifi_requests == before + 1 && wifi_command == test.command;
        wifi_admitted = false;
        ok = ok && action_list_dispatch_result(&action, 1, "wifi") == ACTION_FAILED &&
             wifi_requests == before + 2;
        wifi_admitted = true;
        action_storage_clear(storage);
    }
    document.clear();
    if (deserializeJson(document, R"({"op":"wifi.ap.set_enabled","args":{"enabled":"true"}})")) return false;
    ok = ok && action_operation_validate_json(document.as<JsonObjectConst>());
    return ok;
}

static bool brightness_operation_boundaries() {
    const struct { const char* json; uint8_t expected; } cases[] = {
        {R"({"op":"display.brightness.set","args":{"percent":0}})", 5},
        {R"({"op":"display.brightness.set","args":{"percent":80}})", 80},
        {R"({"op":"display.brightness.adjust","args":{"percent":2147483647}})", 100},
        {R"({"op":"display.brightness.adjust","args":{"percent":-2147483648}})", 5},
    };
    bool ok = true;
    for (const auto& test : cases) {
        JsonDocument input;
        if (deserializeJson(input, test.json)) return false;
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        const unsigned before = brightness_requests;
        ok = ok && !storage.failed && action_list_dispatch_result(&action, 1, "brightness") == ACTION_COMPLETE &&
             brightness_requests == before + 1 && test_brightness == test.expected;
        action_storage_clear(storage);
    }
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"display.brightness.set","args":{"percent":{"$template":"[var:missing_brightness]"}}})"))
        return false;
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    const unsigned before = brightness_requests;
    ok = ok && action_list_dispatch_result(&action, 1, "brightness") == ACTION_FAILED &&
         brightness_requests == before;
    action_storage_clear(storage);
    return ok;
}

static bool volume_operation_boundaries() {
    const struct { const char* json; uint8_t expected; } cases[] = {
        {R"({"op":"audio.volume.set","args":{"percent":30}})", 30},
        {R"({"op":"audio.volume.adjust","args":{"percent":2147483647}})", 100},
        {R"({"op":"audio.volume.adjust","args":{"percent":-2147483648}})", 0},
    };
    bool ok = true;
    for (const auto& test : cases) {
        JsonDocument input;
        if (deserializeJson(input, test.json)) return false;
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        const unsigned before = volume_requests;
        ok = ok && !storage.failed && action_list_dispatch_result(&action, 1, "volume") == ACTION_COMPLETE &&
             volume_requests == before + 1 && test_volume == test.expected;
        action_storage_clear(storage);
    }
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"audio.volume.set","args":{"percent":{"$template":"[var:missing_volume]"}}})"))
        return false;
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    const unsigned before = volume_requests;
    ok = ok && action_list_dispatch_result(&action, 1, "volume") == ACTION_FAILED &&
         volume_requests == before;
    action_storage_clear(storage);
    return ok;
}

static unsigned long test_now_ms = 0;
static bool delay_operation_retains_item() {
    JsonDocument document;
    if (deserializeJson(document, R"([
        {"op":"delay.wait","args":{"duration_ms":100}},
        {"op":"screen.open","args":{"target":{"$template":"[pad:selected]"}}}
    ])")) return false;
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(document[0].as<JsonObject>(), actions[0]);
        action_parse(document[1].as<JsonObject>(), actions[1]);
    }
    ListSourceSnapshot* row = ListSourceSnapshot::create();
    JsonDocument item;
    item["id"] = "pad_4";
    if (!row || row->append(item.as<JsonObjectConst>()) || row->seal()) {
        if (row) row->release();
        action_storage_clear(storage);
        return false;
    }
    const unsigned before = screen_calls;
    ActionResult result;
    PadBinding origin = {};
    strlcpy(origin.name, "selected", sizeof(origin.name));
    strlcpy(origin.value, "[item:id]", sizeof(origin.value));
    {
        ItemBindingScope scope(row, 0);
        result = action_list_dispatch_result(actions, 2, "delay", ACTION_CONTINUATION_OWNER_LVGL, true,
                                             &origin, 1);
    }
    strlcpy(origin.value, "pad_7", sizeof(origin.value));
    row->release();
    action_storage_clear(storage);
    if (result != ACTION_PENDING || screen_calls != before) return false;
    test_now_ms += 99;
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    if (screen_calls != before) return false;
    ++test_now_ms;
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    if (screen_calls != before + 1 || std::strcmp(shown_screen, "pad_4")) return false;

    ButtonAction direct = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(document[1].as<JsonObject>(), direct);
    }
    pad_binding_set_bindings(&origin, 1);
    const ActionResult empty_origin = action_list_dispatch_result(
        &direct, 1, "empty-origin", ACTION_CONTINUATION_OWNER_LVGL, true, nullptr, 0);
    const bool unchanged = screen_calls == before + 1;
    const ActionResult active_origin = action_list_dispatch_result(
        &direct, 1, "active-origin", ACTION_CONTINUATION_OWNER_LVGL, true);
    pad_binding_set_bindings(nullptr, 0);
    action_storage_clear(storage);
    return unchanged && empty_origin == ACTION_FAILED && active_origin == ACTION_COMPLETE &&
           screen_calls == before + 2 && !std::strcmp(shown_screen, "pad_7");
}

static bool timer_operation_commands() {
    struct TimerCase {
        const char* operation;
        const char* command;
        const char* mode;
        const char* value;
        int seconds;
    };
    const TimerCase cases[] = {
        {"timer.adjust", "adjust", "", "-12", -12},
        {"timer.set", "set", "", "42", 42},
        {"timer.start_up", "start", "up", "", 0},
        {"timer.start_down", "start", "down", "42", 42},
        {"timer.toggle_up", "toggle", "up", "", 0},
        {"timer.toggle_down", "toggle", "down", "42", 42},
        {"timer.stop", "stop", "", "", 0},
        {"timer.pause", "pause", "", "", 0},
        {"timer.resume", "resume", "", "", 0},
        {"timer.reset", "reset", "", "", 0},
    };
    bool ok = true;
    for (const TimerCase& test : cases) {
        JsonDocument document;
        document["op"] = test.operation;
        document["args"]["timer"] = 2;
        if (test.value[0]) document["args"]["seconds"] = test.seconds;
        if (action_operation_validate_json(document.as<JsonObjectConst>(), false)) return false;
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(document.as<JsonObject>(), action);
        }
        const unsigned before = timer_calls;
        ok = ok && !storage.failed && action_dispatch(action, "timer") == ACTION_COMPLETE &&
             timer_calls == before + 1 && last_timer.timer_id == 2 &&
             !std::strcmp(last_timer.timer_command, test.command) &&
             !std::strcmp(last_timer.timer_mode, test.mode) &&
             !std::strcmp(last_timer.timer_value, test.value);
        timer_command_allowed = false;
        ok = ok && action_dispatch(action, "timer") == ACTION_FAILED;
        timer_command_allowed = true;
        action_storage_clear(storage);
    }
    JsonDocument invalid;
    invalid["op"] = "timer.start_down";
    invalid["args"]["timer"] = 1;
    invalid["args"]["seconds"] = 0;
    return ok && action_operation_validate_json(invalid.as<JsonObjectConst>(), false);
}

static int32_t normalized_boolean = -1;
static char normalized_enum[16] = {};
static ActionResult capture_typed_operation(const ActionResolvedArgument* args, const char*, uint32_t) {
    normalized_boolean = args[0].integer;
    strlcpy(normalized_enum, args[1].text, sizeof(normalized_enum));
    return ACTION_COMPLETE;
}
static const char* const typed_choices[] = {"on", "off"};
static const ActionArgumentDef typed_arguments[] = {
    {"enabled", "Enabled", ACTION_ARGUMENT_BOOLEAN, 0, 0, 0, true, "", "toggle"},
    {"mode", "Mode", ACTION_ARGUMENT_ENUM, 0, 0, 15, true, "", "select", typed_choices, 2}
};
static const ActionOperationDef typed_operations[] = {
    {"test.typed", "Typed", typed_arguments, 2, capture_typed_operation}
};
static const ActionTypeDef typed_type = {
    "test_typed", nullptr, nullptr,
    ACTION_EXECUTION_SYNC, true, false, typed_operations, 1
};

static bool typed_operation_validation() {
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"test.typed","args":{"enabled":true,"mode":"on"}})"))
        return false;
    JsonObject action_json = input.as<JsonObject>();
    if (action_operation_validate_json(action_json, false)) return false;
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(action_json, action);
    }
    JsonDocument serialized;
    action_to_json(action, serialized.to<JsonObject>());
    bool ok = serialized["args"]["enabled"].is<bool>() &&
              serialized["args"]["enabled"].as<bool>() &&
              !std::strcmp(serialized["args"]["mode"].as<const char*>(), "on") &&
              action_list_dispatch_result(&action, 1, "typed", ACTION_CONTINUATION_OWNER_LVGL, true)
                  == ACTION_COMPLETE && normalized_boolean == 1 && !std::strcmp(normalized_enum, "on");
    action_storage_clear(storage);
    action_json["args"]["enabled"] = "true";
    ok = ok && action_operation_validate_json(action_json, false);
    action_json["args"]["enabled"] = false;
    action_json["args"]["mode"] = "unknown";
    ok = ok && action_operation_validate_json(action_json, false);
    action_json["args"]["mode"] = "off";
    action_json["args"]["enabled"].to<JsonObject>()["$template"] = "[event:value]";
    {
        ActionStorageScope scope(&storage);
        action_parse(action_json, action);
    }
    ActionEventData event = {"false", nullptr, BINDING_VALUE_KIND_UNKNOWN};
    {
        EventBindingScope scope(&event);
        ok = ok && action_list_dispatch_result(&action, 1, "typed", ACTION_CONTINUATION_OWNER_LVGL, true)
                       == ACTION_COMPLETE && normalized_boolean == 0 && !std::strcmp(normalized_enum, "off");
    }
    event.value = "1";
    {
        EventBindingScope scope(&event);
        ok = ok && action_list_dispatch_result(&action, 1, "typed", ACTION_CONTINUATION_OWNER_LVGL, true)
                       == ACTION_FAILED;
    }
    action_storage_clear(storage);
    action_json["args"]["enabled"] = true;
    action_json["args"]["mode"].to<JsonObject>()["$template"] = "[event:step]";
    {
        ActionStorageScope scope(&storage);
        action_parse(action_json, action);
    }
    event.step = "off";
    {
        EventBindingScope scope(&event);
        ok = ok && action_list_dispatch_result(&action, 1, "typed", ACTION_CONTINUATION_OWNER_LVGL, true)
                       == ACTION_COMPLETE && normalized_boolean == 1 && !std::strcmp(normalized_enum, "off");
    }
    event.step = "unknown";
    {
        EventBindingScope scope(&event);
        ok = ok && action_list_dispatch_result(&action, 1, "typed", ACTION_CONTINUATION_OWNER_LVGL, true)
                       == ACTION_FAILED;
    }
    action_storage_clear(storage);
    return ok;
}

static bool boundary_received_item = false;
static bool nested_item_retained = false;
static bool accept_load = true;
static char requested_note[64] = {};
static bool accept_record = true;
static VoiceNoteCommand recorded_command = VoiceNoteCommand::Toggle;
static uint32_t recorded_token = 0;
static VoiceNotesProcessingStart processing_result = VoiceNotesProcessingStart::Accepted;
bool voice_notes_command(VoiceNoteCommand command, uint32_t token) {
    recorded_command = command;
    recorded_token = token;
    return accept_record;
}
VoiceNotesProcessingStart voice_notes_processing_start() { return processing_result; }
bool voice_notes_load_request(const char* id) {
    strlcpy(requested_note, id, sizeof(requested_note));
    return accept_load;
}
static unsigned cleared_notes = 0;
static char cleared_note[17] = {};
void voice_notes_loaded_clear_if_matches(const char* id) {
    ++cleared_notes;
    strlcpy(cleared_note, id, sizeof(cleared_note));
}
static LoopBridgeExec delete_exec = nullptr;
alignas(uint32_t) static unsigned char delete_job[LOOP_BRIDGE_CTX_BYTES];
static char deleted_note[17] = {};
static VoiceNoteFileStatus delete_result = VoiceNoteFileStatus::Ready;
static bool bridge_reject = false;
static bool bridge_consumer = false;
#if TEST_SPECIALIZED_MCP_BRIDGE
extern "C" LoopBridgeResult __wrap__Z19loop_bridge_enqueuePFvPKvPbPcmES0_m(LoopBridgeExec exec, const void* context, size_t size);
extern "C" bool __wrap__Z23loop_bridge_is_consumerv() { return bridge_consumer; }
LoopBridgeResult __wrap__Z19loop_bridge_enqueuePFvPKvPbPcmES0_m(LoopBridgeExec exec, const void* context, size_t size) {
#else
bool loop_bridge_is_consumer() { return bridge_consumer; }
LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec, const void* context, size_t size) {
#endif
    if (!exec || (!context && size) || size > sizeof(delete_job)) return LOOP_BRIDGE_INVALID;
    if (bridge_reject) return LOOP_BRIDGE_BUSY;
    delete_exec = exec;
    if (size) std::memcpy(delete_job, context, size);
    return LOOP_BRIDGE_OK;
}

static bool system_and_saved_level_operations() {
    struct Case { const char* op; unsigned* calls; };
    const Case cases[] = {
        {"device.restart", &reboot_requests},
        {"display.sleep", &sleep_requests},
        {"display.wake", &wake_requests},
    };
    for (const Case& test : cases) {
        JsonDocument input;
        input["op"] = test.op;
        input["args"].to<JsonObject>();
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        const unsigned before = *test.calls;
        bool ok = !storage.failed && !action_operation_validate_json(input.as<JsonObjectConst>()) &&
            action_dispatch(action, "system") == ACTION_COMPLETE && *test.calls == before + 1;
        action_storage_clear(storage);
        if (!ok) return false;
    }
    const Case saved_levels[] = {
        {"display.brightness.save", &save_brightness_requests},
        {"audio.volume.save", &save_volume_requests},
    };
    for (const Case& test : saved_levels) {
        JsonDocument input;
        input[0]["op"] = test.op;
        input[0]["args"].to<JsonObject>();
        input[1]["op"] = "test.wide";
        input[1]["args"]["target"] = "suffix";
        input[1]["args"]["number"] = 1;
        input[1]["args"]["enabled"] = true;
        ActionStorage storage = {};
        ButtonAction actions[2] = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input[0].as<JsonObject>(), actions[0]);
            action_parse(input[1].as<JsonObject>(), actions[1]);
        }
        test_brightness = test_volume = 37;
        const unsigned before = *test.calls;
        const unsigned before_suffix = wide_calls;
        bool ok = !storage.failed &&
            action_list_dispatch_result(actions, 2, "save", ACTION_CONTINUATION_OWNER_LVGL, true) == ACTION_PENDING &&
            *test.calls == before && wide_calls == before_suffix && delete_exec;
        test_brightness = test_volume = 75;
        bool completed = false;
        if (ok) delete_exec(delete_job, &completed, nullptr, 0);
        action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
        ok = ok && completed && *test.calls == before + 1 && saved_level == 37 &&
            wide_calls == before_suffix + 1;

        save_level_allowed = false;
        ok = ok && action_list_dispatch_result(actions, 2, "save", ACTION_CONTINUATION_OWNER_LVGL, true) == ACTION_PENDING;
        completed = true;
        if (ok) delete_exec(delete_job, &completed, nullptr, 0);
        action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
        save_level_allowed = true;
        ok = ok && !completed && *test.calls == before + 2 && wide_calls == before_suffix + 1;

        bridge_reject = true;
        ok = ok && action_list_dispatch_result(actions, 2, "save", ACTION_CONTINUATION_OWNER_LVGL, true) == ACTION_FAILED &&
            *test.calls == before + 2 && wide_calls == before_suffix + 1;
        bridge_reject = false;
        bridge_consumer = true;
        bridge_reject = true;
        ok = ok && action_list_dispatch_result(actions, 2, "save", ACTION_CONTINUATION_OWNER_LOOP, true) == ACTION_COMPLETE &&
            *test.calls == before + 3 && saved_level == 75 && wide_calls == before_suffix + 2;
        bridge_reject = false;
        bridge_consumer = false;
        action_storage_clear(storage);
        if (!ok) return false;
    }
    JsonDocument input;
    input["op"] = "device.restart_setup_ap";
    input["args"].to<JsonObject>();
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    bool ok = !storage.failed && action_dispatch(action, "system") == ACTION_COMPLETE && delete_exec;
    bridge_reject = true;
    ok = ok && action_dispatch(action, "system") == ACTION_FAILED;
    bridge_reject = false;
    action_storage_clear(storage);
    return ok;
}

static unsigned camera_requests = 0;
static CameraCaptureSaveTo camera_destination = CAMERA_CAPTURE_SAVE_BOTH;
static bool camera_success = true;
bool camera_capture_save(CameraCaptureSaveTo destination) {
    ++camera_requests;
    camera_destination = destination;
    return camera_success;
}

static bool camera_operation_boundaries() {
    const struct { const char* value; CameraCaptureSaveTo destination; } cases[] = {
        {"latest", CAMERA_CAPTURE_SAVE_LATEST},
        {"roll", CAMERA_CAPTURE_SAVE_ROLL},
        {"both", CAMERA_CAPTURE_SAVE_BOTH},
    };
    bool ok = true;
    for (const auto& test : cases) {
        JsonDocument input;
        input["op"] = "camera.capture";
        input["args"]["save_to"] = test.value;
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        const unsigned before = camera_requests;
        ok = ok && !storage.failed && !action_operation_validate_json(input.as<JsonObjectConst>()) &&
            action_list_dispatch_result(&action, 1, "camera", ACTION_CONTINUATION_OWNER_LVGL, true) == ACTION_PENDING;
        bool completed = false;
        if (ok) delete_exec(delete_job, &completed, nullptr, 0);
        action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
        ok = ok && completed && camera_requests == before + 1 && camera_destination == test.destination;
        bridge_reject = true;
        ok = ok && action_list_dispatch_result(&action, 1, "camera", ACTION_CONTINUATION_OWNER_LVGL, true) == ACTION_FAILED &&
            camera_requests == before + 1;
        bridge_reject = false;
        action_storage_clear(storage);
    }
    JsonDocument input;
    input["op"] = "camera.capture";
    input["args"]["save_to"]["$template"] = "invalid";
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    const unsigned before = camera_requests;
    ok = ok && !storage.failed &&
        action_list_dispatch_result(&action, 1, "camera", ACTION_CONTINUATION_OWNER_LVGL, true) == ACTION_FAILED &&
        camera_requests == before;
    action_storage_clear(storage);
    input["args"]["save_to"] = "invalid";
    ok = ok && action_operation_validate_json(input.as<JsonObjectConst>());
    input.clear();
    if (deserializeJson(input, R"([
        {"op":"camera.capture","args":{"save_to":"roll"}},
        {"op":"test.wide","args":{"target":"suffix","number":1,"enabled":true}}
    ])")) return false;
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input[0].as<JsonObject>(), actions[0]);
        action_parse(input[1].as<JsonObject>(), actions[1]);
    }
    const unsigned before_suffix = wide_calls;
    camera_success = false;
    ok = ok && !storage.failed &&
        action_list_dispatch_result(actions, 2, "camera", ACTION_CONTINUATION_OWNER_LVGL, true) == ACTION_PENDING;
    bool completed = true;
    if (ok) delete_exec(delete_job, &completed, nullptr, 0);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    camera_success = true;
    ok = ok && !completed && wide_calls == before_suffix;
    action_storage_clear(storage);
    return ok;
}

VoiceNoteFileStatus voice_notes_remove(const char* id) {
    strlcpy(deleted_note, id, sizeof(deleted_note));
    return delete_result;
}

static ActionResult inspect_nested_action(const ActionResolvedArgument*, const char*, uint32_t) {
    char value[32];
    binding_template_resolve("[item:id|none]", value, sizeof(value));
    nested_item_retained = std::strcmp(value, "item-1") == 0;
    return ACTION_COMPLETE;
}

static ActionResult dispatch_boundary(const ActionResolvedArgument* arguments, const char*, uint32_t) {
    boundary_received_item = std::strcmp(arguments[0].text, "item-1") == 0;
    JsonDocument document;
    document["op"] = "test.nested";
    document.createNestedObject("args");
    ButtonAction nested = make_test_operation(document.as<JsonObject>());
    return action_dispatch(nested, "nested");
}

static const ActionOperationDef nested_operations[] = {
    {"test.nested", "Nested", nullptr, 0, inspect_nested_action}
};
static const ActionTypeDef nested_action = {
    "test_nested", nullptr, nullptr, ACTION_EXECUTION_SYNC, true, false, nested_operations, 1
};
static const ActionArgumentDef boundary_arguments[] = {
    {"target", "Target", ACTION_ARGUMENT_STRING, 0, 0, 31, true, "[item:id]", "text"}
};
static const ActionOperationDef boundary_operations[] = {
    {"test.boundary", "Boundary", boundary_arguments, 1, dispatch_boundary}
};
static const ActionTypeDef boundary_action = {
    "test_boundary", nullptr, nullptr, ACTION_EXECUTION_SYNC, true, false, boundary_operations, 1
};

static uint32_t suspended_token = 0;
static char operation_target[32] = {};
static int32_t operation_seconds = 0;
static ActionResult suspend_action(const ActionResolvedArgument*, const char*, uint32_t token) {
    suspended_token = token;
    return token ? ACTION_PENDING : ACTION_FAILED;
}
static ActionResult capture_operation(const ActionResolvedArgument* args, const char*, uint32_t) {
    strlcpy(operation_target, args[0].text, sizeof(operation_target));
    operation_seconds = args[1].integer;
    return ACTION_COMPLETE;
}
static const ActionArgumentDef captured_arguments[] = {
    {"target", "Screen", ACTION_ARGUMENT_SCREEN, 0, 0, 31, true, "[item:id]", "screens"},
    {"seconds", "Seconds", ACTION_ARGUMENT_INTEGER, -1000, 1000, 0, true, "[event:step]", "number"},
};
static const ActionOperationDef captured_operations[] = {
    {"test.capture", "Capture", captured_arguments, 2, capture_operation}
};
static const ActionTypeDef capture_type = {
    "test_capture", nullptr, nullptr,
    ACTION_EXECUTION_SYNC, true, false, captured_operations, 1
};
static const ActionOperationDef suspend_operations[] = {
    {"test.suspend", "Suspend", nullptr, 0, suspend_action, ACTION_EXECUTION_PAUSABLE}
};
static const ActionTypeDef suspend_type = {
    "test_suspend", nullptr, nullptr,
    ACTION_EXECUTION_PAUSABLE, true, false, suspend_operations, 1
};

static unsigned typed_pause_calls = 0;
static uint32_t typed_pause_token = 0;
static ActionResult pause_operation(const ActionResolvedArgument*, const char*, uint32_t token) {
    ++typed_pause_calls;
    typed_pause_token = token;
    return ACTION_PENDING;
}
static const ActionOperationDef pause_operations[] = {
    {"test.pause", "Pause", nullptr, 0, pause_operation, ACTION_EXECUTION_PAUSABLE}
};
static const ActionTypeDef pause_type = {
    "test_pause", nullptr, nullptr,
    ACTION_EXECUTION_SYNC, true, false, pause_operations, 1
};

#if HAS_MCP
struct TrackedHaProbe {
    HaServicePayload payload = {};
    unsigned calls = 0;
    unsigned failures = 0;
    unsigned releases = 0;
    uint8_t index = 0;
};

static bool capture_tracked_ha(void* data, uint8_t index, const HaServicePayload& payload,
                               ActionResult* result) {
    auto* probe = static_cast<TrackedHaProbe*>(data);
    probe->payload = payload;
    probe->index = index;
    ++probe->calls;
    *result = ACTION_COMPLETE;
    return true;
}
static void fail_tracked_ha(void* data, uint8_t index) {
    auto* probe = static_cast<TrackedHaProbe*>(data);
    probe->index = index;
    ++probe->failures;
}
static void release_tracked_ha(void* data) {
    ++static_cast<TrackedHaProbe*>(data)->releases;
}

struct TrackedMcpProbe {
    HaServiceDelivery delivery;
    PadBinding binding;
    ActionResult dispatch_result = ACTION_FAILED;
};
static bool mcp_test_reserve(uint8_t count, uint32_t* id, void* data) {
    return static_cast<TrackedMcpProbe*>(data)->delivery.execution_reserve(count, 100, *id);
}
static void mcp_test_list(const ButtonAction*, uint8_t, void*) {}
static void mcp_test_set(uint32_t id, uint8_t result_index, uint8_t action_index,
                         const HaServicePayload& payload, void* data) {
    static_cast<TrackedMcpProbe*>(data)->delivery.execution_set_action(id, result_index,
                                                                        action_index, payload);
}
static HaServiceEnqueueResult mcp_test_enqueue(const HaServicePayload& payload, uint32_t id,
                                               uint8_t action_index, void* data) {
    return static_cast<TrackedMcpProbe*>(data)->delivery.enqueue(payload, id, action_index);
}
static void mcp_test_record(const HaServiceResult& result, void* data) {
    static_cast<TrackedMcpProbe*>(data)->delivery.execution_complete(result, 101);
}
static void mcp_test_observed(const ButtonAction* actions, uint8_t count,
                              const ActionRunObserver& observer, void* data) {
    auto* probe = static_cast<TrackedMcpProbe*>(data);
    probe->dispatch_result = action_list_dispatch_observed(
        actions, count, "MCP", ACTION_CONTINUATION_OWNER_LVGL, false,
        &probe->binding, 1, observer);
}
static const McpPressDispatchOps kMcpTestOps = {
    mcp_test_reserve, mcp_test_list, mcp_test_set,
    mcp_test_enqueue, mcp_test_record, mcp_test_observed
};

static bool observed_ha_preserves_context_and_resolves_live() {
    JsonDocument input;
    if (deserializeJson(input, R"([
        {"op":"test.pause","args":{}},
        {"op":"ha.service.call","args":{
            "entity_id":{"$template":"[pad:entity]"},
            "service":{"$template":"[var:service]"},
            "data_json":""
        }}
    ])")) return false;
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input[0].as<JsonObject>(), actions[0]);
        action_parse(input[1].as<JsonObject>(), actions[1]);
    }
    if (storage.failed) { action_storage_clear(storage); return false; }
    ListSourceSnapshot* item = ListSourceSnapshot::create();
    JsonDocument row;
    row["id"] = "one";
    row["entity"] = "light.kitchen";
    if (!item || item->append(row.as<JsonObjectConst>()) || item->seal()) {
        if (item) item->release();
        action_storage_clear(storage);
        return false;
    }
    PadBinding binding = {};
    strlcpy(binding.name, "entity", sizeof(binding.name));
    strlcpy(binding.value, "[item:entity]", sizeof(binding.value));
    runtime_variable_set("service", "turn_off");
    TrackedHaProbe probe;
    const ActionRunObserver observer = {
        &probe, capture_tracked_ha, fail_tracked_ha, release_tracked_ha
    };
    ActionResult result;
    {
        ItemBindingScope scope(item, 0);
        result = action_list_dispatch_observed(actions, 2, "tracked", ACTION_CONTINUATION_OWNER_LVGL,
                                               true, &binding, 1, observer);
    }
    item->release();
    strlcpy(binding.value, "light.changed", sizeof(binding.value));
    runtime_variable_set("service", "turn_on");
    const bool started = result == ACTION_PENDING && typed_pause_token && !probe.releases;
    const bool completed = action_continuation_complete(typed_pause_token, true);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    const bool resolved = probe.calls == 1 && probe.index == 1 && probe.failures == 0 &&
        probe.releases == 1 && !strcmp(probe.payload.entity_id, "light.kitchen") &&
        !strcmp(probe.payload.service, "turn_on") && !probe.payload.data_json[0];

    TrackedHaProbe cancelled;
    const ActionRunObserver cancel_observer = {
        &cancelled, capture_tracked_ha, fail_tracked_ha, release_tracked_ha
    };
    result = action_list_dispatch_observed(actions, 2, "cancel", ACTION_CONTINUATION_OWNER_LVGL,
                                           true, &binding, 1, cancel_observer);
    if (result == ACTION_PENDING) action_continuation_release(typed_pause_token);
    const bool cancelled_ok = result == ACTION_PENDING && !cancelled.calls && cancelled.releases == 1;
    runtime_variable_set("service", "Invalid");
    TrackedHaProbe invalid;
    const ActionRunObserver invalid_observer = {
        &invalid, capture_tracked_ha, fail_tracked_ha, release_tracked_ha
    };
    result = action_list_dispatch_observed(actions + 1, 1, "invalid", ACTION_CONTINUATION_OWNER_LVGL,
                                           true, &binding, 1, invalid_observer);
    const bool invalid_ok = result == ACTION_FAILED && !invalid.calls &&
        invalid.failures == 1 && invalid.index == 0 && invalid.releases == 1;

    TrackedMcpProbe mcp;
    strlcpy(mcp.binding.name, "entity", sizeof(mcp.binding.name));
    strlcpy(mcp.binding.value, "[item:entity]", sizeof(mcp.binding.value));
    ListSourceSnapshot* mcp_item = ListSourceSnapshot::create();
    if (!mcp_item || mcp_item->append(row.as<JsonObjectConst>()) || mcp_item->seal()) {
        if (mcp_item) mcp_item->release();
        action_storage_clear(storage);
        return false;
    }
    runtime_variable_set("service", "turn_off");
    uint32_t execution_id = 0;
    McpPressDispatchResult press_result;
    {
        ItemBindingScope scope(mcp_item, 0);
        press_result = mcp_press_button_dispatch(actions, 2, kMcpTestOps, &mcp,
                                                 &execution_id, &mcp);
    }
    mcp_item->release();
    strlcpy(mcp.binding.value, "light.changed", sizeof(mcp.binding.value));
    runtime_variable_set("service", "turn_on");
    HaExecutionSnapshot mcp_snapshot = {};
    bool mcp_ok = press_result == MCP_PRESS_DISPATCH_TRACKED && execution_id &&
        mcp.dispatch_result == ACTION_PENDING &&
        mcp.delivery.execution_snapshot(execution_id, 100, mcp_snapshot) == HA_EXECUTION_FOUND &&
        mcp_snapshot.state == HA_EXECUTION_PENDING &&
        mcp_snapshot.actions[0].action_index == 1 &&
        action_continuation_complete(typed_pause_token, true);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    HaServiceRequest queued = {};
    mcp_ok = mcp_ok && mcp.delivery.dequeue(queued) && queued.execution_id == execution_id &&
        queued.action_index == 1 && !strcmp(queued.payload.entity_id, "light.kitchen") &&
        !strcmp(queued.payload.service, "turn_on");
    HaServiceResult delivered = {};
    delivered.execution_id = execution_id;
    delivered.action_index = 1;
    delivered.status = HA_STATUS_SUCCESS;
    delivered.http_status = 200;
    strlcpy(delivered.entity_id, queued.payload.entity_id, sizeof(delivered.entity_id));
    strlcpy(delivered.service, queued.payload.service, sizeof(delivered.service));
    mcp_ok = mcp_ok && mcp.delivery.execution_complete(delivered, 102) &&
        mcp.delivery.execution_snapshot(execution_id, 103, mcp_snapshot) == HA_EXECUTION_FOUND &&
        mcp_snapshot.state == HA_EXECUTION_COMPLETED &&
        mcp_snapshot.actions[0].status == HA_STATUS_SUCCESS;

    uint32_t cancelled_id = 0;
    press_result = mcp_press_button_dispatch(actions, 2, kMcpTestOps, &mcp, &cancelled_id, &mcp);
    if (mcp.dispatch_result == ACTION_PENDING) action_continuation_release(typed_pause_token);
    mcp_ok = mcp_ok && press_result == MCP_PRESS_DISPATCH_TRACKED &&
        mcp.delivery.execution_snapshot(cancelled_id, 103, mcp_snapshot) == HA_EXECUTION_FOUND &&
        mcp_snapshot.state == HA_EXECUTION_COMPLETED &&
        mcp_snapshot.actions[0].status == HA_STATUS_CANCELLED;

    runtime_variable_set("service", "Invalid");
    uint32_t invalid_id = 0;
    press_result = mcp_press_button_dispatch(actions + 1, 1, kMcpTestOps, &mcp, &invalid_id, &mcp);
    mcp_ok = mcp_ok && press_result == MCP_PRESS_DISPATCH_TRACKED &&
        mcp.dispatch_result == ACTION_FAILED &&
        mcp.delivery.execution_snapshot(invalid_id, 103, mcp_snapshot) == HA_EXECUTION_FOUND &&
        mcp_snapshot.state == HA_EXECUTION_COMPLETED &&
        mcp_snapshot.actions[0].status == HA_STATUS_INVALID_REQUEST &&
        !mcp.delivery.dequeue(queued);

    runtime_variable_set("service", "turn_on");
    const HaServicePayload filler = {};
    for (unsigned index = 0; index < HA_SERVICE_QUEUE_CAPACITY; ++index)
        mcp_ok = mcp_ok && mcp.delivery.enqueue(filler) == HA_SERVICE_ACCEPTED;
    const ButtonAction repeated[] = {actions[1], actions[1]};
    uint32_t full_id = 0;
    press_result = mcp_press_button_dispatch(repeated, 2, kMcpTestOps, &mcp, &full_id, &mcp);
    mcp_ok = mcp_ok && press_result == MCP_PRESS_DISPATCH_TRACKED &&
        mcp.dispatch_result == ACTION_FAILED &&
        mcp.delivery.execution_snapshot(full_id, 103, mcp_snapshot) == HA_EXECUTION_FOUND &&
        mcp_snapshot.state == HA_EXECUTION_COMPLETED &&
        mcp_snapshot.action_count == 2 &&
        mcp_snapshot.actions[0].status == HA_STATUS_QUEUE_FULL &&
        mcp_snapshot.actions[1].status == HA_STATUS_CANCELLED;

    TrackedMcpProbe timed_out;
    timed_out.binding = mcp.binding;
    uint32_t timeout_id = 0;
    press_result = mcp_press_button_dispatch(actions, 2, kMcpTestOps, &timed_out,
                                             &timeout_id, &timed_out);
    const uint32_t previous_now = test_now_ms;
    test_now_ms += ACTION_CONTINUATION_TIMEOUT_MS;
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    test_now_ms = previous_now;
    mcp_ok = mcp_ok && press_result == MCP_PRESS_DISPATCH_TRACKED &&
        timed_out.dispatch_result == ACTION_PENDING &&
        timed_out.delivery.execution_snapshot(timeout_id, 103, mcp_snapshot) == HA_EXECUTION_FOUND &&
        mcp_snapshot.state == HA_EXECUTION_COMPLETED &&
        mcp_snapshot.actions[0].status == HA_STATUS_CANCELLED &&
        !timed_out.delivery.dequeue(queued);
    action_storage_clear(storage);
    if (!(started && completed && resolved && cancelled_ok && invalid_ok && mcp_ok))
        std::printf("tracked: started=%d completed=%d resolved=%d cancel=%d invalid=%d mcp=%d calls=%u failures=%u releases=%u index=%u entity=%s service=%s\n",
                    started, completed, resolved, cancelled_ok, invalid_ok, mcp_ok, probe.calls, probe.failures,
                    probe.releases, probe.index, probe.payload.entity_id, probe.payload.service);
    return started && completed && resolved && cancelled_ok && invalid_ok && mcp_ok;
}
#endif

static bool typed_continuation_contract() {
    JsonDocument input;
    if (deserializeJson(input, R"([
        {"op":"screen.open","args":{"target":"pad_1"}},
        {"op":"timer.adjust","args":{"timer":1,"seconds":60}},
        {"op":"test.pause","args":{}},
        {"op":"test.wide","args":{"target":"suffix","number":2,"enabled":false}}
    ])")) return false;
    ActionStorage storage = {};
    ButtonAction actions[4] = {};
    {
        ActionStorageScope scope(&storage);
        for (unsigned index = 0; index < 4; ++index)
            action_parse(input[index].as<JsonObject>(), actions[index]);
    }
    if (storage.failed) {
        action_storage_clear(storage);
        return false;
    }
    uint32_t tokens[ACTION_CONTINUATION_SLOTS] = {};
    bool ok = true;
    for (unsigned index = 0; index < ACTION_CONTINUATION_SLOTS; ++index)
        ok = action_continuation_begin(nullptr, 0, "full", &tokens[index]) && ok;
    const unsigned before_screen = screen_calls;
    const unsigned before_timer = timer_calls;
    ok = ok && action_list_dispatch_result(actions, 2, "full", ACTION_CONTINUATION_OWNER_LVGL, true)
                    == ACTION_COMPLETE &&
         screen_calls == before_screen + 1 && timer_calls == before_timer + 1;
    ok = ok && action_list_dispatch_result(actions + 2, 2, "full", ACTION_CONTINUATION_OWNER_LVGL, true)
                    == ACTION_FAILED && typed_pause_calls == 0;
    for (uint32_t token : tokens) action_continuation_release(token);

    ok = ok && action_dispatch(actions[2], "no token") == ACTION_FAILED &&
         action_dispatch_synchronous(actions[2], "sync", nullptr, 0) == ACTION_FAILED &&
         typed_pause_calls == 0;
    const unsigned before_suffix = wide_calls;
    ok = ok && action_list_dispatch_result(actions + 2, 2, "pause", ACTION_CONTINUATION_OWNER_LVGL, true)
                    == ACTION_PENDING && typed_pause_calls == 1 && typed_pause_token;
    const uint32_t completed = typed_pause_token;
    ok = ok && action_continuation_complete(completed, true) &&
         !action_continuation_complete(completed, true);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    ok = ok && wide_calls == before_suffix + 1 && !std::strcmp(wide_target, "suffix") &&
         !action_continuation_complete(completed, true);

    ok = ok && action_list_dispatch_result(actions + 2, 2, "cancel", ACTION_CONTINUATION_OWNER_LVGL, true)
                    == ACTION_PENDING && typed_pause_calls == 2;
    const uint32_t cancelled = typed_pause_token;
    action_continuation_release(cancelled);
    ok = ok && !action_continuation_complete(cancelled, true);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    ok = ok && wide_calls == before_suffix + 1;
    action_storage_clear(storage);
    return ok;
}

static bool synchronous_navigation_survives_replacement() {
    JsonDocument input;
    if (deserializeJson(input, R"([
        {"op":"screen.open","args":{"target":"pad_2"}},
        {"op":"timer.adjust","args":{"timer":2,"seconds":{"$template":"[event:step]"}}}
    ])")) return false;
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input[0].as<JsonObject>(), actions[0]);
        action_parse(input[1].as<JsonObject>(), actions[1]);
    }
    if (storage.failed) {
        action_storage_clear(storage);
        return false;
    }
    const unsigned before = timer_calls;
    replaced_storage = &storage;
    replaced_actions = actions;
    ActionEventData event = {nullptr, "42", BINDING_VALUE_KIND_UNKNOWN};
    ActionResult result;
    {
        EventBindingScope scope(&event);
        result = action_list_dispatch_result(actions, 2, "replacement", ACTION_CONTINUATION_OWNER_LVGL, true);
    }
    replaced_storage = nullptr;
    replaced_actions = nullptr;
    action_storage_clear(storage);
    return result == ACTION_COMPLETE && timer_calls == before + 1 &&
           last_timer.timer_id == 2 && !std::strcmp(last_timer.timer_value, "42");
}

static bool captured_run_survives_pause() {
    JsonDocument document;
    if (deserializeJson(document, R"({"op":"test.capture","args":{
        "target":{"$template":"[pad:selected]"},
        "seconds":{"$template":"[expr:[event:step]+[var:live]]"}
    }})")) return false;
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    JsonDocument suspend_document;
    suspend_document["op"] = "test.suspend";
    suspend_document.createNestedObject("args");
    actions[0] = make_test_operation(suspend_document.as<JsonObject>());
    {
        ActionStorageScope scope(&storage);
        action_parse(document.as<JsonObject>(), actions[1]);
    }
    if (storage.failed || !actions[1].type[0]) { action_storage_clear(storage); return false; }
    ListSourceSnapshot* row = ListSourceSnapshot::create();
    JsonDocument item;
    item["id"] = "001";
    if (!row || row->append(item.as<JsonObjectConst>()) || row->seal()) {
        if (row) row->release();
        action_storage_clear(storage);
        return false;
    }
    PadBinding binding = {};
    strlcpy(binding.name, "selected", sizeof(binding.name));
    strlcpy(binding.value, "[item:id]", sizeof(binding.value));
    char step[] = "2";
    ActionEventData event = {nullptr, step, BINDING_VALUE_KIND_UNKNOWN};
    runtime_variable_set("live", "3");
    ActionResult result;
    {
        ItemBindingScope item_scope(row, 0);
        EventBindingScope event_scope(&event);
        pad_binding_set_bindings(&binding, 1);
        fail_psram = true;
        const ActionResult failed = action_list_dispatch_result(
            actions, 2, "allocation failure", ACTION_CONTINUATION_OWNER_LVGL, true);
        fail_psram = false;
        if (failed != ACTION_FAILED || suspended_token) {
            row->release();
            action_storage_clear(storage);
            return false;
        }
        result = action_list_dispatch_result(actions, 2, "capture", ACTION_CONTINUATION_OWNER_LVGL, true);
    }
    row->release();
    action_storage_clear(storage);
    pad_binding_set_bindings(nullptr, 0);
    strlcpy(binding.value, "changed", sizeof(binding.value));
    step[0] = '9';
    runtime_variable_set("live", "5");
    if (result != ACTION_PENDING || !action_continuation_complete(suspended_token, true)) return false;
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    return !std::strcmp(operation_target, "001") && operation_seconds == 7;
}

static bool real_operations_survive_pause() {
    JsonDocument document;
    if (deserializeJson(document, R"([
        {"op":"screen.open","args":{"target":{"$template":"[pad:selected]"}}},
        {"op":"timer.adjust","args":{"timer":1,"seconds":{"$template":"[expr:[event:step]*60]"}}}
    ])")) return false;
    ActionStorage storage = {};
    ButtonAction actions[3] = {};
    JsonDocument suspend_document;
    suspend_document["op"] = "test.suspend";
    suspend_document.createNestedObject("args");
    actions[0] = make_test_operation(suspend_document.as<JsonObject>());
    {
        ActionStorageScope scope(&storage);
        action_parse(document[0].as<JsonObject>(), actions[1]);
        action_parse(document[1].as<JsonObject>(), actions[2]);
    }
    ListSourceSnapshot* row = ListSourceSnapshot::create();
    JsonDocument item;
    item["id"] = "pad_3";
    if (storage.failed || !row || row->append(item.as<JsonObjectConst>()) || row->seal()) {
        if (row) row->release();
        action_storage_clear(storage);
        return false;
    }
    PadBinding binding = {};
    strlcpy(binding.name, "selected", sizeof(binding.name));
    strlcpy(binding.value, "[item:id]", sizeof(binding.value));
    char step[] = "2";
    ActionEventData event = {nullptr, step, BINDING_VALUE_KIND_UNKNOWN};
    ActionResult result;
    {
        ItemBindingScope item_scope(row, 0);
        EventBindingScope event_scope(&event);
        pad_binding_set_bindings(&binding, 1);
        result = action_list_dispatch_result(actions, 3, "real operations", ACTION_CONTINUATION_OWNER_LVGL, true);
    }
    row->release();
    action_storage_clear(storage);
    pad_binding_set_bindings(nullptr, 0);
    strlcpy(binding.value, "changed", sizeof(binding.value));
    step[0] = '9';
    if (result != ACTION_PENDING || !action_continuation_complete(suspended_token, true)) return false;
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);
    return screen_calls == 3 && !std::strcmp(shown_screen, "pad_3") &&
           timer_calls == 2 && last_timer.timer_id == 1 && !std::strcmp(last_timer.timer_value, "120");
}
#endif

#if HAS_MQTT || HAS_HA_LIVE
#error Runtime variable action test requires network bindings disabled
#endif

void display_manager_lock_if_needed(bool* did_lock) {
#if HAS_DISPLAY
    *did_lock = !test_display_locked;
    if (*did_lock) {
        test_display_locked = true;
        ++test_display_locks;
    }
#else
    if (did_lock) *did_lock = false;
#endif
}

void display_manager_unlock_if_needed(bool did_lock) {
#if HAS_DISPLAY
    if (did_lock) test_display_locked = false;
#else
    (void)did_lock;
#endif
}
static unsigned binding_refreshes = 0;
void display_manager_request_binding_refresh() { ++binding_refreshes; }
void ha_service_execute() {}
#if HAS_MCP
HaServiceEnqueueResult ha_service_enqueue(const HaServicePayload&, uint32_t, uint8_t) {
    return HA_SERVICE_ACCEPTED;
}
bool ha_service_execute_immediate(const HaServicePayload&) { return true; }
#endif
extern "C" unsigned long millis() {
#if HAS_DISPLAY && HAS_PSRAM
    return test_now_ms;
#else
    return 0;
#endif
}

static ButtonAction set_variable_action(const char* name, const char* value) {
    JsonDocument document;
    document["op"] = "variable.set";
    JsonObject arguments = document.createNestedObject("args");
    arguments["name"] = name;
    if (strchr(value, '[')) arguments.createNestedObject("value")["$template"] = value;
    else arguments["value"] = value;
    return make_test_operation(document.as<JsonObject>());
}

static bool get_equals(const char* name, const char* expected) {
    char value[RUNTIME_VARIABLE_VALUE_CAPACITY] = {};
    return runtime_variable_get(name, value, sizeof(value)) == RUNTIME_VARIABLE_FOUND &&
           std::strcmp(value, expected) == 0;
}

#if TEST_STOPWATCH_OPERATIONS || TEST_STICKY_OPERATIONS
static bool board_request_accepted = true;
static uint32_t board_shutdown_token = 0;

#if TEST_STOPWATCH_OPERATIONS
#include "../src/boards/m5stack-stopwatch/stopwatch_hardware.h"
static StopWatchChargingMode board_charge_mode = StopWatchChargingMode::Off;
static bool board_led_enabled = false;
static uint16_t board_vibration[STOPWATCH_VIBRATION_MAX_STEPS] = {};
static uint8_t board_vibration_count = 0;

bool m5stack_stopwatch_set_charging_mode(StopWatchChargingMode mode) {
    if (!board_request_accepted) return false;
    board_charge_mode = mode;
    return true;
}
bool m5stack_stopwatch_set_status_led(bool enabled) {
    if (!board_request_accepted) return false;
    board_led_enabled = enabled;
    return true;
}
bool m5stack_stopwatch_vibrate_pattern(const uint16_t* durations, uint8_t count) {
    if (!board_request_accepted) return false;
    board_vibration_count = count;
    memcpy(board_vibration, durations, count * sizeof(*durations));
    return true;
}
bool m5stack_stopwatch_request_shutdown(uint32_t token, bool) {
    if (!board_request_accepted) return false;
    board_shutdown_token = token;
    return true;
}

static bool stopwatch_operation_boundaries() {
    struct Case { const char* op; const char* argument; const char* value; };
    const Case cases[] = {
        {"power.charging.set_mode", "mode", "high"},
        {"device.status_led.set", "mode", "on"},
        {"device.vibrate", "pattern", "20,100,20"},
    };
    for (const Case& test : cases) {
        JsonDocument input;
        input["op"] = test.op;
        input["args"][test.argument]["$template"] = "[var:board_value]";
        runtime_variable_set("board_value", test.value);
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        bool ok = !storage.failed && action_dispatch(action, "board") == ACTION_COMPLETE;
        if (!strcmp(test.op, "power.charging.set_mode"))
            ok = ok && board_charge_mode == StopWatchChargingMode::High;
        else if (!strcmp(test.op, "device.status_led.set"))
            ok = ok && board_led_enabled;
        else
            ok = ok && board_vibration_count == 3 && board_vibration[0] == 20 &&
                 board_vibration[1] == 100 && board_vibration[2] == 20;
        board_request_accepted = false;
        ok = ok && action_dispatch(action, "board rejected") == ACTION_FAILED;
        board_request_accepted = true;
        runtime_variable_set("board_value", "invalid");
        ok = ok && action_dispatch(action, "invalid board value") == ACTION_FAILED;
        action_storage_clear(storage);

        input["args"][test.argument] = test.value;
        ActionStorage fixed_storage = {};
        ButtonAction fixed_action = {};
        {
            ActionStorageScope scope(&fixed_storage);
            action_parse(input.as<JsonObject>(), fixed_action);
        }
        ok = ok && !fixed_storage.failed &&
            !action_operation_validate_json(input.as<JsonObjectConst>()) &&
            action_dispatch(fixed_action, "fixed board value") == ACTION_COMPLETE;
        action_storage_clear(fixed_storage);
        input["args"][test.argument] = "invalid";
        ok = ok && action_operation_validate_json(input.as<JsonObjectConst>());
        if (!ok) return false;
    }
    return true;
}
#else
static std::string board_shutdown_message;
static uint32_t board_shutdown_delay = 0;
bool sticky_request_shutdown(uint32_t token, const char* message, uint32_t delay_ms) {
    if (!board_request_accepted) return false;
    board_shutdown_token = token;
    board_shutdown_message = message;
    board_shutdown_delay = delay_ms;
    return true;
}
#endif

static bool board_shutdown_operation() {
    JsonDocument input;
    input["op"] = "device.shutdown";
    input["args"].to<JsonObject>();
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
    }
    actions[1] = set_variable_action("shutdown_after", "done");
    runtime_variable_set("shutdown_after", "before");
    board_request_accepted = false;
    bool ok = !storage.failed &&
        action_list_dispatch_result(actions, 2, "shutdown") == ACTION_FAILED &&
        get_equals("shutdown_after", "before");
    board_request_accepted = true;
    board_shutdown_token = 0;
    ok = ok && action_list_dispatch_result(actions, 2, "shutdown") == ACTION_PENDING &&
        board_shutdown_token && get_equals("shutdown_after", "before");
    if (ok) {
        action_continuation_release(board_shutdown_token);
        ok = !action_continuation_complete(board_shutdown_token, true) &&
            get_equals("shutdown_after", "before");
    }
    board_shutdown_token = 0;
    ok = ok && action_list_dispatch_result(actions, 2, "shutdown") == ACTION_PENDING &&
        board_shutdown_token && action_continuation_complete(board_shutdown_token, false);
    action_list_dispatch_continuation();
    ok = ok && get_equals("shutdown_after", "before");
#if TEST_STICKY_OPERATIONS
    ok = ok && board_shutdown_message.empty() && board_shutdown_delay == 0;
    action_storage_clear(storage);
    input["args"]["message"] = "Powered off";
    input["args"]["delay_ms"] = 5000;
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
    }
    JsonDocument saved;
    action_to_json(actions[0], saved.to<JsonObject>());
    ok = ok && !storage.failed && saved["args"]["message"] == "Powered off" &&
        saved["args"]["delay_ms"] == 5000 &&
        action_list_dispatch_result(actions, 1, "shutdown") == ACTION_PENDING &&
        board_shutdown_message == "Powered off" && board_shutdown_delay == 5000;
    action_continuation_release(board_shutdown_token);
    for (int delay : {-1, 30001}) {
        input["args"]["delay_ms"] = delay;
        ok = ok && action_operation_validate_json(input.as<JsonObjectConst>());
    }
    input["args"]["delay_ms"] = nullptr;
    ok = ok && action_operation_validate_json(input.as<JsonObjectConst>());
    input["args"].remove("delay_ms");
    ok = ok && !action_operation_validate_json(input.as<JsonObjectConst>());
    action_storage_clear(storage);
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
    }
    ok = ok && !storage.failed &&
        action_list_dispatch_result(actions, 1, "shutdown") == ACTION_PENDING &&
        board_shutdown_message == "Powered off" && board_shutdown_delay == 0;
    action_continuation_release(board_shutdown_token);
#endif
    action_storage_clear(storage);
    return ok;
}
#endif

#if HAS_DISPLAY && HAS_PSRAM
#if IS_VOICE_ASSISTANT
#include "device_classes/voice_assistant/voice.h"

static bool voice_accept = true;
static uint32_t voice_token = 0;
static uint16_t voice_silence = 0;
static uint8_t voice_threshold = 0, voice_volume = 0;
static char voice_text[60] = {}, voice_name[8] = {};
#endif
#if TEST_SPECIALIZED_MCP_BRIDGE && (IS_COFFEE_SCALE || IS_DARKROOM_TIMER)
#include "mcp_tool_registry.h"
#include <freertos/task.h>
bool test_specialized_mcp_control(const char* tool, const char* command, const char* value,
                                  int expected_code, bool drain, const char* expected_error);
void test_specialized_mcp_drain();
static unsigned specialized_mcp_effects = 0;
static TaskHandle_t specialized_mcp_task = nullptr;
static void capture_specialized_effect() {
    ++specialized_mcp_effects;
    specialized_mcp_task = xTaskGetCurrentTaskHandle();
}

static bool check_specialized_control(const char* tool, const char* command, const char* value,
                                      int error = 0, bool drain = true, const char* message = nullptr) {
    const unsigned before = specialized_mcp_effects;
    if (!test_specialized_mcp_control(tool, command, value, error, drain, message)) return false;
    if (error ? specialized_mcp_effects != before
              : specialized_mcp_effects != before + 1 ||
                    specialized_mcp_task != reinterpret_cast<TaskHandle_t>(1)) {
        std::printf("FAIL: unexpected effect or task for %s/%s\n", tool, command ? command : "<missing>");
        return false;
    }
    return true;
}
#endif
#if IS_DARKROOM_TIMER
#include "device_classes/darkroom_timer/mcp/control_mapping.h"
static char shelly_host[64] = {}, print_command[20] = {}, print_value[16] = {};
static uint8_t shelly_relay = 0;
static bool shelly_enabled = false;
static char darkroom_owner[16] = {}, darkroom_command[32] = {}, darkroom_value[32] = {};
static void darkroom_capture(const char* owner, const char* command, const char* value) {
#if TEST_SPECIALIZED_MCP_BRIDGE
    capture_specialized_effect();
#endif
    strlcpy(darkroom_owner, owner, sizeof(darkroom_owner));
    strlcpy(darkroom_command, command, sizeof(darkroom_command));
    strlcpy(darkroom_value, value, sizeof(darkroom_value));
}
void expose_timer_dispatch(const char* command, const char* value) {
    darkroom_capture("exposure", command, value);
}
void test_strip_dispatch(const char* command, const char* value) {
    darkroom_capture("strip", command, value);
}
void meter_dispatch(const char* command, const char* value) {
    darkroom_capture("meter", command, value);
}
#endif
#if IS_COFFEE_SCALE
#include "device_classes/coffee_scale/mcp/control_mapping.h"
#include "device_classes/coffee_scale/brew/brew_templates.h"
static float scale_weight = 0;
static unsigned scale_tares = 0, scale_calibrations = 0;
static char brew_command[24] = {}, brew_template[24] = {};
void scale_request_tare() { capture_specialized_effect(); ++scale_tares; }
void scale_request_tare_no_persist() { strlcpy(brew_command, "tare", sizeof(brew_command)); }
void scale_request_calibrate() { ++scale_calibrations; }
float scale_get_cal_weight() { return scale_weight; }
void scale_adjust_cal_weight(float value) { capture_specialized_effect(); scale_weight += value; }
void scale_set_cal_weight(float value) { capture_specialized_effect(); scale_weight = value; }
void brew_advance(const char*) { strlcpy(brew_command, "advance", sizeof(brew_command)); }
void brew_start(const char*) { strlcpy(brew_command, "start", sizeof(brew_command)); }
void brew_next() { capture_specialized_effect(); strlcpy(brew_command, "next", sizeof(brew_command)); }
void brew_stop() { strlcpy(brew_command, "stop", sizeof(brew_command)); }
void brew_reset() { strlcpy(brew_command, "reset", sizeof(brew_command)); }
void brew_hint_template(const char* name) {
    capture_specialized_effect();
    strlcpy(brew_template, name, sizeof(brew_template));
}
const BrewTemplate* brew_template_find(const char* name) {
    static BrewTemplate known = {};
    static BrewTemplate fallback = {};
    strlcpy(known.name, "v60", sizeof(known.name));
    strlcpy(fallback.name, "free_pour", sizeof(fallback.name));
    return !std::strcmp(name, known.name) ? &known : &fallback;
}
#endif
#if TEST_SHUTTER_OPERATIONS || TEST_RELAY_HOST
#include <map>
#include <string>
std::map<std::string, std::string> storage_test_files;
bool storage_test_fail_open = false;
size_t storage_test_write_limit = SIZE_MAX;
FakeLittleFS LittleFS;
static bool shutter_write_failure = false;
static bool config_alloc_failure = false;
void storage_publish_usage(bool) {}
void* config_psram_alloc(size_t bytes, const char*) {
    return config_alloc_failure ? nullptr : std::calloc(1, bytes);
}
bool storage_write_atomic(const char* path, const uint8_t* data, size_t length) {
    if (shutter_write_failure) return false;
    storage_test_files[path] = std::string(reinterpret_cast<const char*>(data), length);
    return true;
}
#endif
#if TEST_SHUTTER_OPERATIONS
#include "device_classes/shutter_tester/mcp/control_mapping.h"
#include "device_classes/shutter_tester/shutter_session_actions.h"
#include <freertos/task.h>
bool test_shutter_mcp_control(const char* command, const char* value, bool drain, int* error_code);
void test_shutter_mcp_drain();
static char shutter_command[32] = {}, shutter_value[64] = {};
static bool shutter_accept = true;
static TaskHandle_t shutter_dispatch_task = nullptr;
static void shutter_capture_operation(const char* command, const char* value = "") {
    strlcpy(shutter_command, command, sizeof(shutter_command));
    strlcpy(shutter_value, value, sizeof(shutter_value));
    shutter_dispatch_task = xTaskGetCurrentTaskHandle();
}
bool shutter_measure_set_target(const char* value, bool) {
    shutter_capture_operation("set", value);
    return shutter_accept;
}
void shutter_measure_adjust_target(bool faster) {
    shutter_capture_operation("adjust", faster ? "faster" : "slower");
}
bool shutter_measure_toggle_lock() {
    shutter_capture_operation("toggle_lock");
    return shutter_accept;
}
bool shutter_session_start(const char* value) { shutter_capture_operation("sess_start", value); return shutter_accept; }
bool shutter_session_toggle(const char* value) { shutter_capture_operation("sess_toggle", value); return shutter_accept; }
bool shutter_session_guide_start(const char* value) { shutter_capture_operation("guide_start", value); return shutter_accept; }
void shutter_session_stop() { shutter_capture_operation("sess_stop"); }
void shutter_session_discard_last() { shutter_capture_operation("sess_discard"); }
void shutter_session_guide_stop() { shutter_capture_operation("guide_stop"); }
void shutter_session_guide_skip() { shutter_capture_operation("guide_skip"); }
void shutter_session_guide_redo() { shutter_capture_operation("guide_redo"); }
void shutter_capture_start_alignment() { shutter_capture_operation("align_start"); }
void shutter_capture_stop_alignment() { shutter_capture_operation("align_stop"); }
void shutter_capture_recalibrate() { shutter_capture_operation("recalibrate"); }
#endif
#if IS_DARKROOM_TIMER
#if !TEST_RELAY_HOST
void relay_queue_shelly(const char* host, uint8_t relay, bool enabled) {
    strlcpy(shelly_host, host, sizeof(shelly_host));
    shelly_relay = relay;
    shelly_enabled = enabled;
}
#endif
void print_log_dispatch(const char* command, const char* value) {
#if TEST_SPECIALIZED_MCP_BRIDGE
    capture_specialized_effect();
#endif
    strlcpy(print_command, command, sizeof(print_command));
    strlcpy(print_value, value, sizeof(print_value));
}
#endif
#if TEST_RELAY_HOST
#include <freertos/portmacro.h>
#include "device_classes/darkroom_timer/relay_controller.cpp"
static TaskFunction_t relay_test_worker = nullptr;
bool rtos_create_task_psram_stack_pinned(TaskFunction_t fn, const char*, uint32_t,
                                        void*, UBaseType_t, TaskHandle_t* handle,
                                        RtosTaskPsramAlloc*, BaseType_t) {
    relay_test_worker = fn;
    *handle = reinterpret_cast<TaskHandle_t>(1);
    return true;
}

struct RelayTestDone {};
static void relay_stop_worker(SemaphoreHandle_t, TickType_t) { throw RelayTestDone{}; }
static void relay_run_pending() {
    test_freertos_current_task = g_relay_task;
    test_freertos_semaphore_take_hook = relay_stop_worker;
    try { relay_test_worker(nullptr); } catch (const RelayTestDone&) {}
    test_freertos_semaphore_take_hook = nullptr;
    test_freertos_current_task = nullptr;
}

static bool relay_host_operations() {
    const char* path = "/config/relay_actions.json";
    storage_test_files[path] = R"({"enlarger_on":{"type":"shelly"}})";
    relay_controller_init();
    std::string serialized;
    storage_test_fail_open = true;
    relay_load_config();
    bool ok = !relay_get_config_json(serialized);
    storage_test_fail_open = false;
    config_alloc_failure = true;
    relay_load_config();
    ok = ok && !relay_get_config_json(serialized);
    config_alloc_failure = false;
    relay_load_config();
    JsonDocument reset;
    ok = ok && relay_test_worker && relay_get_config_json(serialized) &&
        !deserializeJson(reset, serialized) && reset["config_reset"] == true &&
        reset["enlarger_on"].size() == 0 && storage_test_files[path] == R"({"enlarger_on":{"type":"shelly"}})";
    const char* incompatible = R"({"action_format_version":1,
        "enlarger_on":{"op":"variable.set","args":{"name":"example","value":"valid"}},
        "enlarger_off":{"op":"missing.operation","args":{}}})";
    storage_test_files[path] = incompatible;
    relay_load_config();
    ok = ok && relay_get_config_json(serialized) && !deserializeJson(reset, serialized) &&
        reset["config_reset"] == true && reset["enlarger_on"].size() == 0 &&
        reset["enlarger_off"].size() == 0 && storage_test_files[path] == incompatible;
    const std::string legacy = storage_test_files[path];
    const char* config = R"({"action_format_version":1,
        "enlarger_on":{"op":"shelly.relay.set","args":{"host":{"$template":"[var:relay_host]"},"relay":0,"enabled":false}},
        "enlarger_off":{},
        "safelight_on":{},
        "safelight_off":{"op":"shelly.relay.set","args":{"host":"safe.local","relay":1,"enabled":true}}})";
    shutter_write_failure = true;
    ok = ok && !relay_save_config_from_json(reinterpret_cast<const uint8_t*>(config), strlen(config)) &&
        storage_test_files[path] == legacy && relay_get_config_json(serialized) &&
        !deserializeJson(reset, serialized) && reset["config_reset"] == true;
    shutter_write_failure = false;
    ok = ok && relay_save_config_from_json(reinterpret_cast<const uint8_t*>(config), strlen(config));
    relay_load_config();
    ok = ok && relay_get_config_json(serialized) && !deserializeJson(reset, serialized) &&
        reset["config_reset"] == false && reset["enlarger_on"]["op"] == "shelly.relay.set";
    const std::string saved = storage_test_files[path];
    shutter_write_failure = true;
    ok = ok && !relay_save_config_from_json(reinterpret_cast<const uint8_t*>(config), strlen(config)) &&
         storage_test_files[path] == saved;
    shutter_write_failure = false;
    const char* invalid = R"({"action_format_version":1,"enlarger_on":{"type":"shelly"}})";
    ok = ok && !relay_save_config_from_json(reinterpret_cast<const uint8_t*>(invalid), strlen(invalid)) &&
         storage_test_files[path] == saved && relay_get_config_json(serialized);
    const std::string previous = serialized;
    const std::string nul_key = std::string(199, ' ') +
        R"({"action_format_version":1,"enlarger_on":{},"enlarger_on\u0000x":{"type":"shelly"},"enlarger_off":{},"safelight_on":{},"safelight_off":{}})";
    ok = ok && !relay_save_config_from_json(reinterpret_cast<const uint8_t*>(nul_key.data()), nul_key.size()) &&
         storage_test_files[path] == saved;
    storage_test_files[path] = nul_key;
    relay_load_config();
    ok = ok && storage_test_files[path] == nul_key && relay_get_config_json(serialized) &&
         serialized == previous;
    storage_test_files[path] = saved;
    runtime_variable_set("relay_host", "enlarger.local");
    relay_test_requests.clear();
    relay_request(true);
    relay_run_pending();
    ok = ok && relay_test_requests.size() == 2 &&
         relay_test_requests[0] == "http://enlarger.local/relay/0?turn=on" &&
         relay_test_requests[1] == "http://safe.local/relay/1?turn=off";
    runtime_variable_set("relay_host", "");
    relay_test_requests.clear();
    relay_request(true);
    relay_run_pending();
    ok = ok && relay_test_requests.size() == 1 &&
         relay_test_requests[0] == "http://safe.local/relay/1?turn=off";
    const char* deferred = R"({"action_format_version":1,
        "enlarger_on":{"op":"variable.set","args":{"name":"relay_result","value":"first"}},
        "enlarger_off":{},
        "safelight_on":{},
        "safelight_off":{"op":"variable.set","args":{"name":"relay_result","value":"last"}}})";
    ok = ok && relay_save_config_from_json(reinterpret_cast<const uint8_t*>(deferred), strlen(deferred));
    relay_request(true);
    relay_run_pending();
    const char* empty = R"({"action_format_version":1,"enlarger_on":{},"enlarger_off":{},"safelight_on":{},"safelight_off":{}})";
    ok = ok && relay_save_config_from_json(reinterpret_cast<const uint8_t*>(empty), strlen(empty));
    relay_loop();
    ok = ok && get_equals("relay_result", "last");
    return ok;
}
#endif
#if IS_VOICE_ASSISTANT
bool voice_start_recording() { return voice_accept; }
bool voice_start_until_silence(uint16_t silence, uint8_t threshold, uint32_t token) {
    voice_silence = silence;
    voice_threshold = threshold;
    voice_token = token;
    return voice_accept;
}
VoiceStopResult voice_stop_and_transcribe(uint32_t token) {
    voice_token = token;
    return voice_accept ? VOICE_STOP_PENDING : VOICE_STOP_FAILED;
}
bool voice_cancel_recording() { return voice_accept; }
bool voice_speak(const char* text, const char* voice, uint8_t volume) {
    strlcpy(voice_text, text, sizeof(voice_text));
    strlcpy(voice_name, voice, sizeof(voice_name));
    voice_volume = volume;
    return voice_accept;
}

static bool voice_assistant_operations() {
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"voice.record.until_silence","args":{"silence_ms":{"$template":"[var:voice_delay]"},"speech_threshold":7}})"))
        return false;
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
    }
    actions[1] = set_variable_action("voice_after", "done");
    runtime_variable_set("voice_after", "before");
    runtime_variable_set("voice_delay", "1200");
    voice_accept = false;
    bool ok = !storage.failed &&
        action_list_dispatch_result(actions, 2, "voice") == ACTION_FAILED &&
        get_equals("voice_after", "before");
    voice_accept = true;
    voice_token = 0;
    ok = ok && action_list_dispatch_result(actions, 2, "voice") == ACTION_PENDING &&
        voice_token && voice_silence == 1200 && voice_threshold == 7 &&
        get_equals("voice_after", "before");
    if (ok) {
        ok = action_continuation_complete(voice_token, true);
        action_list_dispatch_continuation();
        ok = ok && get_equals("voice_after", "done");
    }
    runtime_variable_set("voice_delay", "10001");
    ok = ok && action_list_dispatch_result(actions, 2, "voice") == ACTION_FAILED;
    action_storage_clear(storage);

    input.clear();
    if (deserializeJson(input, R"({"op":"voice.speak","args":{"text":{"$template":"[var:message]"},"voice":"nova","volume":45}})"))
        return false;
    runtime_variable_set("message", "hello");
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
    }
    ok = ok && !storage.failed && action_dispatch(actions[0], "voice") == ACTION_COMPLETE &&
        !std::strcmp(voice_text, "hello") && !std::strcmp(voice_name, "nova") && voice_volume == 45;
    voice_accept = false;
    ok = ok && action_dispatch(actions[0], "voice") == ACTION_FAILED;
    voice_accept = true;
    action_storage_clear(storage);

    input.clear();
    input["op"] = "voice.record.start";
    input["args"].to<JsonObject>();
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
    }
    ok = ok && !storage.failed && action_dispatch(actions[0], "voice") == ACTION_COMPLETE;
    voice_accept = false;
    ok = ok && action_dispatch(actions[0], "voice") == ACTION_FAILED;
    voice_accept = true;
    action_storage_clear(storage);

    input["op"] = "voice.record.stop_transcribe";
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
    }
    runtime_variable_set("voice_after", "before");
    voice_token = 0;
    ok = ok && !storage.failed &&
        action_list_dispatch_result(actions, 2, "voice") == ACTION_PENDING && voice_token;
    if (ok) {
        action_continuation_release(voice_token);
        ok = !action_continuation_complete(voice_token, true) && get_equals("voice_after", "before");
    }
    action_storage_clear(storage);

    input["op"] = "voice.record.cancel";
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
    }
    ok = ok && !storage.failed &&
        action_list_dispatch_result(actions, 2, "voice") == ACTION_FAILED &&
        get_equals("voice_after", "before");
    action_storage_clear(storage);
    return ok;
}
#endif

#if IS_DARKROOM_TIMER || IS_COFFEE_SCALE || TEST_SHUTTER_OPERATIONS
static bool mcp_mapping_dispatch(JsonDocument& input) {
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    const bool ok = !storage.failed && action.type[0] == '@' &&
        action_dispatch(action, "MCP") == ACTION_COMPLETE;
    action_storage_clear(storage);
    return ok;
}
#endif

#if IS_DARKROOM_TIMER
static bool darkroom_simple_operations() {
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"shelly.relay.set","args":{"host":{"$template":"[var:relay_host]"},"relay":2,"enabled":{"$template":"[var:relay_enabled]"}}})"))
        return false;
    runtime_variable_set("relay_host", "shelly.local");
    runtime_variable_set("relay_enabled", "false");
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    bool ok = !storage.failed && action_dispatch(action, "relay") == ACTION_COMPLETE &&
        !std::strcmp(shelly_host, "shelly.local") && shelly_relay == 2 && !shelly_enabled;
    runtime_variable_set("relay_enabled", "invalid");
    shelly_host[0] = '\0';
    ok = ok && action_dispatch(action, "relay") == ACTION_FAILED && !shelly_host[0];
    action_storage_clear(storage);

    input.clear();
    input["op"] = "print.star.set";
    input["args"]["starred"] = true;
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    ok = ok && !storage.failed && action_dispatch(action, "print") == ACTION_COMPLETE &&
        !std::strcmp(print_command, "set_star") && !std::strcmp(print_value, "1");
    action_storage_clear(storage);
    input["op"] = "print.star.toggle";
    input["args"].to<JsonObject>();
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    ok = ok && !storage.failed && action_dispatch(action, "print") == ACTION_COMPLETE &&
        !std::strcmp(print_command, "toggle_star");
    action_storage_clear(storage);
    return ok;
}

static bool darkroom_timer_operations() {
    struct Case { const char* op; const char* owner; const char* command; const char* argument; const char* value; };
    const Case cases[] = {
        {"exposure.toggle", "exposure", "toggle", nullptr, ""},
        {"exposure.start", "exposure", "start", nullptr, ""},
        {"exposure.stop", "exposure", "stop", nullptr, ""},
        {"exposure.pause", "exposure", "pause", nullptr, ""},
        {"exposure.resume", "exposure", "resume", nullptr, ""},
        {"exposure.reset", "exposure", "reset", nullptr, ""},
        {"exposure.focus.on", "exposure", "focus", nullptr, ""},
        {"exposure.focus.off", "exposure", "focus_off", nullptr, ""},
        {"exposure.focus.toggle", "exposure", "focus_toggle", nullptr, ""},
        {"exposure.time.set", "exposure", "set_time", "seconds", "1.25"},
        {"exposure.time.adjust", "exposure", "adjust_seconds", "seconds", "1.25"},
        {"exposure.stops.adjust", "exposure", "adjust_stops", "stops", "1.25"},
        {"exposure.dry_down.set", "exposure", "set_dry_down", "percent", "1.25"},
        {"exposure.dry_down.adjust", "exposure", "adjust_dry_down", "percent", "1.25"},
        {"strip.start", "strip", "start", nullptr, ""},
        {"strip.cancel", "strip", "cancel", nullptr, ""},
        {"strip.step.increase", "strip", "step_up", nullptr, ""},
        {"strip.step.decrease", "strip", "step_down", nullptr, ""},
        {"strip.base.set", "strip", "set_base", "seconds", "1.25"},
        {"strip.base.adjust", "strip", "adjust_base", "seconds", "1.25"},
        {"strip.segments.adjust", "strip", "adjust_segments", "segments", "2"},
        {"strip.segments.set", "strip", "set_segments", "segments", "2"},
        {"strip.countdown.set", "strip", "set_countdown", "seconds", "2"},
        {"strip.countdown.adjust", "strip", "adjust_countdown", "seconds", "2"},
        {"strip.pause.set", "strip", "set_pause", "seconds", "2"},
        {"strip.pause.adjust", "strip", "adjust_pause", "seconds", "2"},
        {"strip.tick.set", "strip", "set_tick", "enabled", "on"},
        {"meter.reference.read", "meter", "read_lref", nullptr, ""},
        {"meter.bright.read", "meter", "read_bright", nullptr, ""},
        {"meter.dark.read", "meter", "read_dark", nullptr, ""},
        {"meter.magnification.measure_a", "meter", "mag_measure_a", nullptr, ""},
        {"meter.magnification.measure_b", "meter", "mag_measure_b", nullptr, ""},
        {"meter.magnification.clear", "meter", "mag_clear", nullptr, ""},
        {"meter.reference.set", "meter", "set_lref", "lux", "1.25"},
        {"meter.reference.adjust", "meter", "adjust_lref", "lux", "1.25"},
        {"meter.zone5.set", "meter", "set_zone5", "seconds", "1.25"},
        {"meter.zone5.adjust", "meter", "adjust_zone5", "seconds", "1.25"},
    };
    for (const Case& test : cases) {
        JsonDocument input;
        input["op"] = test.op;
        JsonObject args = input["args"].to<JsonObject>();
        if (test.argument) {
            args[test.argument]["$template"] = "[var:darkroom_value]";
            runtime_variable_set("darkroom_value",
                !std::strcmp(test.value, "on") ? "true" : test.value);
        }
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        darkroom_command[0] = '\0';
        bool ok = !storage.failed && action_dispatch(action, "darkroom") == ACTION_COMPLETE &&
            !std::strcmp(darkroom_owner, test.owner) && !std::strcmp(darkroom_command, test.command) &&
            !std::strcmp(darkroom_value, test.value);
        if (test.argument) {
            runtime_variable_set("darkroom_value", "invalid");
            darkroom_command[0] = '\0';
            ok = ok && action_dispatch(action, "darkroom") == ACTION_FAILED && !darkroom_command[0];
        }
        action_storage_clear(storage);
        if (!ok) {
            std::printf("FAIL: darkroom operation %s\n", test.op);
            return false;
        }
        const char* type = !std::strcmp(test.owner, "exposure") ? ACTION_TYPE_EXPOSE :
            !std::strcmp(test.owner, "strip") ? ACTION_TYPE_STRIP : ACTION_TYPE_METER;
        const DrControlMapping* mapping = dr_control_mapping(type, test.command);
        if (!mapping) return false;
        input.clear();
        const char* value = test.argument && !mapping->boolean ? "[var:darkroom_value]" : test.value;
        if (test.argument && !mapping->boolean) runtime_variable_set("darkroom_value", test.value);
        if (!dr_control_request(input.to<JsonObject>(), *mapping, value) ||
            std::strcmp(input["op"].as<const char*>(), test.op)) return false;
        darkroom_command[0] = '\0';
        if (!mcp_mapping_dispatch(input) || std::strcmp(darkroom_owner, test.owner) ||
            std::strcmp(darkroom_command, test.command) || std::strcmp(darkroom_value, test.value))
            return false;
    }
    if (dr_control_mapping(ACTION_TYPE_EXPOSE, "set_tick") ||
        dr_control_mapping(ACTION_TYPE_STRIP, "unknown") ||
        dr_control_mapping(nullptr, "start")) return false;
    struct BoolCase { const char* type; const char* command; const char* value; bool expected; };
    const BoolCase bool_cases[] = {
        {ACTION_TYPE_STRIP, "set_tick", "on", true}, {ACTION_TYPE_STRIP, "set_tick", "off", false},
        {ACTION_TYPE_PRINT, "set_star", "1", true}, {ACTION_TYPE_PRINT, "set_star", "0", false},
    };
    for (const BoolCase& test : bool_cases) {
        const DrControlMapping* mapping = dr_control_mapping(test.type, test.command);
        runtime_variable_set("mcp_bool", test.value);
        char resolved[BINDING_TEMPLATE_MAX_LEN];
        if (!mapping || !binding_template_resolve_checked("[var:mcp_bool]", resolved, sizeof(resolved)))
            return false;
        JsonDocument input;
        if (!dr_control_request(input.to<JsonObject>(), *mapping, resolved) ||
            !input["args"][mapping->argument].is<bool>() ||
            input["args"][mapping->argument].as<bool>() != test.expected ||
            !mcp_mapping_dispatch(input)) return false;
        if (!std::strcmp(test.type, ACTION_TYPE_STRIP)) {
            if (std::strcmp(darkroom_value, test.value)) return false;
        } else if (std::strcmp(print_command, "set_star") || std::strcmp(print_value, test.value)) {
            return false;
        }
        input.clear();
        if (dr_control_request(input.to<JsonObject>(), *mapping, "true")) return false;
        input.clear();
        if (dr_control_request(input.to<JsonObject>(), *mapping,
                               !std::strcmp(test.type, ACTION_TYPE_STRIP) ? "1" : "on")) return false;
    }
    const DrControlMapping* toggle = dr_control_mapping(ACTION_TYPE_PRINT, "toggle_star");
    JsonDocument input;
    if (!toggle || !dr_control_request(input.to<JsonObject>(), *toggle, "") ||
        !mcp_mapping_dispatch(input) || std::strcmp(print_command, "toggle_star")) return false;
    const DrControlMapping* numeric = dr_control_mapping(ACTION_TYPE_EXPOSE, "set_time");
    input.clear();
    runtime_variable_set("mcp_bad_number", "not-a-number");
    if (!numeric || !dr_control_request(input.to<JsonObject>(), *numeric, "[var:mcp_bad_number]"))
        return false;
    darkroom_command[0] = '\0';
    if (mcp_mapping_dispatch(input) || darkroom_command[0]) return false;
    char unresolved[BINDING_TEMPLATE_MAX_LEN];
    if (binding_template_resolve_checked("[var:missing_mcp_boolean]", unresolved, sizeof(unresolved)))
        return false;
    return true;
}

#if TEST_SPECIALIZED_MCP_BRIDGE
static bool darkroom_mcp_bridge() {
    runtime_variable_set("mcp_seconds", "12.5");
    runtime_variable_set("mcp_print_star", "1");
    if (!check_specialized_control("expose_control", "set_time", "[var:mcp_seconds]") ||
        std::strcmp(darkroom_owner, "exposure") || std::strcmp(darkroom_command, "set_time") ||
        std::strcmp(darkroom_value, "12.5")) return false;
    if (!check_specialized_control("strip_control", "set_tick", "off") ||
        std::strcmp(darkroom_owner, "strip") || std::strcmp(darkroom_command, "set_tick") ||
        std::strcmp(darkroom_value, "off")) return false;
    if (!check_specialized_control("meter_control", "read_bright", nullptr) ||
        std::strcmp(darkroom_owner, "meter") || std::strcmp(darkroom_command, "read_bright")) return false;
    if (!check_specialized_control("print_control", "set_star", "[var:mcp_print_star]") ||
        std::strcmp(print_command, "set_star") || std::strcmp(print_value, "1")) return false;
    if (!check_specialized_control("print_control", "start", nullptr, MCP_RPC_ERR_PARAMS) ||
        !check_specialized_control("expose_control", nullptr, nullptr, MCP_RPC_ERR_PARAMS) ||
        !check_specialized_control("strip_control", "set_tick",
                                   "onxxxxxxxxxxxxxxxxxxxxxxxx", MCP_RPC_ERR_PARAMS) ||
        !check_specialized_control("meter_control", "set_lref", "not-a-number",
                                   MCP_RPC_ERR_INTERNAL, true, "failed to dispatch") ||
        !check_specialized_control("strip_control", "set_tick", "1",
                                   MCP_RPC_ERR_INTERNAL, true, "invalid value") ||
        !check_specialized_control("print_control", "set_star", "[var:no_mcp_bool]",
                                   MCP_RPC_ERR_INTERNAL, true, "invalid value")) return false;

    const unsigned before = specialized_mcp_effects;
    if (!check_specialized_control("expose_control", "set_time", "[var:mcp_seconds]",
                                   MCP_RPC_ERR_INTERNAL, false, "timed out") ||
        !check_specialized_control("strip_control", "cancel", nullptr, MCP_RPC_ERR_CONTROL_BUSY))
        return false;
    // A timeout abandons only the reply. The copied job resolves bindings when drained.
    runtime_variable_set("mcp_seconds", "19.25");
    test_specialized_mcp_drain();
    if (specialized_mcp_effects != before + 1 ||
        specialized_mcp_task != reinterpret_cast<TaskHandle_t>(1) ||
        std::strcmp(darkroom_owner, "exposure") || std::strcmp(darkroom_command, "set_time") ||
        std::strcmp(darkroom_value, "19.25")) return false;
    test_specialized_mcp_drain();
    return specialized_mcp_effects == before + 1 &&
        check_specialized_control("strip_control", "cancel", nullptr) &&
        !std::strcmp(darkroom_owner, "strip") && !std::strcmp(darkroom_command, "cancel");
}
#endif
#endif

#if IS_COFFEE_SCALE
static bool coffee_operations() {
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"scale.calibration.set_weight","args":{"grams":251.5}})"))
        return false;
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    bool ok = !storage.failed && action_dispatch(action, "scale") == ACTION_COMPLETE &&
        scale_weight == 251.5f;
    action_storage_clear(storage);
    input["op"] = "scale.calibration.adjust_weight";
    input["args"]["grams"].to<JsonObject>()["$template"] = "[var:weight_delta]";
    runtime_variable_set("weight_delta", "-0.5");
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    ok = ok && !storage.failed && action_dispatch(action, "scale") == ACTION_COMPLETE && scale_weight == 251.0f;
    runtime_variable_set("weight_delta", "0");
    ok = ok && action_dispatch(action, "scale") == ACTION_FAILED && scale_weight == 251.0f;
    action_storage_clear(storage);

    input.clear();
    input["op"] = "brew.template.set";
    input["args"]["template"]["$template"] = "[var:recipe]";
    runtime_variable_set("recipe", "v60");
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    ok = ok && !storage.failed && action_dispatch(action, "brew") == ACTION_COMPLETE &&
        !std::strcmp(brew_template, "v60");
    runtime_variable_set("recipe", "missing");
    ok = ok && action_dispatch(action, "brew") == ACTION_FAILED && !std::strcmp(brew_template, "v60");
    action_storage_clear(storage);

    struct Case { const char* operation; const char* command; };
    const Case cases[] = {
        {"brew.advance", "advance"}, {"brew.start", "start"}, {"brew.next", "next"},
        {"brew.stop", "stop"}, {"brew.reset", "reset"}, {"brew.tare", "tare"},
    };
    for (const Case& test : cases) {
        input.clear();
        input["op"] = test.operation;
        input["args"].to<JsonObject>();
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        ok = ok && !storage.failed && action_dispatch(action, "brew") == ACTION_COMPLETE &&
            !std::strcmp(brew_command, test.command);
        action_storage_clear(storage);
    }
    for (const char* op : {"scale.tare", "scale.calibrate"}) {
        input.clear();
        input["op"] = op;
        input["args"].to<JsonObject>();
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        ok = ok && !storage.failed && action_dispatch(action, "scale") == ACTION_COMPLETE;
        action_storage_clear(storage);
    }
    return ok && scale_tares == 1 && scale_calibrations == 1;
}

static bool coffee_mcp_mappings() {
    struct Case { const char* type; const char* command; const char* operation; const char* value; };
    const Case cases[] = {
        {ACTION_TYPE_SCALE, "tare", "scale.tare", ""},
        {ACTION_TYPE_SCALE, "calibrate", "scale.calibrate", ""},
        {ACTION_TYPE_SCALE, "cal_weight", "scale.calibration.adjust_weight", "-0.5"},
        {ACTION_TYPE_SCALE, "cal_weight_set", "scale.calibration.set_weight", "123.5"},
        {ACTION_TYPE_BREW, "advance", "brew.advance", ""},
        {ACTION_TYPE_BREW, "start", "brew.start", ""},
        {ACTION_TYPE_BREW, "next", "brew.next", ""},
        {ACTION_TYPE_BREW, "stop", "brew.stop", ""},
        {ACTION_TYPE_BREW, "reset", "brew.reset", ""},
        {ACTION_TYPE_BREW, "tare", "brew.tare", ""},
        {ACTION_TYPE_BREW, "set_template", "brew.template.set", "v60"},
    };
    const unsigned tares_before = scale_tares, calibrations_before = scale_calibrations;
    for (const Case& test : cases) {
        const CsControlMapping* mapping = cs_control_mapping(test.type, test.command);
        if (!mapping) return false;
        JsonDocument input;
        cs_control_request(input.to<JsonObject>(), *mapping, test.value);
        if (std::strcmp(input["op"].as<const char*>(), test.operation) || !mcp_mapping_dispatch(input))
            return false;
        if (!std::strcmp(test.command, "cal_weight") && scale_weight != 250.5f) return false;
        if (!std::strcmp(test.command, "cal_weight_set") && scale_weight != 123.5f) return false;
        if (!std::strcmp(test.command, "set_template") && std::strcmp(brew_template, "v60")) return false;
        if (!std::strcmp(test.type, ACTION_TYPE_BREW) && std::strcmp(test.command, "set_template") &&
            std::strcmp(brew_command, test.command)) return false;
    }
    if (scale_tares != tares_before + 1 || scale_calibrations != calibrations_before + 1) return false;
    if (cs_control_mapping(ACTION_TYPE_SCALE, "start") || cs_control_mapping(nullptr, "tare") ||
        cs_control_mapping(ACTION_TYPE_BREW, "unknown")) return false;
    JsonDocument input;
    const CsControlMapping* mapping = cs_control_mapping(ACTION_TYPE_SCALE, "cal_weight");
    cs_control_request(input.to<JsonObject>(), *mapping, "invalid");
    if (mcp_mapping_dispatch(input) || scale_weight != 123.5f) return false;
    input.clear();
    mapping = cs_control_mapping(ACTION_TYPE_BREW, "set_template");
    cs_control_request(input.to<JsonObject>(), *mapping, "missing");
    return !mcp_mapping_dispatch(input) && !std::strcmp(brew_template, "v60");
}

static bool coffee_mcp_bridge() {
    const unsigned tares_before = scale_tares;
    if (!check_specialized_control("scale_control", "tare", nullptr) ||
        scale_tares != tares_before + 1 ||
        !check_specialized_control("scale_control", "cal_weight_set", "120.5") ||
        scale_weight != 120.5f ||
        !check_specialized_control("scale_control", "cal_weight", "-0.25") ||
        scale_weight != 120.25f) return false;
    brew_template[0] = '\0';
    if (!check_specialized_control("brew_control", "set_template", "v60") ||
        std::strcmp(brew_template, "v60")) return false;
    if (!check_specialized_control("scale_control", "start", nullptr, MCP_RPC_ERR_PARAMS) ||
        !check_specialized_control("scale_control", "cal_weight", "0", MCP_RPC_ERR_PARAMS) ||
        !check_specialized_control("brew_control", "set_template", "not-a-template", MCP_RPC_ERR_PARAMS) ||
        !check_specialized_control("scale_control", "cal_weight_set", "121.5junk",
                                   MCP_RPC_ERR_INTERNAL, true, "failed to dispatch")) return false;

    fail_psram = true;
    const bool allocation_rejected = check_specialized_control("scale_control", "tare", nullptr,
        MCP_RPC_ERR_INTERNAL);
    fail_psram = false;
    if (!allocation_rejected) return false;

    const unsigned before = specialized_mcp_effects;
    if (!check_specialized_control("scale_control", "cal_weight_set", "135.25",
                                   MCP_RPC_ERR_INTERNAL, false, "timed out") ||
        !check_specialized_control("brew_control", "next", nullptr, MCP_RPC_ERR_CONTROL_BUSY) ||
        scale_weight != 120.25f) return false;
    test_specialized_mcp_drain();
    if (specialized_mcp_effects != before + 1 || scale_weight != 135.25f ||
        specialized_mcp_task != reinterpret_cast<TaskHandle_t>(1)) return false;
    test_specialized_mcp_drain();
    brew_command[0] = '\0';
    return specialized_mcp_effects == before + 1 &&
        check_specialized_control("brew_control", "next", nullptr) &&
        !std::strcmp(brew_command, "next");
}
#endif

#if TEST_SHUTTER_OPERATIONS
static bool shutter_operations() {
    struct Case { const char* op; const char* command; const char* argument; const char* value; };
    const Case cases[] = {
        {"shutter.target.set", "set", "speed", "1/125"},
        {"shutter.target.adjust", "adjust", "direction", "slower"},
        {"shutter.target.toggle_lock", "toggle_lock", nullptr, ""},
        {"shutter.session.start", "sess_start", "camera", "camera1"},
        {"shutter.session.toggle", "sess_toggle", "camera", "camera1"},
        {"shutter.session.stop", "sess_stop", nullptr, ""},
        {"shutter.session.discard", "sess_discard", nullptr, ""},
        {"shutter.guide.start", "guide_start", "test", "test1"},
        {"shutter.guide.stop", "guide_stop", nullptr, ""},
        {"shutter.guide.skip", "guide_skip", nullptr, ""},
        {"shutter.guide.redo", "guide_redo", nullptr, ""},
        {"shutter.alignment.start", "align_start", nullptr, ""},
        {"shutter.alignment.stop", "align_stop", nullptr, ""},
        {"shutter.recalibrate", "recalibrate", nullptr, ""},
    };
    for (const Case& test : cases) {
        JsonDocument input;
        input["op"] = test.op;
        JsonObject args = input["args"].to<JsonObject>();
        if (test.argument) {
            args[test.argument]["$template"] = "[var:shutter_value]";
            runtime_variable_set("shutter_value", test.value);
        }

        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        shutter_command[0] = '\0';
        bool ok = !storage.failed && action_dispatch(action, "shutter") == ACTION_COMPLETE &&
            !std::strcmp(shutter_command, test.command) && !std::strcmp(shutter_value, test.value);
        if (!std::strcmp(test.command, "set") || !std::strcmp(test.command, "adjust")) {
            runtime_variable_set("shutter_value", "invalid");
            shutter_command[0] = '\0';
            ok = ok && action_dispatch(action, "shutter") == ACTION_FAILED && !shutter_command[0];
        }
        if (!std::strcmp(test.command, "toggle_lock") ||
            !std::strcmp(test.command, "sess_start") ||
            !std::strcmp(test.command, "sess_toggle") ||
            !std::strcmp(test.command, "guide_start")) {
            shutter_accept = false;
            ok = ok && action_dispatch(action, "shutter") == ACTION_FAILED;
            shutter_accept = true;
        }
        action_storage_clear(storage);
        if (!ok) {
            std::printf("FAIL: shutter operation %s\n", test.op);
            return false;
        }
        const ShutterControlMapping* mapping = shutter_control_mapping(test.command);
        if (!mapping) return false;
        input.clear();
        shutter_control_request(input.to<JsonObject>(), *mapping, test.value);
        shutter_command[0] = '\0';
        if (std::strcmp(input["op"].as<const char*>(), test.op) || !mcp_mapping_dispatch(input) ||
            std::strcmp(shutter_command, test.command) || std::strcmp(shutter_value, test.value)) return false;
    }
    if (shutter_control_mapping("unknown") || shutter_control_mapping(nullptr)) return false;
    JsonDocument input;
    const ShutterControlMapping* mapping = shutter_control_mapping("sess_start");
    shutter_control_request(input.to<JsonObject>(), *mapping, "");
    if (!mcp_mapping_dispatch(input) || std::strcmp(shutter_command, "sess_start") || shutter_value[0])
        return false;
    input.clear();
    mapping = shutter_control_mapping("adjust");
    shutter_control_request(input.to<JsonObject>(), *mapping, "invalid");
    shutter_command[0] = '\0';
    if (mcp_mapping_dispatch(input) || shutter_command[0]) return false;
    input.clear();
    runtime_variable_set("mcp_direction", "faster");
    shutter_control_request(input.to<JsonObject>(), *mapping, "[var:mcp_direction]");
    if (!mcp_mapping_dispatch(input) || std::strcmp(shutter_value, "faster")) return false;
    input.clear();
    mapping = shutter_control_mapping("toggle_lock");
    shutter_control_request(input.to<JsonObject>(), *mapping, "");
    shutter_accept = false;
    const bool dispatched = mcp_mapping_dispatch(input);
    shutter_accept = true;
    return !dispatched;
}

static bool shutter_mcp_bridge() {
    int error = 0;
    shutter_command[0] = '\0';
    if (!test_shutter_mcp_control("sess_start", "camera", true, &error) ||
        std::strcmp(shutter_command, "sess_start") ||
        std::strcmp(shutter_value, "camera") ||
        shutter_dispatch_task != reinterpret_cast<TaskHandle_t>(1)) return false;
    const struct { const char* command; const char* value; } controls[] = {
        {"set", "1/125"}, {"toggle_lock", ""}, {"sess_toggle", "camera"},
        {"sess_stop", ""}, {"sess_discard", ""}, {"guide_start", "test"},
        {"guide_skip", ""}, {"guide_redo", ""}, {"guide_stop", ""},
        {"align_start", ""}, {"align_stop", ""}, {"recalibrate", ""},
        {"adjust", "faster"},
    };
    for (const auto& control : controls) {
        shutter_command[0] = '\0';
        if (!test_shutter_mcp_control(control.command, control.value, true, &error) ||
            std::strcmp(shutter_command, control.command) ||
            shutter_dispatch_task != reinterpret_cast<TaskHandle_t>(1)) {
            std::printf("FAIL: shutter MCP command %s (error %d, dispatched %s)\n",
                        control.command, error, shutter_command);
            return false;
        }
        if (!std::strcmp(control.command, "adjust") && std::strcmp(shutter_value, "faster")) return false;
    }
    shutter_command[0] = '\0';
    if (test_shutter_mcp_control("adjust", "invalid", true, &error) ||
        error != -32602 || shutter_command[0]) return false;
    shutter_command[0] = '\0';
    if (test_shutter_mcp_control("unknown", "", true, &error) || error != -32602 ||
        shutter_command[0]) return false;
    shutter_accept = false;
    bool accepted = test_shutter_mcp_control("sess_start", "camera", true, &error);
    shutter_accept = true;
    if (accepted || error != -32603) return false;
    shutter_command[0] = '\0';
    if (test_shutter_mcp_control("sess_start", "later", false, &error) ||
        error != -32603 || shutter_command[0]) return false;
    if (test_shutter_mcp_control("sess_stop", "", true, &error) || error != -32001) return false;
    test_shutter_mcp_drain();
    if (std::strcmp(shutter_command, "sess_start") ||
        std::strcmp(shutter_value, "later") ||
        shutter_dispatch_task != reinterpret_cast<TaskHandle_t>(1)) return false;
    shutter_command[0] = '\0';
    return test_shutter_mcp_control("sess_stop", "", true, &error) &&
        !std::strcmp(shutter_command, "sess_stop") &&
        shutter_dispatch_task == reinterpret_cast<TaskHandle_t>(1);
}

static bool shutter_session_hook_storage() {
    const char* path = "/config/shutter_session_actions.json";
    const char* legacy = R"({"save_start_actions":[{"type":"notify"}],"save_complete_actions":[]})";
    storage_test_files[path] = legacy;
    std::string serialized;
    config_alloc_failure = true;
    shutter_session_actions_init();
    const bool allocation_unavailable = !shutter_session_actions_to_json(serialized);
    config_alloc_failure = false;
    if (!allocation_unavailable) return false;
    storage_test_fail_open = true;
    shutter_session_actions_init();
    const bool read_unavailable = !shutter_session_actions_to_json(serialized);
    storage_test_fail_open = false;
    if (!read_unavailable) return false;
    shutter_session_actions_init();
    JsonDocument reset;
    if (!shutter_session_actions_to_json(serialized) || deserializeJson(reset, serialized) ||
        reset["config_reset"] != true || reset["save_start_actions"].size() != 0 ||
        reset["save_complete_actions"].size() != 0 || storage_test_files[path] != legacy) return false;
    const char* incompatible = R"({"action_format_version":1,
        "save_start_actions":[{"op":"shutter.alignment.start","args":{}}],
        "save_complete_actions":[{"op":"missing.operation","args":{}}]})";
    storage_test_files[path] = incompatible;
    shutter_session_actions_init();
    if (!shutter_session_actions_to_json(serialized) || deserializeJson(reset, serialized) ||
        reset["config_reset"] != true || reset["save_start_actions"].size() != 0 ||
        reset["save_complete_actions"].size() != 0 || storage_test_files[path] != incompatible) return false;
    const char* valid = R"({"action_format_version":1,"save_start_actions":[{"op":"test.number","args":{"value":{"$template":"[var:number]"}}}],"save_complete_actions":[{"op":"shutter.alignment.start","args":{}}]})";
    shutter_write_failure = true;
    const bool failed_save = shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(valid), strlen(valid));
    shutter_write_failure = false;
    if (failed_save || storage_test_files[path] != incompatible ||
        !shutter_session_actions_to_json(serialized) || deserializeJson(reset, serialized) ||
        reset["config_reset"] != true) return false;
    if (!shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(valid), strlen(valid))) return false;
    shutter_session_actions_init();
    if (!shutter_session_actions_to_json(serialized) || deserializeJson(reset, serialized) ||
        reset["config_reset"] != false) return false;
    if (!shutter_session_actions_to_json(serialized) || serialized.find("[var:number]") == std::string::npos)
        return false;
    const char* full = R"({"action_format_version":1,"save_start_actions":[{"op":"test.number","args":{"value":1}},{"op":"test.number","args":{"value":2}},{"op":"test.number","args":{"value":3}}],"save_complete_actions":[{"op":"shutter.alignment.start","args":{}},{"op":"shutter.alignment.stop","args":{}},{"op":"shutter.recalibrate","args":{}}]})";
    if (!shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(full), strlen(full)) ||
        !shutter_session_actions_to_json(serialized)) return false;
    JsonDocument round_trip;
    if (deserializeJson(round_trip, serialized) ||
        round_trip["save_start_actions"].size() != 3 ||
        round_trip["save_complete_actions"].size() != 3 ||
        round_trip["save_start_actions"][2]["args"]["value"] != 3 ||
        round_trip["save_complete_actions"][2]["op"] != "shutter.recalibrate") return false;
    struct GuidedHook { const char* op; const char* command; const char* argument; };
    const GuidedHook guided_hooks[] = {
        {"shutter.guide.start", "guide_start", "test"},
        {"shutter.guide.stop", "guide_stop", nullptr},
        {"shutter.guide.skip", "guide_skip", nullptr},
    };
    for (const GuidedHook& item : guided_hooks) {
        for (const char* event : {"save_start_actions", "save_complete_actions"}) {
            JsonDocument input;
            input["action_format_version"] = 1;
            input["save_start_actions"].to<JsonArray>();
            input["save_complete_actions"].to<JsonArray>();
            JsonObject action = input[event].as<JsonArray>().add<JsonObject>();
            action["op"] = item.op;
            JsonObject args = action["args"].to<JsonObject>();
            if (item.argument) args[item.argument] = "test";
            std::string json;
            serializeJson(input, json);
            if (!shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(json.data()), json.size()))
                return false;
            shutter_command[0] = '\0';
            if (!strcmp(event, "save_start_actions")) shutter_session_actions_dispatch_start();
            else {
                shutter_session_actions_notify_complete();
                shutter_session_actions_loop();
            }
            if (strcmp(shutter_command, item.command)) return false;
        }
    }
    if (!shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(valid), strlen(valid))) return false;
    const char* invalid[] = {
        legacy,
        R"({"action_format_version":1,"save_start_actions":[{},{"op":"shutter.alignment.start","args":{}}],"save_complete_actions":[]})",
        R"({"action_format_version":1,"save_start_actions":[{"op":"shutter.alignment.start","args":{}},{"op":"shutter.alignment.start","args":{}},{"op":"shutter.alignment.start","args":{}},{"op":"shutter.alignment.start","args":{}}],"save_complete_actions":[]})",
        R"({"action_format_version":1,"save_start_actions":[{"op":"shutter.session.stop","args":{}}],"save_complete_actions":[]})",
        R"({"action_format_version":1,"save_start_actions":[{"op":"test.number","args":{"value":true}}],"save_complete_actions":[]})",
        R"({"action_format_version":1,"save_start_actions":[],"save_start_actions\u0000x":[{"type":"notify"}],"save_complete_actions":[]})"
    };
    for (const char* json : invalid) {
        if (shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json)) ||
            storage_test_files[path] != valid) return false;
    }
    struct Forbidden { const char* op; const char* argument; };
    const Forbidden forbidden[] = {
        {"shutter.session.start", "camera"},
        {"shutter.session.stop", nullptr},
        {"shutter.session.toggle", "camera"},
    };
    for (const Forbidden& item : forbidden) {
        for (const char* event : {"save_start_actions", "save_complete_actions"}) {
            JsonDocument input;
            input["action_format_version"] = 1;
            input["save_start_actions"].to<JsonArray>();
            input["save_complete_actions"].to<JsonArray>();
            JsonObject action = input[event].as<JsonArray>().add<JsonObject>();
            action["op"] = item.op;
            JsonObject args = action["args"].to<JsonObject>();
            if (item.argument) args[item.argument] = "test";
            if (action_operation_validate_json(action, false)) return false;
            std::string json;
            serializeJson(input, json);
            if (shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(json.data()), json.size()) ||
                storage_test_files[path] != valid) return false;
        }
    }
    shutter_write_failure = true;
    const bool saved = shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(valid), strlen(valid));
    shutter_write_failure = false;
    if (saved || storage_test_files[path] != valid) return false;
    runtime_variable_set("number", "2.5");
    captured_number = 0;
    shutter_session_actions_dispatch_start();
    if (captured_number != 2.5f) return false;
    shutter_command[0] = '\0';
    shutter_session_actions_notify_complete();
    shutter_session_actions_loop();
    if (std::strcmp(shutter_command, "align_start")) return false;
    const char* independent = R"({"action_format_version":1,"save_start_actions":[{"op":"test.number","args":{"value":{"$template":"[var:number]"}}},{"op":"shutter.alignment.start","args":{}}],"save_complete_actions":[]})";
    if (!shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(independent), strlen(independent)))
        return false;
    runtime_variable_set("number", "not-a-number");
    shutter_command[0] = '\0';
    shutter_session_actions_dispatch_start();
    if (std::strcmp(shutter_command, "align_start")) return false;
    const char* pending = R"({"action_format_version":1,"save_start_actions":[{"op":"delay.wait","args":{"duration_ms":100}},{"op":"shutter.alignment.start","args":{}}],"save_complete_actions":[]})";
    if (!shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(pending), strlen(pending)))
        return false;
    shutter_command[0] = '\0';
    shutter_session_actions_dispatch_start();
    if (std::strcmp(shutter_command, "align_start") ||
        !shutter_session_actions_save_raw(reinterpret_cast<const uint8_t*>(valid), strlen(valid))) return false;
    shutter_command[0] = '\0';
    test_now_ms += 100;
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LOOP);
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LOOP);
    return !shutter_command[0];
}
#endif

static bool voice_note_command_operations() {
    struct Case { const char* id; VoiceNoteCommand command; };
    const Case cases[] = {
        {"voice_note.record.toggle", VoiceNoteCommand::Toggle},
        {"voice_note.record.start", VoiceNoteCommand::Start},
        {"voice_note.record.pause", VoiceNoteCommand::Pause},
        {"voice_note.record.resume", VoiceNoteCommand::Resume},
        {"voice_note.record.save", VoiceNoteCommand::Save},
        {"voice_note.record.discard", VoiceNoteCommand::Discard},
    };
    for (const Case& test : cases) {
        JsonDocument input;
        input["op"] = test.id;
        input["args"].to<JsonObject>();
        ActionStorage storage = {};
        ButtonAction actions[2] = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), actions[0]);
        }
        actions[1] = set_variable_action("voice_after", "done");
        runtime_variable_set("voice_after", "before");
        accept_record = false;
        bool ok = !storage.failed &&
            action_list_dispatch_result(actions, 2, "record") == ACTION_FAILED &&
            get_equals("voice_after", "before");
        accept_record = true;
        recorded_token = 0;
        ok = ok && action_list_dispatch_result(actions, 2, "record") == ACTION_PENDING &&
            recorded_command == test.command && recorded_token && get_equals("voice_after", "before");
        if (ok) {
            ok = action_continuation_complete(recorded_token, true);
            action_list_dispatch_continuation();
            ok = ok && get_equals("voice_after", "done");
        }
        action_storage_clear(storage);
        if (!ok) return false;
    }

    JsonDocument input;
    input["op"] = "voice_note.process";
    input["args"].to<JsonObject>();
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), actions[0]);
    }
    actions[1] = set_variable_action("voice_after", "done");
    runtime_variable_set("voice_after", "before");
    processing_result = VoiceNotesProcessingStart::Busy;
    bool ok = !storage.failed &&
        action_list_dispatch_result(actions, 2, "process") == ACTION_FAILED &&
        get_equals("voice_after", "before");
    processing_result = VoiceNotesProcessingStart::Accepted;
    ok = ok && action_list_dispatch_result(actions, 2, "process") == ACTION_COMPLETE &&
        get_equals("voice_after", "done");
    action_storage_clear(storage);
    return ok;
}
#endif

static bool variable_operation_boundaries() {
    for (const char* json : {
            R"({"op":"@variable.set","args":{"name":"counter","value":"1"}})",
            R"({"op\u0000other":"variable.set","args":{"name":"counter","value":"1"}})",
            R"({"op":"variable.set","args":{"name\u0000other":"counter","value":"1"}})",
            R"({"op":"variable.set","args":{"name":"counter","value":{"$template\u0000other":"[var:counter]"}}})"}) {
        JsonDocument invalid;
        if (deserializeJson(invalid, json) ||
            !action_operation_validate_json(invalid.as<JsonObjectConst>(), false)) return false;
    }
    JsonDocument input;
    if (deserializeJson(input, R"({"op":"variable.set","args":{"name":"counter","value":"[var:counter]"}})"))
        return false;
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    JsonDocument round_trip;
    action_to_json(action, round_trip.to<JsonObject>());
    bool ok = !storage.failed && round_trip["args"]["value"] == "[var:counter]" &&
              action_dispatch(action, "literal") == ACTION_COMPLETE &&
              get_equals("counter", "[var:counter]");
    action_storage_clear(storage);

    input["args"]["value"] = "";
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    ok = ok && !storage.failed && action_dispatch(action, "empty") == ACTION_COMPLETE &&
         get_equals("counter", "");
    action_storage_clear(storage);

    input["args"]["name"] = "bad/name";
    ok = ok && action_operation_validate_json(input.as<JsonObjectConst>());
    input["args"]["name"] = "counter";
    input["args"]["value"].to<JsonObject>()["$template"] = "[expr:[var:counter]+1]";
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    runtime_variable_set("counter", "0");
    std::atomic<int> failures{0};
    std::thread workers[4];
    for (auto& worker : workers) {
        worker = std::thread([&] {
            for (int i = 0; i < 100; ++i)
                if (action_dispatch(action, "typed increment") != ACTION_COMPLETE) ++failures;
        });
    }
    for (auto& worker : workers) worker.join();
    ok = ok && !storage.failed && !failures && get_equals("counter", "400");
    action_storage_clear(storage);

    input["args"]["value"]["$template"] = "[var:not_set]";
    {
        ActionStorageScope scope(&storage);
        action_parse(input.as<JsonObject>(), action);
    }
    ok = ok && action_dispatch(action, "missing") == ACTION_FAILED && get_equals("counter", "400");
    action_storage_clear(storage);
    return ok;
}

#if HAS_DISPLAY && HAS_PSRAM
static bool item_action_validation() {
    static const ListSourceField fields[] = {
        {"id", "Stable ID", ListSourceFieldType::String, false},
        {"title", "Title", ListSourceFieldType::String, true},
    };
    static const ListSourceDescriptor source = {"[test:items]", "Test", fields, 2};
    PadBinding alias = {};
    strlcpy(alias.name, "chosen", sizeof(alias.name));
    strlcpy(alias.value, "[item:id]", sizeof(alias.value));
    JsonDocument input;
    if (deserializeJson(input, R"([
        {"op":"variable.set","args":{"name":"chosen","value":{"$template":"[pad:chosen]"}}},
        {"op":"test.boundary","args":{}},
        {"op":"variable.set","args":{"name":"later","value":{"$template":"[var:chosen]"}}}
    ])")) return false;
    JsonArray actions = input.as<JsonArray>();
    if (item_binding_validate_actions(actions, &source, &alias, 1)) return false;
    actions[2]["args"]["value"]["$template"] = "[pad:chosen]";
    if (item_binding_validate_actions(actions, &source, &alias, 1)) return false;
    actions[0]["args"]["value"]["$template"] = "[item:unknown]";
    if (item_binding_validate_actions(actions, &source, &alias, 1)) return false;
    actions[0]["args"]["value"]["$template"] = "[item:id]";
    if (!item_binding_validate_actions(actions, nullptr, &alias, 1)) return false;
    actions[0]["type"] = "set_var";
    return item_binding_validate_actions(actions, &source, &alias, 1) != nullptr;
}
#endif

#if HAS_DISPLAY
static bool synchronous_operation_locks() {
    JsonDocument input;
    if (deserializeJson(input, R"([
        {"op":"test.wide","args":{"target":"screen","number":1,"enabled":true}},
        {"op":"test.number","args":{"value":{"$template":"[var:lock_test]"}}}
    ])")) return false;
    ActionStorage storage = {};
    ButtonAction actions[2] = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(input[0].as<JsonObject>(), actions[0]);
        action_parse(input[1].as<JsonObject>(), actions[1]);
    }
    if (storage.failed) {
        action_storage_clear(storage);
        return false;
    }
    check_dispatch_lock = true;
    unsigned before = test_display_locks;
    bool ok = action_dispatch_synchronous(actions[0], "ui", nullptr, 0) == ACTION_FAILED &&
              !test_display_locked && test_display_locks == before + 2;
    test_display_locked = true;
    before = test_display_locks;
    ok = ok && action_dispatch_synchronous(actions[0], "ui locked", nullptr, 0) == ACTION_FAILED &&
         test_display_locked && test_display_locks == before;
    test_display_locked = false;
    runtime_variable_set("lock_test", "1.5");
    before = test_display_locks;
    const unsigned calls = number_calls;
    ok = ok && action_dispatch_synchronous(actions[1], "network", nullptr, 0) == ACTION_COMPLETE &&
         number_calls == calls + 1 && test_display_locks == before + 1 && !test_display_locked;
    runtime_variable_set("lock_test", "invalid");
    before = test_display_locks;
    ok = ok && action_dispatch_synchronous(actions[1], "invalid", nullptr, 0) == ACTION_FAILED &&
         number_calls == calls + 1 && test_display_locks == before + 1 && !test_display_locked &&
         !action_dispatch_context().synchronous;
    check_dispatch_lock = false;
    number_calls = calls;
    action_storage_clear(storage);
    return ok;
}
#endif

int main() {
    expr_binding_init();
    var_binding_init();
    action_type_register(&sync_context_action);
    action_type_register(&wide_type);
    action_type_register(&number_type);
#if HAS_DISPLAY
    if (!synchronous_operation_locks()) {
        std::puts("FAIL: synchronous operation lock ownership");
        return 1;
    }
#endif
    if (!number_operation_dispatch()) {
        std::puts("FAIL: numeric operation arguments");
        return 1;
    }
    if (!wide_operation_dispatch()) {
        std::puts("FAIL: bounded operation arguments through production dispatch");
        return 1;
    }
#if HAS_DISPLAY && HAS_PSRAM && TEST_RELAY_HOST
    if (!relay_host_operations() || !darkroom_timer_operations()) {
        std::puts("FAIL: relay host operations");
        return 1;
    }
    return 0;
#elif HAS_DISPLAY && HAS_PSRAM && IS_DARKROOM_TIMER
    if (!darkroom_simple_operations() || !darkroom_timer_operations()) {
        std::puts("FAIL: darkroom operations");
        return 1;
    }
    if (!darkroom_mcp_bridge()) {
        std::puts("FAIL: darkroom MCP bridge");
        return 1;
    }
    return 0;
#endif
#if HAS_DISPLAY && HAS_PSRAM && TEST_SHUTTER_OPERATIONS
    if (!shutter_operations() || !shutter_session_hook_storage() || !shutter_mcp_bridge()) {
        std::puts("FAIL: shutter operations");
        return 1;
    }
    return 0;
#endif
#if TEST_STOPWATCH_OPERATIONS || TEST_STICKY_OPERATIONS
    if (!board_shutdown_operation()
#if TEST_STOPWATCH_OPERATIONS
        || !stopwatch_operation_boundaries()
#endif
    ) {
        std::puts("FAIL: board control operation lifecycle");
        return 1;
    }
    return 0;
#endif
    if (!malformed_operation_templates()) {
        std::puts("FAIL: malformed dynamic operation templates");
        return 1;
    }
    JsonDocument sync_document;
    sync_document["op"] = "test.sync_context";
    sync_document.createNestedObject("args");
    sync_action = make_test_operation(sync_document.as<JsonObject>());
    if (action_dispatch_synchronous(sync_action, "outer", outer_guard, 101, true) != ACTION_COMPLETE) {
        std::puts("FAIL: nested synchronous action context");
        return 1;
    }
    const ActionDispatchContext restored_context = action_dispatch_context();
    if (restored_context.synchronous || restored_context.work_guard || restored_context.generation ||
        restored_context.alarm_audio || restored_context.interactive) {
        std::puts("FAIL: synchronous action context restored after dispatch");
        return 1;
    }
#if HAS_DISPLAY && HAS_PSRAM
    item_binding_init();
    pad_binding_init();
    event_binding_init();
    action_type_register(&capture_type);
    action_type_register(&suspend_type);
    action_type_register(&nested_action);
    action_type_register(&boundary_action);
    action_type_register(&typed_type);
    action_type_register(&pause_type);
#if IS_VOICE_ASSISTANT
    if (!voice_assistant_operations()) {
        std::printf("FAIL: voice assistant operations\n");
        return 1;
    }
#endif
#if IS_DARKROOM_TIMER
    if (!darkroom_simple_operations()) {
        std::printf("FAIL: darkroom simple operations\n");
        return 1;
    }
    if (!darkroom_timer_operations()) {
        std::puts("FAIL: darkroom timer operations");
        return 1;
    }
#endif
#if IS_COFFEE_SCALE
    if (!coffee_operations() || !coffee_mcp_mappings()) {
        std::printf("FAIL: coffee operations\n");
        return 1;
    }
    if (!coffee_mcp_bridge()) {
        std::puts("FAIL: coffee MCP bridge");
        return 1;
    }
#endif
    if (!voice_note_command_operations()) {
        std::puts("FAIL: voice-note command operation dispatch");
        return 1;
    }
    if (!argument_allocation_failure()) {
        std::puts("FAIL: argument allocation failure");
        return 1;
    }
    if (!typed_operation_validation()) {
        std::puts("FAIL: strict boolean and enum operation arguments");
        return 1;
    }
    if (!operation_boundaries()) {
        std::puts("FAIL: screen and timer operation boundaries");
        return 1;
    }
    if (!navigation_operation_boundaries()) {
        std::puts("FAIL: navigation and image operation boundaries");
        return 1;
    }
    if (!hid_operation_boundaries()) {
        std::puts("FAIL: keyboard, mouse, gamepad and BLE operation boundaries");
        return 1;
    }
    if (!dialog_and_display_operation_boundaries()) {
        std::puts("FAIL: dialog and display operation dispatch");
        return 1;
    }
    if (!alarm_operation_boundaries()) {
        std::puts("FAIL: normalized alarm operations and queue rejection");
        return 1;
    }
    if (!audio_alert_operation_boundaries()) {
        std::puts("FAIL: normalized audio alert operations and queue rejection");
        return 1;
    }
    if (!messaging_operation_boundaries()) {
        std::puts("FAIL: messaging operation dispatch and suffix failure");
        return 1;
    }
    if (!wifi_operation_boundaries()) {
        std::puts("FAIL: Wi-Fi operation dispatch and suffix failure");
        return 1;
    }
    if (!system_and_saved_level_operations()) {
        std::puts("FAIL: system and saved level operations");
        return 1;
    }
    if (!brightness_operation_boundaries()) {
        std::puts("FAIL: brightness operations and unavailable dynamic value");
        return 1;
    }
    if (!back_operation_boundaries()) {
        std::puts("FAIL: back operation");
        return 1;
    }
    if (!music_operation_boundaries()) {
        std::puts("FAIL: music operations and rejection");
        return 1;
    }
    if (!camera_operation_boundaries()) {
        std::puts("FAIL: camera operations and rejection");
        return 1;
    }
    if (!volume_operation_boundaries()) {
        std::puts("FAIL: volume operations and unavailable dynamic value");
        return 1;
    }
    if (!captured_run_survives_pause()) {
        std::puts("FAIL: operation context across pause");
        return 1;
    }
    if (!real_operations_survive_pause()) {
        std::puts("FAIL: real operation context across pause");
        return 1;
    }
    if (!delay_operation_retains_item()) {
        std::puts("FAIL: delay operation and captured item");
        return 1;
    }
    if (!timer_operation_commands()) {
        std::puts("FAIL: timer operation commands");
        return 1;
    }
    if (!typed_continuation_contract()) {
        std::puts("FAIL: typed continuation and full-pool dispatch");
        return 1;
    }
#if HAS_MCP
    if (!observed_ha_preserves_context_and_resolves_live()) {
        std::puts("FAIL: tracked HA context and live resolution");
        return 1;
    }
#endif
    if (!synchronous_navigation_survives_replacement()) {
        std::puts("FAIL: synchronous navigation and configuration replacement");
        return 1;
    }
    if (!item_action_validation()) {
        std::puts("FAIL: item action validation");
        return 1;
    }
    ListSourceSnapshot* snapshot = ListSourceSnapshot::create();
    if (!snapshot) {
        std::puts("FAIL: create item snapshot");
        return 1;
    }
    JsonDocument input;
    input["id"] = "item-1";
    if (snapshot->append(input.as<JsonObjectConst>()) || snapshot->seal()) {
        snapshot->release();
        std::puts("FAIL: populate item snapshot");
        return 1;
    }
    {
        ListSourceSnapshot* ids = ListSourceSnapshot::create();
        JsonDocument row;
        row["id"] = "001";
        row["title"] = "a\"b\\c";
        row["numeric"] = 2;
        if (!ids || ids->append(row.as<JsonObjectConst>()) || ids->seal()) {
            if (ids) ids->release();
            snapshot->release();
            std::puts("FAIL: create string ID snapshot");
            return 1;
        }
        PadBinding alias = {};
        strlcpy(alias.name, "selected", sizeof(alias.name));
        strlcpy(alias.value, "[item:id]", sizeof(alias.value));
        pad_binding_set_bindings(&alias, 1);
        {
            ItemBindingScope scope(ids, 0);
            if (action_dispatch(set_variable_action("item_test",
                    "[expr:[item:id] == \"001\" ? \"yes\" : \"no\"]"), "item") != ACTION_COMPLETE ||
                !get_equals("item_test", "yes") ||
                action_dispatch(set_variable_action("item_test",
                    "[expr:[pad:selected] == \"001\" ? \"yes\" : \"no\"]"), "item") != ACTION_COMPLETE ||
                !get_equals("item_test", "yes") ||
                action_dispatch(set_variable_action("item_test",
                    "[expr:[item:id|other] == \"001\" ? \"yes\" : \"no\"]"), "item") != ACTION_COMPLETE ||
                !get_equals("item_test", "yes") ||
                action_dispatch(set_variable_action("item_test",
                    "[expr:[item:title]]"), "item") != ACTION_COMPLETE ||
                !get_equals("item_test", "a\"b\\c") ||
                action_dispatch(set_variable_action("item_test",
                    "[expr:[item:numeric]+1]"), "item") != ACTION_COMPLETE ||
                !get_equals("item_test", "3")) {
                pad_binding_set_bindings(nullptr, 0);
                ids->release();
                snapshot->release();
                std::puts("FAIL: checked expression preserves string IDs directly and through pad aliases");
                return 1;
            }
        }
        pad_binding_set_bindings(nullptr, 0);
        ids->release();
    }
    {
        ListSourceSnapshot* notes = ListSourceSnapshot::create();
        JsonDocument note;
        note["id"] = "0123456789abcdef";
        if (!notes || notes->append(note.as<JsonObjectConst>()) || notes->seal()) {
            if (notes) notes->release();
            snapshot->release();
            std::puts("FAIL: create note snapshot");
            return 1;
        }
        JsonDocument typed;
        deserializeJson(typed, R"([
            {"op":"voice_note.load","args":{"id":{"$template":"[item:id]"}}},
            {"op":"voice_note.delete","args":{"id":{"$template":"[item:id]"}}}
        ])");
        ActionStorage storage = {};
        ButtonAction operations[2] = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(typed[0].as<JsonObject>(), operations[0]);
            action_parse(typed[1].as<JsonObject>(), operations[1]);
        }
        runtime_variable_set("after_load", "before");
        runtime_variable_set("after_delete", "before");
        const ButtonAction load_actions[] = {operations[0], set_variable_action("after_load", "done")};
        const ButtonAction delete_actions[] = {operations[1], set_variable_action("after_delete", "typed")};
        delete_exec = nullptr;
        bool typed_ok = !storage.failed;
        {
            ItemBindingScope scope(notes, 0);
            typed_ok = typed_ok &&
                action_list_dispatch_result(load_actions, 2, "typed load") == ACTION_COMPLETE &&
                !std::strcmp(requested_note, "0123456789abcdef") && get_equals("after_load", "done");
            accept_load = false;
            runtime_variable_set("after_load", "before");
            typed_ok = typed_ok &&
                action_list_dispatch_result(load_actions, 2, "rejected load") == ACTION_FAILED &&
                get_equals("after_load", "before");
            accept_load = true;
            typed_ok = typed_ok &&
                action_list_dispatch_result(delete_actions, 2, "typed delete") == ACTION_PENDING &&
                delete_exec && get_equals("after_delete", "before");
        }
        if (typed_ok) {
            bool ok = false;
            delete_exec(delete_job, &ok, nullptr, 0);
            action_list_dispatch_continuation();
            typed_ok = ok && !std::strcmp(deleted_note, "0123456789abcdef") &&
                get_equals("after_delete", "typed") && cleared_notes == 1 &&
                !std::strcmp(cleared_note, deleted_note);
        }
        {
            ItemBindingScope scope(notes, 0);
            runtime_variable_set("after_delete", "before");
            bridge_reject = true;
            typed_ok = typed_ok &&
                action_list_dispatch_result(delete_actions, 2, "busy delete") == ACTION_FAILED &&
                get_equals("after_delete", "before") && cleared_notes == 1;
            bridge_reject = false;
            delete_result = VoiceNoteFileStatus::Busy;
            typed_ok = typed_ok &&
                action_list_dispatch_result(delete_actions, 2, "failed delete") == ACTION_PENDING;
        }
        if (typed_ok) {
            bool ok = true;
            delete_exec(delete_job, &ok, nullptr, 0);
            action_list_dispatch_continuation();
            typed_ok = !ok && get_equals("after_delete", "before") && cleared_notes == 1;
        }
        delete_result = VoiceNoteFileStatus::Ready;
        {
            ItemBindingScope scope(snapshot, 0);
            runtime_variable_set("after_load", "before");
            typed_ok = typed_ok &&
                action_list_dispatch_result(load_actions, 2, "invalid note") == ACTION_FAILED &&
                get_equals("after_load", "before") &&
                !std::strcmp(requested_note, "0123456789abcdef");
        }
        action_storage_clear(storage);
        notes->release();
        if (!typed_ok) {
            snapshot->release();
            std::puts("FAIL: typed note operations normalize item IDs and honor completion");
            return 1;
        }
    }
    {
        ItemBindingScope outer(snapshot, 0);
        char item_value[32];
        if (action_dispatch_synchronous(sync_action, "outer", outer_guard, 101, true) != ACTION_COMPLETE) {
            snapshot->release();
            std::puts("FAIL: synchronous dispatch with item scope");
            return 1;
        }
        binding_template_resolve("[item:id|none]", item_value, sizeof(item_value));
        if (std::strcmp(item_value, "item-1") != 0) {
            snapshot->release();
            std::puts("FAIL: synchronous dispatch preserves item context");
            return 1;
        }
        {
            ItemBindingScope inner(snapshot, 0);
            JsonDocument boundary_document;
            boundary_document["op"] = "test.boundary";
            boundary_document["args"]["target"]["$template"] = "[item:id]";
            ButtonAction boundary = make_test_operation(boundary_document.as<JsonObject>());
            const ButtonAction actions[] = {
                set_variable_action("captured", "[item:id]"), boundary,
                set_variable_action("after_boundary", "[item:id|none]"),
            };
            if (action_list_dispatch_result(actions, 3, "item") != ACTION_COMPLETE ||
                !get_equals("captured", "item-1") || !get_equals("after_boundary", "item-1") ||
                !boundary_received_item || !nested_item_retained) {
                snapshot->release();
                std::puts("FAIL: nested and subsequent operations retain item context across the boundary");
                return 1;
            }
        }
        char value[32];
        binding_template_resolve("[item:id|none]", value, sizeof(value));
        if (std::strcmp(value, "item-1") != 0) {
            snapshot->release();
            std::puts("FAIL: nested operation preserves the outer item context");
            return 1;
        }
    }
    {
        ItemBindingScope scope(snapshot, 0);
        ButtonAction unknown = {};
        strlcpy(unknown.type, "unknown_action", sizeof(unknown.type));
        const ActionResult result = action_dispatch(unknown, "unknown");
        char value[32];
        binding_template_resolve("[item:id|none]", value, sizeof(value));
        if (result != ACTION_FAILED || std::strcmp(value, "item-1") != 0) {
            snapshot->release();
            std::puts("FAIL: unknown action fails without changing item context");
            return 1;
        }
    }
    snapshot->release();
#endif
    if (!action_type_find(ACTION_TYPE_SET_VAR)) {
        std::puts("FAIL: Set variable action is registered");
        return 1;
    }

    const ButtonAction increment = set_variable_action(
        "counter", "[expr:[var:counter|0] + 1]");
    char value[RUNTIME_VARIABLE_VALUE_CAPACITY] = {};
    if (runtime_variable_get("counter", value, sizeof(value)) != RUNTIME_VARIABLE_MISSING ||
        action_dispatch(increment, "counter") != ACTION_COMPLETE || !get_equals("counter", "1") ||
        action_dispatch(increment, "counter") != ACTION_COMPLETE || !get_equals("counter", "2")) {
        std::puts("FAIL: counter advances from missing to one and then two");
        return 1;
    }
    if (action_dispatch(set_variable_action("counter", "0"), "counter") != ACTION_COMPLETE ||
        !get_equals("counter", "0")) {
        std::puts("FAIL: literal reset action stores zero");
        return 1;
    }
    const ButtonAction sequential[] = {increment, increment};
    if (action_list_dispatch_result(sequential, 2, "counter") != ACTION_COMPLETE ||
        !get_equals("counter", "2")) {
        std::puts("FAIL: sequential actions observe preceding writes");
        return 1;
    }
    const ButtonAction toggle = set_variable_action(
        "enabled", "[expr:[var:enabled|0] == 0 ? 1 : 0]");
    if (action_dispatch(toggle, "enabled") != ACTION_COMPLETE || !get_equals("enabled", "1") ||
        action_dispatch(toggle, "enabled") != ACTION_COMPLETE || !get_equals("enabled", "0")) {
        std::puts("FAIL: toggle expression switches between zero and one");
        return 1;
    }
    if (action_dispatch(set_variable_action("empty_value", ""), "empty_value") != ACTION_COMPLETE ||
        runtime_variable_get("empty_value", value, sizeof(value)) != RUNTIME_VARIABLE_FOUND || value[0]) {
        std::puts("FAIL: action stores a valid empty string");
        return 1;
    }
    if (runtime_variable_set("literal_dash", "---") != RUNTIME_VARIABLE_STORED ||
        action_dispatch(set_variable_action("checked_dash", "[expr:[var:literal_dash]]"), "checked_dash") != ACTION_COMPLETE ||
        !get_equals("checked_dash", "---")) {
        std::puts("FAIL: checked expression distinguishes literal placeholder text from missing data");
        return 1;
    }
    char long_text[120];
    std::memset(long_text, '0', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    if (runtime_variable_set("long_text", long_text) != RUNTIME_VARIABLE_STORED ||
        action_dispatch(set_variable_action("checked_dash",
                        "[expr:1+[var:long_text]+[var:long_text]+1]"), "checked_dash") != ACTION_FAILED ||
        !get_equals("checked_dash", "---")) {
        std::puts("FAIL: checked expression rejects truncated intermediate expansion");
        return 1;
    }
    if (runtime_variable_set("quoted", "a\"b\\c") != RUNTIME_VARIABLE_STORED ||
        action_dispatch(set_variable_action("checked_dash", "[expr:[var:quoted]]"), "checked_dash") != ACTION_COMPLETE ||
        !get_equals("checked_dash", "a\"b\\c")) {
        std::puts("FAIL: checked expression preserves quoted binding data");
        return 1;
    }
    if (action_dispatch(set_variable_action("checked_dash", "[expr:\"a\\\";b|c\"]"), "checked_dash") != ACTION_COMPLETE ||
        !get_equals("checked_dash", "a\";b|c")) {
        std::puts("FAIL: checked expression preserves delimiters after escaped quotes");
        return 1;
    }
    if (action_dispatch(set_variable_action("precise", "[expr:1000000000000001]"), "precise") != ACTION_COMPLETE ||
        !get_equals("precise", "1000000000000001") ||
        action_dispatch(set_variable_action("precise", "[expr:0.123456789]"), "precise") != ACTION_COMPLETE ||
        !get_equals("precise", "0.123456789")) {
        std::puts("FAIL: checked expression preserves numeric precision");
        return 1;
    }
    char overlong_expr[160] = "[expr:\"";
    std::memset(overlong_expr + 7, 'x', 128);
    std::strcpy(overlong_expr + 135, "\"]");
    if (action_dispatch(set_variable_action("checked_dash", overlong_expr), "checked_dash") != ACTION_FAILED ||
        !get_equals("checked_dash", "a\";b|c")) {
        std::puts("FAIL: checked expression rejects an overlong string result");
        return 1;
    }
    runtime_variable_set("counter", "0");

    const unsigned before_refresh = binding_refreshes;
    if (action_list_dispatch_result(sequential, 2, "User", ACTION_CONTINUATION_OWNER_LVGL, true) != ACTION_COMPLETE ||
        !get_equals("counter", "2") ||
        binding_refreshes != before_refresh + (HAS_DISPLAY ? 2 : 0) ||
        action_dispatch_context().interactive) {
        std::puts("FAIL: user mutations notify the UI without leaking interaction origin");
        return 1;
    }
    const unsigned after_refresh = binding_refreshes;
    if (action_dispatch(set_variable_action("counter", "2"), "Unchanged", 0, true) != ACTION_COMPLETE ||
        action_dispatch(set_variable_action("counter", "[var:missing]"), "Failed", 0, true) != ACTION_FAILED ||
        !get_equals("counter", "2") || binding_refreshes != after_refresh) {
        std::puts("FAIL: unchanged or failed assignments do not request immediate UI work");
        return 1;
    }
    if (action_dispatch(increment, "Background") != ACTION_COMPLETE ||
        binding_refreshes != after_refresh) {
        std::puts("FAIL: unchanged or background writes do not request immediate UI work");
        return 1;
    }
    ButtonAction partial[] = {increment, set_variable_action("counter", "[var:missing]")};
    if (action_list_dispatch_result(partial, 2, "Partial", ACTION_CONTINUATION_OWNER_LVGL, true) != ACTION_FAILED ||
        !get_equals("counter", "4") ||
        binding_refreshes != after_refresh + (HAS_DISPLAY ? 1 : 0) ||
        action_dispatch_context().interactive) {
        std::puts("FAIL: successful mutations remain refreshable after a later action fails");
        return 1;
    }
    runtime_variable_set("counter", "0");

    std::atomic<int> failures{0};
    constexpr int kWorkers = 4;
    constexpr int kIncrementsPerWorker = 100;
    std::thread workers[kWorkers];
    for (int worker = 0; worker < kWorkers; ++worker) {
        workers[worker] = std::thread([&] {
            for (int index = 0; index < kIncrementsPerWorker; ++index) {
                if (action_dispatch(increment, "counter") != ACTION_COMPLETE) ++failures;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    if (failures || !get_equals("counter", "400")) {
        std::puts("FAIL: concurrent read-modify-write actions retain every increment");
        return 1;
    }
    if (!variable_operation_boundaries()) {
        std::puts("FAIL: typed variable literals, validation, and atomic updates");
        return 1;
    }

    runtime_variable_set("stable", "before");
    const ButtonAction unresolved = set_variable_action("stable", "[var:not_set]");
    if (action_dispatch(unresolved, "stable") != ACTION_FAILED ||
        !get_equals("stable", "before")) {
        std::puts("FAIL: unresolved assignment fails without replacing the prior value");
        return 1;
    }

    char name[32];
    bool store_full = false;
    for (int index = 0; index < static_cast<int>(RUNTIME_VARIABLE_MAX_COUNT); ++index) {
        std::snprintf(name, sizeof(name), "fill_%02d", index);
        const RuntimeVariableSetResult set_result = runtime_variable_set(name, "x");
        if (set_result == RUNTIME_VARIABLE_SET_FULL) {
            store_full = true;
            break;
        }
        if (set_result != RUNTIME_VARIABLE_STORED) {
            std::puts("FAIL: test fills remaining runtime variable slots");
            return 1;
        }
    }
    if (!store_full) {
        std::puts("FAIL: test reached the fixed runtime variable capacity");
        return 1;
    }
    const ButtonAction failed_list[] = {
        set_variable_action("overflow", "full"),
        set_variable_action("after_failure", "must_not_run"),
    };
    if (action_list_dispatch_result(failed_list, 2, "capacity") != ACTION_FAILED ||
        runtime_variable_get("after_failure", name, sizeof(name)) != RUNTIME_VARIABLE_MISSING ||
        !get_equals("stable", "before")) {
        std::puts("FAIL: full-store failure preserves state and stops the remaining action list");
        return 1;
    }

    std::puts("runtime_variable_actions: PASS");
    return 0;
}