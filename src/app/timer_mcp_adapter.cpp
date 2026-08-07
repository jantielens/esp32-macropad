#include "timer_mcp_adapter.h"

#if HAS_DISPLAY && HAS_MCP

#include "timer_engine.h"

#include <string.h>

static bool parse_error(char* error, size_t error_len, const char* message) {
    if (error && error_len > 0) strlcpy(error, message, error_len);
    return false;
}

bool timer_mcp_parse_args(JsonObjectConst args, TimerPayload* payload,
                          char* error, size_t error_len) {
    if (error && error_len > 0) error[0] = '\0';
    if (!payload) return parse_error(error, error_len, "missing timer payload");
    memset(payload, 0, sizeof(*payload));

    JsonVariantConst id_value = args["timer_id"];
    if (id_value.isNull()) {
        return parse_error(error, error_len, "missing 'timer_id' (1-3)");
    }
    int id = id_value | 0;
    if (id < 1 || id > TIMER_COUNT) {
        return parse_error(error, error_len, "timer_id out of range (1-3)");
    }
    const char* command = args["command"] | (const char*)nullptr;
    if (!command || !command[0]) {
        return parse_error(error, error_len, "missing 'command'");
    }
    static const char* commands[] = {
        "start", "stop", "toggle", "pause", "resume", "reset", "adjust", "set"
    };
    bool known = false;
    for (const char* candidate : commands) {
        if (strcmp(command, candidate) == 0) {
            known = true;
            break;
        }
    }
    if (!known) {
        return parse_error(error, error_len,
                           "command must be start|stop|toggle|pause|resume|reset|adjust|set");
    }
    if (strlen(command) >= sizeof(payload->timer_command)) {
        return parse_error(error, error_len, "command is too long");
    }

    payload->timer_id = (uint8_t)id;
    strlcpy(payload->timer_command, command, sizeof(payload->timer_command));
    JsonVariantConst mode_value = args["mode"];
    if (!mode_value.isNull()) {
        const char* mode = mode_value | "";
        if (strlen(mode) >= sizeof(payload->timer_mode)) {
            return parse_error(error, error_len, "mode is too long");
        }
        strlcpy(payload->timer_mode, mode, sizeof(payload->timer_mode));
    }
    JsonVariantConst value_variant = args["value"];
    if (!value_variant.isNull()) {
        if (value_variant.is<const char*>()) {
            const char* value = value_variant | "";
            if (strlen(value) >= sizeof(payload->timer_value)) {
                return parse_error(error, error_len, "value is too long");
            }
            strlcpy(payload->timer_value, value, sizeof(payload->timer_value));
        } else {
            size_t written = serializeJson(value_variant, payload->timer_value,
                                           sizeof(payload->timer_value));
            if (written == 0 || written >= sizeof(payload->timer_value) - 1) {
                return parse_error(error, error_len, "value is out of range");
            }
        }
    }
    return true;
}

#endif
