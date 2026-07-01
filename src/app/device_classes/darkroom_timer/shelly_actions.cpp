// Darkroom-timer ActionTypeDef — parse, serialize, dispatch for the "shelly"
// action type. Registered via REGISTER_ACTION_TYPE so action_dispatch.cpp /
// action_parse.cpp do not need to know about the shelly arm at compile time.
//
// Aggregated into the build via
// device_classes/darkroom_timer/darkroom_timer_device_class.cpp
// under #if IS_DARKROOM_TIMER.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "../../log_manager.h"
#include "darkroom_timer_payload.h"
#include "relay_controller.h"

#include <stdlib.h>
#include <string.h>

#define TAG "ShellyAction"

// Wire format: typed JSON {shelly_host, shelly_relay, shelly_on} mapping
// directly to ShellyPayload.
static void shelly_parse(const JsonObject& a, ButtonAction& act) {
    ShellyPayload& p = shelly_payload(act);
    strlcpy(p.host, a["shelly_host"] | "", sizeof(p.host));
    p.relay = a["shelly_relay"] | 0;
    p.on    = a["shelly_on"] | true;
}

static void shelly_serialize(const ButtonAction& act, JsonObject obj) {
    const ShellyPayload& p = shelly_payload(act);
    if (p.host[0]) obj["shelly_host"] = p.host;
    obj["shelly_relay"] = p.relay;
    obj["shelly_on"]    = p.on;
}

static void shelly_dispatch(const ButtonAction& act, const char* label) {
    const ShellyPayload& p = shelly_payload(act);
    if (!p.host[0]) {
        LOGW(TAG, "%s shelly: empty host", label);
        return;
    }
    LOGI(TAG, "%s shelly: %s relay %u %s", label, p.host, p.relay,
         p.on ? "on" : "off");
    relay_queue_shelly(p.host, p.relay, p.on);
}

static const ActionTypeDef shelly_action_type = {
    /* type_name   */ ACTION_TYPE_SHELLY,
    /* parse       */ shelly_parse,
    /* serialize   */ shelly_serialize,
    /* dispatch    */ shelly_dispatch,
    /* value_field */ nullptr,  // host/relay/on — no single bindable value field
};

REGISTER_ACTION_TYPE(shelly_action_type);

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
