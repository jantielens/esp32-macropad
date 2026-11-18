#include "action_registry.h"
#include "log_manager.h"
#include "wifi_manager.h"
#if defined(ARDUINO)
#include "power_manager.h"
#include "main_loop_bridge.h"
#include "reboot_service.h"
#if HAS_DISPLAY
#include "screen_saver_manager.h"
#endif
#endif

#if HAS_DISPLAY || HAS_BUTTON

namespace {

constexpr const char* kSystemActionTag = "Action";

const ActionArgumentDef kWifiStationArguments[] = {
    {"enabled", "Station mode", ACTION_ARGUMENT_BOOLEAN, 0, 0, 0, true,
     "[expr:[event:value]==\"aan\"?\"true\":[event:value]==\"uit\"?\"false\":\"invalid\"]", "boolean"},
};
const ActionArgumentDef kWifiApArguments[] = {
    {"enabled", "Access point", ACTION_ARGUMENT_BOOLEAN, 0, 0, 0, true,
     "[var:ap_enabled]", "boolean"},
};

ActionResult request_wifi_command(WifiNetworkCommand command, const char* label) {
#if defined(ARDUINO)
    if (wifi_manager_request_network_command(command)) return ACTION_COMPLETE;
    LOGW(kSystemActionTag, "%s network request rejected", label);
#else
    (void)command;
    LOGW(kSystemActionTag, "%s network command unavailable", label);
#endif
    return ACTION_FAILED;
}

ActionResult dispatch_wifi_station(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
    return request_wifi_command(arguments[0].integer
        ? WifiNetworkCommand::Connect : WifiNetworkCommand::Disconnect, label);
}

ActionResult dispatch_wifi_ap(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
    return request_wifi_command(arguments[0].integer
        ? WifiNetworkCommand::EnableAp : WifiNetworkCommand::DisableAp, label);
}

ActionResult dispatch_wifi_off(const ActionResolvedArgument*, const char* label, uint32_t) {
    return request_wifi_command(WifiNetworkCommand::TurnOff, label);
}

#if defined(ARDUINO)
void restart_into_setup_ap(const void*, bool* ok, char*, size_t) {
    *ok = power_manager_request_setup_ap_restart();
    if (!*ok) {
        LOGE(kSystemActionTag, "AP restart request failed");
        return;
    }
    LOGI(kSystemActionTag, "Restarting into AP");
    reboot_request(200, REBOOT_NETWORK_TRANSITION);
}
#endif

ActionResult dispatch_restart(const ActionResolvedArgument*, const char* label, uint32_t) {
#if defined(ARDUINO)
    LOGI(kSystemActionTag, "%s system: reboot", label);
    reboot_request(200);
    return ACTION_COMPLETE;
#else
    LOGW(kSystemActionTag, "%s system: restart unavailable", label);
    return ACTION_FAILED;
#endif
}

ActionResult dispatch_restart_ap(const ActionResolvedArgument*, const char* label, uint32_t) {
#if defined(ARDUINO)
    const LoopBridgeResult result = loop_bridge_enqueue(restart_into_setup_ap, nullptr, 0);
    if (result == LOOP_BRIDGE_OK) return ACTION_COMPLETE;
    LOGE(kSystemActionTag, "%s system: setup AP restart could not be queued (%d)", label, result);
#else
    LOGW(kSystemActionTag, "%s system: setup AP restart unavailable", label);
#endif
    return ACTION_FAILED;
}

#if HAS_DISPLAY
ActionResult dispatch_sleep_display(const ActionResolvedArgument*, const char* label, uint32_t) {
#if defined(ARDUINO)
    LOGI(kSystemActionTag, "%s system: screensaver", label);
    screen_saver_manager_sleep_now();
    return ACTION_COMPLETE;
#else
    LOGW(kSystemActionTag, "%s system: screensaver unavailable", label);
    return ACTION_FAILED;
#endif
}

ActionResult dispatch_wake_display(const ActionResolvedArgument*, const char* label, uint32_t) {
#if defined(ARDUINO)
    LOGI(kSystemActionTag, "%s system: wake_display", label);
    screen_saver_manager_wake();
    return ACTION_COMPLETE;
#else
    LOGW(kSystemActionTag, "%s system: wake display unavailable", label);
    return ACTION_FAILED;
#endif
}
#endif

const ActionOperationDef kSystemOperations[] = {
    {"wifi.station.set_enabled", "Set Wi-Fi station mode", kWifiStationArguments, 1, dispatch_wifi_station},
    {"wifi.ap.set_enabled", "Set access point", kWifiApArguments, 1, dispatch_wifi_ap},
    {"wifi.radio.turn_off", "Turn off Wi-Fi", nullptr, 0, dispatch_wifi_off},
    {"device.restart", "Restart device", nullptr, 0, dispatch_restart},
    {"device.restart_setup_ap", "Restart into AP", nullptr, 0, dispatch_restart_ap},
#if HAS_DISPLAY
    {"display.sleep", "Enable screensaver", nullptr, 0, dispatch_sleep_display},
    {"display.wake", "Wake display", nullptr, 0, dispatch_wake_display},
#endif
};

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
        reboot_request(200);
#endif
    } else if (strcmp(command, "reboot_ap") == 0) {
#if defined(ARDUINO)
        const LoopBridgeResult result = loop_bridge_enqueue(restart_into_setup_ap, nullptr, 0);
        if (result != LOOP_BRIDGE_OK) {
            LOGE(kSystemActionTag, "%s system: setup AP restart could not be queued (%d)", label, result);
            return ACTION_FAILED;
        }
#endif
    } else if (wifi_network_command_from_string(command) != WifiNetworkCommand::None) {
        LOGI(kSystemActionTag, "%s system: %s", label, command);
#if defined(ARDUINO)
        if (!wifi_manager_request_network_command(wifi_network_command_from_string(command))) {
            return ACTION_FAILED;
        }
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
        return ACTION_FAILED;
    }
    return ACTION_COMPLETE;
}

const char* validate_system(const JsonObjectConst action) {
    if (!action.containsKey("system_command")) return nullptr;
    if (!action["system_command"].is<const char*>()) return "system_command must be a string";
    const char* command = action["system_command"].as<const char*>();
    if (strcmp(command, "reboot") == 0 || strcmp(command, "reboot_ap") == 0 ||
        wifi_network_command_from_string(command) != WifiNetworkCommand::None) return nullptr;
    return (strcmp(command, "screensaver") == 0 || strcmp(command, "wake_display") == 0) && HAS_DISPLAY
        ? nullptr : "unknown system command";
}

bool system_available() { return true; }

void describe_system(JsonObject& action) {
    action["group"] = "Device"; action["label"] = "Device command";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject reboot = commands.createNestedObject(); reboot["id"] = "reboot"; reboot["label"] = "Restart device";
    JsonObject reboot_ap = commands.createNestedObject(); reboot_ap["id"] = "reboot_ap"; reboot_ap["label"] = "Restart into AP";
    JsonObject connect = commands.createNestedObject(); connect["id"] = "wifi_connect"; connect["label"] = "Connect to Wi-Fi";
    JsonObject disconnect = commands.createNestedObject(); disconnect["id"] = "wifi_disconnect"; disconnect["label"] = "Disconnect from Wi-Fi";
    JsonObject off = commands.createNestedObject(); off["id"] = "wifi_off"; off["label"] = "Turn off Wi-Fi";
    JsonObject ap_enable = commands.createNestedObject(); ap_enable["id"] = "ap_enable"; ap_enable["label"] = "Enable AP";
    JsonObject ap_disable = commands.createNestedObject(); ap_disable["id"] = "ap_disable"; ap_disable["label"] = "Disable AP";
    action["note"] = "Connect to Wi-Fi turns Wi-Fi on and tries the configured network, replacing AP. Disconnect from Wi-Fi turns Wi-Fi off but leaves an active AP unchanged. Turn off Wi-Fi stops both network and AP access; Bluetooth remains available. Commands acknowledge a request, not readiness, and do not change the saved operating mode. Enable AP keeps the current pad; Restart into AP enters recovery.";
#if HAS_DISPLAY
    JsonObject saver = commands.createNestedObject(); saver["id"] = "screensaver"; saver["label"] = "Enable screensaver";
    JsonObject wake = commands.createNestedObject(); wake["id"] = "wake_display"; wake["label"] = "Wake display";
#endif
    JsonArray editor_fields = action.createNestedArray("editor_fields");
    JsonObject command = editor_fields.createNestedObject(); command["name"] = "system_command"; command["label"] = "Command"; command["type"] = "select"; command["command_options"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kSystemActionType, ACTION_TYPE_SYSTEM, parse_system, serialize_system, dispatch_system, nullptr, describe_system, system_available, validate_system, ACTION_EXECUTION_SYNC, true, true, false,
    [](const ButtonAction& action) {
        const char* command = action.payload.system.system_command;
        return strcmp(command, "reboot") == 0 || strcmp(command, "reboot_ap") == 0 ||
            strcmp(command, "screensaver") == 0 || strcmp(command, "wake_display") == 0;
    }, kSystemOperations, sizeof(kSystemOperations) / sizeof(kSystemOperations[0]));

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON