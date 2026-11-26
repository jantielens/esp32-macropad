// Typed operation parsing, validation, serialization, and catalog tests.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ArduinoJson.h>
#include "action_result.h"
#include "pad_config.h"
#include "action_list.h"
#include "action_parse.h"
#include "action_registry.h"
#include "action_catalog.h"
#include "keyboard_layout.h"
#include "mcp_pad_image_metadata.h"
#include "action_validate.h"
#include "binding_template.h"
#include "voice_notes.h"
#include "voice_notes_processing.h"
#include "action_continuation.h"
#include "main_loop_bridge.h"
#include <string>

#if HAS_VOICE_NOTES
#include "voice_notes_file.h"
static VoiceNoteCommand record_command = VoiceNoteCommand::Start;
static uint32_t record_token = 0;
static bool record_accepted = true;
bool voice_notes_command(VoiceNoteCommand command, uint32_t token) {
    record_command = command;
    record_token = token;
    return record_accepted;
}
static VoiceNotesProcessingStart processing_start_result = VoiceNotesProcessingStart::Accepted;
VoiceNotesProcessingStart voice_notes_processing_start() { return processing_start_result; }
static LoopBridgeExec delete_exec = nullptr;
alignas(uint32_t) static unsigned char delete_job[LOOP_BRIDGE_CTX_BYTES];
static bool delete_queue_busy = false;
static VoiceNoteFileStatus delete_result = VoiceNoteFileStatus::Ready;
static char deleted_id[17] = {};
LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec, const void* context, size_t size) {
    if (delete_queue_busy) return LOOP_BRIDGE_BUSY;
    if (!exec || !context || size > sizeof(delete_job)) return LOOP_BRIDGE_INVALID;
    delete_exec = exec;
    memcpy(delete_job, context, size);
    return LOOP_BRIDGE_OK;
}
VoiceNoteFileStatus voice_notes_remove(const char* id) {
    strlcpy(deleted_id, id, sizeof(deleted_id));
    return delete_result;
}
#if HAS_DISPLAY && HAS_PSRAM
bool voice_notes_load_request(const char*) { return true; }
static unsigned cleared_notes = 0;
static char cleared_id[17] = {};
void voice_notes_loaded_clear_if_matches(const char* id) {
    ++cleared_notes;
    strlcpy(cleared_id, id, sizeof(cleared_id));
}
#endif
#endif

ActionDispatchContext action_dispatch_context() { return {}; }
void display_manager_request_binding_refresh() {}
ActionResult action_dispatch(const ButtonAction&, const char*, uint32_t, bool) {
    return ACTION_COMPLETE;
}

extern "C" unsigned long millis() { return 0; }

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void test_##name()
#define RUN(name)  do { \
    printf("  %-50s ", #name); \
    const int failures_before = g_fail; \
    test_##name(); \
    if (g_fail == failures_before) { printf("PASS\n"); g_pass++; } \
} while(0)

#define ASSERT_STR(field, expected) do { \
    if (strcmp((field), (expected)) != 0) { \
        printf("FAIL\n    %s: expected \"%s\", got \"%s\"\n", #field, (expected), (field)); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_EQ(field, expected) do { \
    if ((field) != (expected)) { \
        printf("FAIL\n    %s: expected %d, got %d\n", #field, (int)(expected), (int)(field)); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n", #cond); \
        g_fail++; return; \
    } \
} while(0)

TEST(empty_to_json_produces_empty_object) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_EQ(obj.size(), 0);  // empty action → no keys
}

TEST(typed_audio_actions_suppress_feedback) {
#if HAS_AUDIO
    for (const char* id : {"audio.alert.tone", "audio.alert.stop"}) {
        JsonDocument input;
        input["op"] = id;
        JsonObject args = input["args"].to<JsonObject>();
        if (!strcmp(id, "audio.alert.tone")) {
            args["pattern"] = "1000:200";
            args["volume"] = 0;
        }
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(input.as<JsonObject>(), action);
        }
        ASSERT_TRUE(!storage.failed && action_list_has_audio(&action, 1));
        action_storage_clear(storage);
    }
    ButtonAction legacy = {};
    strlcpy(legacy.type, ACTION_TYPE_SOUND_ALERT, sizeof(legacy.type));
    ASSERT_TRUE(!action_list_has_audio(&legacy, 1));
#endif
}

TEST(cycle_pad_operation_defaults_and_validation) {
    JsonDocument document;
    JsonArray catalog = document.to<JsonArray>();
    action_catalog_emit(catalog);
    JsonObjectConst operation;
    for (JsonObjectConst owner : catalog)
        if (!strcmp(owner["type"] | "", ACTION_TYPE_CYCLE_PAD))
            operation = owner["operations"][0];
    ASSERT_TRUE(!operation.isNull());
    ASSERT_TRUE(operation["arguments"][1]["default"].is<bool>());
    ASSERT_TRUE(operation["arguments"][1]["default"].as<bool>());
    ASSERT_STR(operation["arguments"][2]["control"].as<const char*>(), "pad_exclusions");

    document.clear();
    ASSERT_TRUE(!deserializeJson(document, R"({"op":"pad.cycle","args":{"direction":"next","wrap":true,"excluded_pads":"1, 3"}})"));
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) == nullptr);
    document["args"]["excluded_pads"] = "";
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) == nullptr);
    document["args"]["excluded_pads"] = "1,bad";
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) != nullptr);
    document["args"]["excluded_pads"] = "0";
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) != nullptr);
}

TEST(registered_identifiers_survive_storage) {
    ASSERT_TRUE(action_type_count() > 0);
    for (uint8_t index = 0; index < action_type_count(); ++index) {
        const ActionTypeDef* type = action_type_at(index);
        ASSERT_TRUE(strlen(type->type_name) < sizeof(ButtonAction::type));
        ButtonAction stored = {};
        strlcpy(stored.type, type->type_name, sizeof(stored.type));
        ASSERT_STR(stored.type, type->type_name);
        ASSERT_TRUE(action_type_find(stored.type) == type);
    }
}

TEST(catalog_feature_gates_and_metadata_parity) {
    ASSERT_TRUE(!action_type_is_supported("shutdown"));
    JsonDocument portal;
    action_catalog_emit(portal.to<JsonArray>());
    JsonDocument mcp;
    action_catalog_emit(mcp.to<JsonArray>());
    unsigned alarm_count = 0;
    for (JsonObject entry : mcp.as<JsonArray>()) {
        const char* name = entry["type"];
        ASSERT_TRUE(strcmp(name, "shutdown") != 0);
        const ActionTypeDef* type = action_type_find(name);
        ASSERT_TRUE(type != nullptr);
        ASSERT_EQ(entry["alarm_hook_allowed"].as<bool>(), type->execution == ACTION_EXECUTION_SYNC);
        JsonArrayConst operations = entry["operations"];
        ASSERT_EQ(operations.size(), type->operation_count);
        for (uint8_t index = 0; index < type->operation_count; ++index) {
            const ActionOperationDef& operation = type->operations[index];
            JsonObjectConst metadata = operations[index];
            ASSERT_TRUE(metadata["alarm_hook_allowed"].is<bool>());
            ASSERT_TRUE(metadata["pausable"].is<bool>());
            ASSERT_EQ(metadata["alarm_hook_allowed"].as<bool>(), operation.execution == ACTION_EXECUTION_SYNC);
            ASSERT_EQ(metadata["pausable"].as<bool>(), operation.execution == ACTION_EXECUTION_PAUSABLE);
            ASSERT_TRUE(!strcmp(metadata["execution"] | "",
                operation.execution == ACTION_EXECUTION_SYNC ? "sync"
                : operation.execution == ACTION_EXECUTION_PAUSABLE ? "pausable" : "unknown"));
        }
        if (!strcmp(name, "alarm") || !strcmp(name, "alarm_tone") || !strcmp(name, "alarm_mp3")) ++alarm_count;
    }
    ASSERT_EQ(alarm_count, ALARM_ENABLED ? 3 : 0);
    ASSERT_TRUE(action_type_find("screen")->display_lock_required);
    ASSERT_TRUE(!action_type_find("ha_service")->display_lock_required);
    for (JsonObjectConst entry : portal.as<JsonArray>()) {
        ASSERT_TRUE(!entry.containsKey("template_fields"));
    }
    std::string portal_json, mcp_json;
    serializeJson(portal, portal_json);
    serializeJson(mcp, mcp_json);
    ASSERT_TRUE(portal_json == mcp_json);
}

TEST(typed_operation_descriptors_and_arguments) {
    const ActionTypeDef* owner = nullptr;
    const ActionOperationDef* screen = action_operation_find("screen.open", &owner);
    ASSERT_TRUE(screen != nullptr && owner == action_type_find(ACTION_TYPE_SCREEN));
    const ActionOperationDef* timer = action_operation_find("timer.adjust", &owner);
    ASSERT_TRUE(timer != nullptr && owner == action_type_find(ACTION_TYPE_TIMER));
    const ActionOperationDef* wifi = action_operation_find("wifi.station.set_enabled", &owner);
    ASSERT_TRUE(wifi != nullptr && owner == action_type_find(ACTION_TYPE_SYSTEM));
    ASSERT_TRUE(action_operation_find("screen.unknown") == nullptr);

    JsonDocument document;
    ASSERT_TRUE(!deserializeJson(document, R"({"op":"screen.open","args":{"target":"[item:id]"}})"));
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) == nullptr);
    document.clear();
    ASSERT_TRUE(!deserializeJson(document, R"({"op":"screen.open\u0000other","args":{"target":"pad_0"}})"));
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) != nullptr);
    document.clear();
    ASSERT_TRUE(!deserializeJson(document, R"({"op":"timer.adjust","args":{"timer":1,"seconds":{"$template":"-15"}}})"));
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) == nullptr);
    ActionStorage storage = {};
    ButtonAction parsed = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(document.as<JsonObject>(), parsed);
    }
    ASSERT_TRUE(!storage.failed && parsed.payload.operation.arguments);
    JsonDocument round_trip;
    action_to_json(parsed, round_trip.to<JsonObject>());
    ASSERT_STR(round_trip["op"].as<const char*>(), "timer.adjust");
    ASSERT_EQ(round_trip["args"]["timer"].as<int>(), 1);
    ASSERT_STR(round_trip["args"]["seconds"]["$template"].as<const char*>(), "-15");
    action_storage_clear(storage);
    document["args"]["timer"] = 4;
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) != nullptr);
    document["args"]["timer"] = 1;
    document["args"]["extra"] = true;
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) != nullptr);
    document["args"].as<JsonObject>().remove("extra");
    document["args"].as<JsonObject>().remove("seconds");
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) != nullptr);
    document.clear();
    ASSERT_TRUE(!deserializeJson(document, R"({"op":"wifi.station.set_enabled","args":{"enabled":true}})"));
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) == nullptr);
    {
        ActionStorageScope scope(&storage);
        action_parse(document.as<JsonObject>(), parsed);
    }
    ASSERT_TRUE(!storage.failed && action_operation_for_action(parsed) == wifi);
    round_trip.clear();
    action_to_json(parsed, round_trip.to<JsonObject>());
    ASSERT_STR(round_trip["op"].as<const char*>(), "wifi.station.set_enabled");
    ASSERT_TRUE(round_trip["args"]["enabled"].is<bool>());
    ASSERT_TRUE(round_trip["args"]["enabled"].as<bool>());
    action_storage_clear(storage);
    document["args"]["enabled"] = "true";
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()) != nullptr);
    document["args"]["enabled"].to<JsonObject>()["$template"] = "[event:value]";
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false) == nullptr);
}

TEST(dialog_operation_timing_and_color_validation) {
#if HAS_DISPLAY && (HAS_TOUCH || HAS_BUTTON)
    for (const char* id : {"dialog.notify", "dialog.confirm"}) {
        const ActionOperationDef* operation = action_operation_find(id);
        ASSERT_TRUE(operation != nullptr);
        const ActionArgumentDef& timing = operation->arguments[operation->argument_count - 1];
        ASSERT_TRUE(!action_operation_validate_descriptor(*operation));
        ASSERT_EQ(operation->execution, !strcmp(id, "dialog.confirm")
            ? ACTION_EXECUTION_PAUSABLE : ACTION_EXECUTION_SYNC);
        ASSERT_TRUE(timing.has_default_integer);
        ASSERT_EQ(timing.default_integer, !strcmp(id, "dialog.notify") ? 3000 : 10000);
        JsonDocument document;
        document["op"] = id;
        JsonObject args = document["args"].to<JsonObject>();
        args["message"] = "Continue?";
        if (!strcmp(id, "dialog.confirm")) {
            args["approve_text"] = "Continue";
            args["cancel_text"] = "Cancel";
        }
        for (int32_t value : {0, 1000, 300000}) {
            args[timing.name] = value;
            ASSERT_TRUE(!action_operation_validate_json(document.as<JsonObjectConst>()));
            char text[16];
            snprintf(text, sizeof(text), "%ld", static_cast<long>(value));
            int32_t normalized = -1;
            ASSERT_TRUE(!action_argument_normalize_text(timing, text, nullptr, &normalized));
            ASSERT_EQ(normalized, value);
        }
        for (int32_t value : {-1, 1, 999, 300001}) {
            args[timing.name] = value;
            ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()));
            char text[16];
            snprintf(text, sizeof(text), "%ld", static_cast<long>(value));
            int32_t normalized = 0;
            ASSERT_TRUE(action_argument_normalize_text(timing, text, nullptr, &normalized));
        }
        args[timing.name] = 0;
        char message[CONFIG_DIALOG_MESSAGE_MAX_LEN + 1];
        memset(message, 'a', sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        args["message"] = message;
        ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()));
        message[CONFIG_DIALOG_MESSAGE_MAX_LEN - 1] = '\0';
        args["message"] = message;
        ASSERT_TRUE(!action_operation_validate_json(document.as<JsonObjectConst>()));
        if (!strcmp(id, "dialog.confirm")) {
            args["approve_text"] = "";
            ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()));
            args["approve_text"] = "[literal:caption]";
            ASSERT_TRUE(!action_operation_validate_json(document.as<JsonObjectConst>()));
        }
    }
    const ActionOperationDef* alert = action_operation_find("visual_alert.start");
    ASSERT_TRUE(alert != nullptr);
    const ActionArgumentDef& color = alert->arguments[0];
    const char* normalized = nullptr;
    for (const char* value : {"", "#abc", "0x123456"})
        ASSERT_TRUE(!action_argument_normalize_text(color, value, &normalized, nullptr));
    ASSERT_TRUE(action_argument_normalize_text(color, "#not-color", &normalized, nullptr));
#endif
}

TEST(key_operation_literal_and_template_boundaries) {
#if HAS_BLE_HID || HAS_USB_HID
    const ActionOperationDef* key = action_operation_find("keyboard.send");
    ASSERT_TRUE(key != nullptr);
    ActionArgumentDef oversized = key->arguments[0];
    oversized.max_length = ACTION_OPERATION_MAX_TEXT_LENGTH + 1;
    ActionOperationDef invalid = *key;
    invalid.arguments = &oversized;
    ASSERT_TRUE(action_operation_validate_descriptor(invalid));
    JsonDocument document;
    document["op"] = "keyboard.send";
    JsonObject args = document["args"].to<JsonObject>();
    std::string literal(CONFIG_KEY_SEQ_MAX_LEN - 1, 'a');
    args["sequence"] = literal.c_str();
    ASSERT_TRUE(!action_operation_validate_json(document.as<JsonObjectConst>()));
    ActionStorage storage = {};
    ButtonAction action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(document.as<JsonObject>(), action);
    }
    ASSERT_TRUE(!storage.failed && action.payload.operation.arguments);
    JsonDocument output;
    action_to_json(action, output.to<JsonObject>());
    action_storage_clear(storage);
    ASSERT_TRUE(literal == output["args"]["sequence"].as<const char*>());
    literal.push_back('a');
    args["sequence"] = literal.c_str();
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()));
    std::string templ(BINDING_TEMPLATE_MAX_LEN - 1, 'a');
    args["sequence"].to<JsonObject>()["$template"] = templ.c_str();
    ASSERT_TRUE(!action_operation_validate_json(document.as<JsonObjectConst>(), false));
    templ.push_back('a');
    args["sequence"]["$template"] = templ.c_str();
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
    document.clear();
    document["op"] = "keyboard.type";
    std::string text(KEYBOARD_TEXT_MAX_BYTES, 'a');
    document["args"]["text"] = text.c_str();
    ASSERT_TRUE(!action_operation_validate_json(document.as<JsonObjectConst>()));
    storage = {};
    action = {};
    {
        ActionStorageScope scope(&storage);
        action_parse(document.as<JsonObject>(), action);
    }
    output.clear();
    action_to_json(action, output.to<JsonObject>());
    ASSERT_TRUE(!storage.failed && text == output["args"]["text"].as<const char*>());
    ASSERT_TRUE(!output["args"]["allow_controls"].as<bool>());
    action_storage_clear(storage);
    text.push_back('a');
    document["args"]["text"] = text.c_str();
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>()));
#endif
}

TEST(typed_operation_round_trips) {
    const char* examples[] = {
        R"({"op":"delay.wait","args":{"duration_ms":1000}})",
        R"({"op":"variable.set","args":{"name":"counter","value":{"$template":"[event:value]"}}})",
        R"({"op":"screen.back","args":{}})",
        R"({"op":"screen.open","args":{"target":"pad_1"}})",
        R"({"op":"audio.volume.set","args":{"percent":30}})",
        R"({"op":"audio.volume.adjust","args":{"percent":-5}})",
        R"({"op":"display.brightness.set","args":{"percent":50}})",
        R"({"op":"display.brightness.adjust","args":{"percent":10}})",
        R"({"op":"audio.volume.save","args":{}})",
        R"({"op":"display.brightness.save","args":{}})",
        R"({"op":"wifi.station.set_enabled","args":{"enabled":true}})",
        R"({"op":"wifi.ap.set_enabled","args":{"enabled":{"$template":"[var:enabled]"}}})",
        R"({"op":"device.restart","args":{}})",
        R"({"op":"music.next","args":{}})",
        R"({"op":"mouse.click","args":{"button":"left"}})",
        R"({"op":"gamepad.button.tap","args":{"button":1}})",
        R"({"op":"gamepad.hat.down","args":{"direction":"up"}})",
        R"({"op":"gamepad.trigger.up","args":{"trigger":"left"}})",
        R"({"op":"ble.pair","args":{}})",
        R"({"op":"pad.cycle","args":{"direction":"previous","wrap":false,"excluded_pads":"1,5"}})",
        R"({"op":"button.navigate","args":{"command":"tap"}})",
        R"({"op":"list.navigate","args":{"target":"items","command":"next_page"}})",
        R"({"op":"mqtt.publish","args":{"topic":"test/topic","payload":"hello"}})",
        R"({"op":"bthome.send","args":{"button":1,"event":"press"}})",
        R"({"op":"ha.service.call","args":{"entity_id":"light.kitchen","service":"toggle","data_json":"{}"}})",
        R"({"op":"audio.alert.tone","args":{"pattern":"100","volume":50}})",
        R"({"op":"audio.alert.mp3","args":{"file":"alert.mp3","volume":50}})",
        R"({"op":"audio.alert.stop","args":{}})",
        R"({"op":"dialog.notify","args":{"message":"Hello","duration_ms":1000}})",
        R"({"op":"dialog.confirm","args":{"message":"Continue?","approve_text":"Yes","cancel_text":"No","timeout_ms":0}})",
        R"({"op":"visual_alert.start","args":{"color":"#ff0000","pattern":"blink","period_ms":1000,"intensity":50,"duration_ms":2000}})",
        R"({"op":"visual_alert.stop","args":{}})",
        R"({"op":"timer.adjust","args":{"timer":1,"seconds":-15}})",
        R"({"op":"timer.set","args":{"timer":1,"seconds":60}})",
        R"({"op":"timer.start_down","args":{"timer":1,"seconds":60}})",
        R"({"op":"timer.stop","args":{"timer":1}})",
        R"({"op":"alarm.cancel","args":{"alarm":"0"}})",
        R"({"op":"alarm.set_time","args":{"minutes":60}})",
        R"({"op":"alarm.weekday_enable","args":{"day":"0"}})",
        R"({"op":"alarm.tone","args":{"pattern":"100","volume":50}})",
        R"({"op":"voice_note.record.start","args":{}})",
        R"({"op":"voice_note.process","args":{}})",
        R"({"op":"voice_note.load","args":{"id":"0123456789abcdef"}})",
        R"({"op":"voice_note.delete","args":{"id":{"$template":"[item:id]"}}})",
    };
    unsigned tested = 0;
    for (const char* json : examples) {
        JsonDocument document;
        ASSERT_TRUE(!deserializeJson(document, json));
        if (!action_operation_find(document["op"])) continue;
        if (const char* error = action_operation_validate_json(document.as<JsonObjectConst>(), false)) {
            std::printf("invalid round-trip example %s: %s\n", json, error);
            ASSERT_TRUE(false);
        }
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(document.as<JsonObject>(), action);
        }
        ASSERT_TRUE(!storage.failed && action_operation_for_action(action));
        JsonDocument output;
        action_to_json(action, output.to<JsonObject>());
        std::string expected, actual;
        serializeJson(document, expected);
        serializeJson(output, actual);
        ASSERT_TRUE(actual == expected);
        action_storage_clear(storage);
        ++tested;
    }
    ASSERT_TRUE(tested >= 25);
}

TEST(incompatible_action_documents_are_rejected) {
    for (const char* json : {
            R"({})",
            R"({"type":"back"})",
            R"({"type":"none"})",
            R"({"op":"@screen.back","args":{}})",
            R"({"op":"screen.back","args":{},"type":"back"})",
            R"({"op":"screen.open"})",
            R"({"op":"screen.open","args":{"target":"pad_1","extra":true}})",
            R"({"op":"screen.open","args":{"target":{"$template":"[event:value","extra":1}}})",
            R"({"op":"unknown.operation","args":{}})"}) {
        JsonDocument document;
        ASSERT_TRUE(!deserializeJson(document, json));
        ASSERT_TRUE(action_validate_json(document.as<JsonObjectConst>()));
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(document.as<JsonObject>(), action);
        }
        ASSERT_TRUE(storage.failed && !action.type[0]);
        action_storage_clear(storage);
    }
}

TEST(typed_home_assistant_validation) {
    JsonDocument document;
    document["op"] = "ha.service.call";
    JsonObject args = document["args"].to<JsonObject>();
    args["entity_id"] = "light.kitchen";
    args["service"] = "turn_on";
    args["data_json"] = "{}";
    for (const char* data : {"", "{}", "{\"brightness_pct\":80}"}) {
        args["data_json"] = data;
        ASSERT_TRUE(!action_operation_validate_json(document.as<JsonObjectConst>(), false));
    }

    for (const char* data : {"{", "[]", "42", "true", "null", "{\"x\":1,}"}) {
        args["data_json"] = data;
        ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
    }
    args["data_json"] = "{}";
    for (const char* entity : {"light", ".kitchen", "light.", "light.kitchen.extra", "light. kitchen", ""}) {
        args["entity_id"] = entity;
        ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
    }
    args["entity_id"] = "light.kitchen";
    for (const char* service : {"light.toggle", "MediaStop", "media-stop", ""}) {
        args["service"] = service;
        ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
    }
    args["service"] = "toggle";
    for (const char* name : {"entity_id", "service", "data_json"}) {
        JsonVariant value = args[name];
        const std::string original = value.as<const char*>();
        value.set(42);
        ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
        value.set(original);
        args.remove(name);
        ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
        args[name] = original;
    }
    args["entity_id"] = std::string(sizeof(HaServicePayload::entity_id), 'x');
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
    args["entity_id"] = "light.kitchen";
    args["service"] = std::string(sizeof(HaServicePayload::service), 'x');
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
    args["service"] = "toggle";
    args["data_json"] = std::string(sizeof(HaServicePayload::data_json), ' ');
    ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
}

TEST(home_assistant_catalog_examples_are_valid) {
    JsonDocument catalog;
    action_catalog_emit(catalog.to<JsonArray>());
    JsonObjectConst call;
    for (JsonObjectConst owner : catalog.as<JsonArrayConst>())
        for (JsonObjectConst operation : owner["operations"].as<JsonArrayConst>())
            if (!strcmp(operation["id"] | "", "ha.service.call")) call = operation;
    ASSERT_TRUE(!call.isNull());
    JsonDocument example;
    example["op"] = call["id"];
    JsonObject args = example["args"].to<JsonObject>();
    for (JsonObjectConst argument : call["arguments"].as<JsonArrayConst>()) {
        ASSERT_TRUE((argument["description"] | "")[0]);
        args[argument["name"].as<const char*>()] = argument["example"];
    }
    ASSERT_STR(args["entity_id"].as<const char*>(), "media_player.living_room");
    ASSERT_STR(args["service"].as<const char*>(), "media_play_pause");
    ASSERT_TRUE(!action_operation_validate_json(example.as<JsonObjectConst>(), false));
    args["service"] = "media_player.media_play_pause";
    ASSERT_TRUE(action_operation_validate_json(example.as<JsonObjectConst>(), false));
}

TEST(image_metadata_capabilities_and_fields) {
    for (unsigned profile = 0; profile < 4; ++profile) {
        const bool remote = profile & 1;
        const bool local = profile & 2;
        JsonDocument document;
        JsonObject button = document.to<JsonObject>();
        button.createNestedArray("fields").add("label_center");
        mcp_pad_image_metadata_emit(button, remote, local, remote && local);
        ASSERT_EQ(button["fields"].size(), 8);
        ASSERT_STR(button["fields"][0].as<const char*>(), "label_center");
        JsonObjectConst images = button["background_images"];
        ASSERT_EQ(images["has_image_fetch"].as<bool>(), remote);
        ASSERT_EQ(images["has_image_library"].as<bool>(), local);
        ASSERT_EQ(images["incompatible_widgets"].size(), remote && local ? 1 : 0);
        if (remote && local)
            ASSERT_STR(images["incompatible_widgets"][0].as<const char*>(), "external");
        ASSERT_TRUE((images["authentication_note"] | "")[0]);
        JsonObjectConst fields = button["image_fields"];
        ASSERT_EQ(fields["bg_image_url"]["available"].as<bool>(), remote);
        ASSERT_EQ(fields["bg_image_path"]["available"].as<bool>(), local);
        ASSERT_EQ(fields["bg_image_fit"]["available"].as<bool>(), remote || local);
        ASSERT_EQ(fields["bg_image_url"]["max_length"].as<unsigned>(), CONFIG_BG_IMAGE_URL_MAX_LEN - 1);
        ASSERT_EQ(fields["bg_image_path"]["max_length"].as<unsigned>(), CONFIG_BG_IMAGE_PATH_MAX_LEN - 1);
        ASSERT_TRUE(fields["bg_image_url"]["bindable"].as<bool>());
        ASSERT_TRUE(fields["bg_image_path"]["bindable"].as<bool>());
        ASSERT_TRUE(!fields["bg_image_password"]["bindable"].as<bool>());
        ASSERT_TRUE(fields["bg_image_password_set"]["default"].is<bool>());
        ASSERT_EQ(fields["bg_image_interval_ms"]["max"].as<uint32_t>(), UINT32_MAX);
        ASSERT_EQ(fields["bg_image_interval_ms"]["default"].as<unsigned>(), 0);
        ASSERT_STR(fields["bg_image_fit"]["default"].as<const char*>(), "fill-center");
        ASSERT_EQ(fields["bg_image_fit"]["choices"].size(), 6);
    }
}

TEST(typed_operation_boundaries_and_capabilities) {
    const char* invalid[] = {
        R"({"op":"delay.wait","args":{"duration_ms":0}})",
        R"({"op":"delay.wait","args":{"duration_ms":55001}})",
        R"({"op":"delay.wait","args":{"duration_ms":3.5}})",
        R"({"op":"delay.wait","args":{"duration_ms":"3000"}})",
        R"({"op":"timer.adjust","args":{"timer":0,"seconds":1}})",
        R"({"op":"timer.adjust","args":{"timer":4,"seconds":1}})",
        R"({"op":"timer.start_down","args":{"timer":1,"seconds":0}})",
        R"({"op":"pad.cycle","args":{"direction":"sideways","wrap":true,"excluded_pads":""}})",
        R"({"op":"pad.cycle","args":{"direction":"next","wrap":"true","excluded_pads":""}})",
        R"({"op":"pad.cycle","args":{"direction":"next","wrap":true,"excluded_pads":"0"}})",
        R"({"op":"wifi.station.set_enabled","args":{"enabled":"true"}})",
        R"({"op":"screen.open","args":{"target":{"$template":""}}})",
        R"({"op":"screen.open","args":{"target":{"$template":"[event:value","extra":1}}})",
        R"({"op":"screen.open","args":{"target":"pad_1","extra":1}})",
    };
    for (const char* json : invalid) {
        JsonDocument document;
        ASSERT_TRUE(!deserializeJson(document, json));
        ASSERT_TRUE(action_operation_validate_json(document.as<JsonObjectConst>(), false));
        ActionStorage storage = {};
        ButtonAction action = {};
        {
            ActionStorageScope scope(&storage);
            action_parse(document.as<JsonObject>(), action);
        }
        ASSERT_TRUE(storage.failed && !action.type[0]);
        action_storage_clear(storage);
    }

#if HAS_VOICE_NOTES
    ASSERT_TRUE(action_operation_find("voice_note.record.start"));
    ASSERT_TRUE(action_operation_find("voice_note.process"));
    ASSERT_TRUE(action_operation_find("voice_note.delete"));
#else
    ASSERT_TRUE(!action_operation_find("voice_note.record.start"));
    ASSERT_TRUE(!action_operation_find("voice_note.process"));
    ASSERT_TRUE(!action_operation_find("voice_note.delete"));
#endif
#if HAS_VOICE_NOTES && HAS_DISPLAY && HAS_PSRAM
    ASSERT_TRUE(action_operation_find("voice_note.load"));
#else
    ASSERT_TRUE(!action_operation_find("voice_note.load"));
#endif
    ASSERT_EQ(action_operation_find("alarm.cancel") != nullptr, ALARM_ENABLED != 0);

    JsonDocument document;
    ASSERT_TRUE(!deserializeJson(document, R"({"op":"screen.back","args":{}})"));
    ButtonAction action = {};
    action_parse(document.as<JsonObject>(), action);
    ASSERT_TRUE(!action.type[0]);
}

int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--validate-operations")) {
        JsonDocument input;
        if (deserializeJson(input, argv[2]) || !input.is<JsonArray>()) return 1;
        for (JsonVariantConst value : input.as<JsonArrayConst>()) {
            if (!value.is<JsonObjectConst>()) return 1;
            const char* error = action_operation_validate_json(value.as<JsonObjectConst>(), false);
            if (error) {
                std::fprintf(stderr, "Recipe operation invalid: %s\n", error);
                return 1;
            }
        }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--catalog")) {
        JsonDocument catalogs;
        action_catalog_emit(catalogs["portal"].to<JsonArray>());
        action_catalog_emit(catalogs["mcp"].to<JsonArray>());
        std::string output;
        serializeJson(catalogs, output);
        std::puts(output.c_str());
        return 0;
    }
    printf("=== Typed Action Parse/Serialize Tests ===\n\n");
    RUN(empty_to_json_produces_empty_object);
    RUN(typed_audio_actions_suppress_feedback);
    RUN(cycle_pad_operation_defaults_and_validation);
    RUN(registered_identifiers_survive_storage);
    RUN(catalog_feature_gates_and_metadata_parity);
    RUN(typed_operation_descriptors_and_arguments);
    RUN(dialog_operation_timing_and_color_validation);
    RUN(key_operation_literal_and_template_boundaries);
    RUN(typed_operation_round_trips);
    RUN(incompatible_action_documents_are_rejected);
    RUN(typed_home_assistant_validation);
    RUN(home_assistant_catalog_examples_are_valid);
    RUN(image_metadata_capabilities_and_fields);
    RUN(typed_operation_boundaries_and_capabilities);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
