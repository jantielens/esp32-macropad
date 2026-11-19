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

void describe_system(JsonObject& action) {
    action["group"] = "Device"; action["label"] = "Device command";
    action["note"] = "Connect to Wi-Fi turns Wi-Fi on and tries the configured network, replacing AP. Disconnect from Wi-Fi turns Wi-Fi off but leaves an active AP unchanged. Turn off Wi-Fi stops both network and AP access; Bluetooth remains available. Commands acknowledge a request, not readiness, and do not change the saved operating mode. Enable AP keeps the current pad; Restart into AP enters recovery.";

}

DEFINE_AND_REGISTER_ACTION_TYPE(kSystemActionType, ACTION_TYPE_SYSTEM, describe_system, nullptr, ACTION_EXECUTION_SYNC, true, false,
    kSystemOperations, sizeof(kSystemOperations) / sizeof(kSystemOperations[0]));

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON