#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO)
#include "wifi_manager.h"
#include "power_manager.h"
#include "main_loop_bridge.h"
#if HAS_DISPLAY
#include "screen_saver_manager.h"
#endif
#endif

#if HAS_DISPLAY || HAS_BUTTON

namespace {

constexpr const char* kSystemActionTag = "Action";

#if defined(ARDUINO)
void restart_into_setup_ap(const void*, bool* ok, char*, size_t) {
    *ok = power_manager_request_setup_ap_restart();
    if (!*ok) {
        LOGE(kSystemActionTag, "Setup AP restart request failed");
        return;
    }
    LOGI(kSystemActionTag, "Restarting into setup AP");
    delay(200);
    ESP.restart();
}
#endif

void parse_system(const JsonObject& action, ButtonAction& act) {
    strlcpy(act.payload.system.system_command, action["system_command"] | "",
            sizeof(act.payload.system.system_command));
}

void serialize_system(const ButtonAction& act, JsonObject action) {
    if (act.payload.system.system_command[0]) action["system_command"] = act.payload.system.system_command;
}

ActionResult dispatch_system(const ButtonAction& act, const char* label, uint32_t) {
    const char* command = act.payload.system.system_command;
    if (strcmp(command, "reboot") == 0) {
        LOGI(kSystemActionTag, "%s system: reboot", label);
#if defined(ARDUINO)
        delay(200);
        ESP.restart();
#endif
    } else if (strcmp(command, "reboot_ap") == 0) {
#if defined(ARDUINO)
        const LoopBridgeResult result = loop_bridge_enqueue(restart_into_setup_ap, nullptr, 0);
        if (result != LOOP_BRIDGE_OK) {
            LOGE(kSystemActionTag, "%s system: setup AP restart could not be queued (%d)", label, result);
            return ACTION_FAILED;
        }
#endif
    } else if (strcmp(command, "wifi_reconnect") == 0) {
        LOGI(kSystemActionTag, "%s system: wifi_reconnect", label);
#if defined(ARDUINO)
        wifi_manager_request_reconnect();
#endif
    } else if (strcmp(command, "screensaver") == 0) {
#if defined(ARDUINO) && HAS_DISPLAY
        LOGI(kSystemActionTag, "%s system: screensaver", label);
        screen_saver_manager_sleep_now();
#else
        LOGW(kSystemActionTag, "%s system: screensaver unavailable (no display)", label);
#endif
        } else if (strcmp(command, "wake_display") == 0) {
    #if defined(ARDUINO) && HAS_DISPLAY
        LOGI(kSystemActionTag, "%s system: wake_display", label);
        screen_saver_manager_wake();
    #else
        LOGW(kSystemActionTag, "%s system: wake_display unavailable (no display)", label);
    #endif
    } else {
        LOGW(kSystemActionTag, "%s system: unknown command '%s'", label, command);
    }
    return ACTION_COMPLETE;
}

const char* validate_system(const JsonObjectConst action) {
    if (!action.containsKey("system_command")) return nullptr;
    if (!action["system_command"].is<const char*>()) return "system_command must be a string";
    const char* command = action["system_command"].as<const char*>();
    if (strcmp(command, "reboot") == 0 || strcmp(command, "reboot_ap") == 0 ||
        strcmp(command, "wifi_reconnect") == 0) return nullptr;
    return (strcmp(command, "screensaver") == 0 || strcmp(command, "wake_display") == 0) && HAS_DISPLAY
        ? nullptr : "unknown system command";
}

bool system_available() { return true; }

void describe_system(JsonObject& action) {
    action["group"] = "Device"; action["label"] = "Device command";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject reboot = commands.createNestedObject(); reboot["id"] = "reboot"; reboot["label"] = "Restart device";
    JsonObject reboot_ap = commands.createNestedObject(); reboot_ap["id"] = "reboot_ap"; reboot_ap["label"] = "Restart into setup AP";
    JsonObject reconnect = commands.createNestedObject(); reconnect["id"] = "wifi_reconnect"; reconnect["label"] = "Reconnect Wi-Fi";
#if HAS_DISPLAY
    JsonObject saver = commands.createNestedObject(); saver["id"] = "screensaver"; saver["label"] = "Enable screensaver";
    JsonObject wake = commands.createNestedObject(); wake["id"] = "wake_display"; wake["label"] = "Wake display";
#endif
    JsonArray editor_fields = action.createNestedArray("editor_fields");
    JsonObject command = editor_fields.createNestedObject(); command["name"] = "system_command"; command["label"] = "Command"; command["type"] = "select"; command["command_options"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kSystemActionType, ACTION_TYPE_SYSTEM, parse_system, serialize_system, dispatch_system, nullptr, describe_system, system_available, validate_system, nullptr, ACTION_EXECUTION_SYNC);

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON