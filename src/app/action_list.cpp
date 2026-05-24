#include "action_list.h"

#if HAS_DISPLAY

#include "action_dispatch.h"
#include "action_parse.h"

#include <string.h>

uint8_t action_list_parse(JsonVariant v, ButtonAction* out, uint8_t max) {
    if (!out || max == 0) return 0;
    memset(out, 0, sizeof(ButtonAction) * max);
    if (!v.is<JsonArray>()) return 0;
    JsonArray arr = v.as<JsonArray>();
    uint8_t count = 0;
    for (size_t i = 0; i < arr.size() && count < max; i++) {
        if (!arr[i].is<JsonObject>()) continue;
        action_parse(arr[i].as<JsonObject>(), out[count]);
        if (out[count].type[0]) {
            count++;
        } else {
            memset(&out[count], 0, sizeof(ButtonAction));
        }
    }
    return count;
}

void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char* label) {
    if (!actions) return;
    for (uint8_t i = 0; i < count; i++) {
        action_dispatch(actions[i], label);
    }
}

#endif // HAS_DISPLAY
