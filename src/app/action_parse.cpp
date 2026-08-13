#include "action_parse.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_registry.h"
#include <string.h>

void action_parse(const JsonObject& a, ButtonAction& act) {
    memset(&act, 0, sizeof(ButtonAction));
    strlcpy(act.type, a["type"] | "", sizeof(act.type));
    if (!act.type[0]) return;

    const ActionTypeDef* type = action_type_find(act.type);
    if (type && type->parse) {
        type->parse(a, act);
    } else if (strcmp(act.type, "beep") == 0 || strcmp(act.type, "sound") == 0) {
        memset(&act, 0, sizeof(ButtonAction));
    }
}

void action_to_json(const ButtonAction& act, JsonObject obj) {
    if (!act.type[0]) return;  // empty action → empty object
    obj["type"] = act.type;

    const ActionTypeDef* type = action_type_find(act.type);
    if (type && type->serialize) type->serialize(act, obj);
}

#endif // HAS_DISPLAY || HAS_BUTTON
