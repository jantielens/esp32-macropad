#include "timer_command.h"

#if HAS_DISPLAY

#include "timer_config.h"

#include <limits.h>
#include <string.h>

static bool timer_error(char* error, size_t error_len, const char* message) {
    if (error && error_len > 0) strlcpy(error, message, error_len);
    return false;
}

static bool parse_timer_unsigned(const char* value, uint32_t max_value,
                                 bool require_positive, uint32_t* out) {
    if (!value || !value[0] || !out) return false;
    uint64_t parsed = 0;
    for (const char* cursor = value; *cursor; cursor++) {
        if (*cursor < '0' || *cursor > '9') return false;
        parsed = parsed * 10 + (uint8_t)(*cursor - '0');
        if (parsed > max_value) return false;
    }
    if (require_positive && parsed == 0) return false;
    *out = (uint32_t)parsed;
    return true;
}

static bool parse_timer_signed(const char* value, int32_t* out) {
    if (!value || !value[0] || !out) return false;
    bool negative = value[0] == '-';
    const char* cursor = (negative || value[0] == '+') ? value + 1 : value;
    if (!cursor[0]) return false;
    uint64_t limit = negative ? (uint64_t)INT32_MAX + 1 : INT32_MAX;
    uint64_t parsed = 0;
    for (; *cursor; cursor++) {
        if (*cursor < '0' || *cursor > '9') return false;
        parsed = parsed * 10 + (uint8_t)(*cursor - '0');
        if (parsed > limit) return false;
    }
    if (negative && parsed == (uint64_t)INT32_MAX + 1) {
        *out = INT32_MIN;
    } else {
        *out = negative ? -(int32_t)parsed : (int32_t)parsed;
    }
    return true;
}

bool timer_command_prepare(const TimerPayload& payload, PreparedTimerCommand* out,
                           char* error, size_t error_len) {
    if (error && error_len > 0) error[0] = '\0';
    if (!out) return timer_error(error, error_len, "missing prepared command");
    memset(out, 0, sizeof(*out));
    if (payload.timer_id < 1 || payload.timer_id > TIMER_COUNT) {
        return timer_error(error, error_len, "timer_id must be 1 through 3");
    }
    if (!payload.timer_command[0]) {
        return timer_error(error, error_len, "command is required");
    }

    out->timer_id = payload.timer_id;
    const char* command = payload.timer_command;
    bool is_start = strcmp(command, "start") == 0;
    bool is_toggle = strcmp(command, "toggle") == 0;

    if (is_start || is_toggle) {
        out->command = is_start ? PREPARED_TIMER_START : PREPARED_TIMER_TOGGLE;
        if (strcmp(payload.timer_mode, "up") == 0) {
            out->mode = TIMER_MODE_UP;
            if (payload.timer_value[0]) {
                return timer_error(error, error_len,
                                   "count-up start must not include value");
            }
        } else if (strcmp(payload.timer_mode, "down") == 0) {
            out->mode = TIMER_MODE_DOWN;
            uint32_t seconds = 0;
            if (!parse_timer_unsigned(payload.timer_value, UINT32_MAX / 1000,
                                      true, &seconds)) {
                return timer_error(error, error_len,
                                   "countdown value must be positive whole seconds");
            }
            out->value_ms = seconds * 1000;
        } else {
            return timer_error(error, error_len, "mode must be 'up' or 'down'");
        }
        out->expected_state = timer_get_state(payload.timer_id);
        out->needs_expiry_snapshot = out->mode == TIMER_MODE_DOWN
            && (is_start || out->expected_state == TIMER_STOPPED);
        return true;
    }

    if (payload.timer_mode[0]) {
        return timer_error(error, error_len, "mode is only valid for start and toggle");
    }
    if (strcmp(command, "set") == 0) {
        out->command = PREPARED_TIMER_SET;
        uint32_t seconds = 0;
        if (!parse_timer_unsigned(payload.timer_value, UINT32_MAX / 1000,
                                  false, &seconds)) {
            return timer_error(error, error_len,
                               "set value must be non-negative whole seconds");
        }
        if (timer_get_mode(payload.timer_id) != TIMER_MODE_DOWN) {
            return timer_error(error, error_len, "set requires countdown mode");
        }
        out->value_ms = seconds * 1000;
        return true;
    }
    if (strcmp(command, "adjust") == 0) {
        out->command = PREPARED_TIMER_ADJUST;
        if (!parse_timer_signed(payload.timer_value, &out->delta_seconds)) {
            return timer_error(error, error_len,
                               "adjust value must be signed whole seconds");
        }
        if (timer_get_mode(payload.timer_id) != TIMER_MODE_DOWN) {
            return timer_error(error, error_len, "adjust requires countdown mode");
        }
        return true;
    }

    if (payload.timer_value[0]) {
        return timer_error(error, error_len, "value is not valid for this command");
    }
    if (strcmp(command, "stop") == 0) out->command = PREPARED_TIMER_STOP;
    else if (strcmp(command, "pause") == 0) out->command = PREPARED_TIMER_PAUSE;
    else if (strcmp(command, "resume") == 0) out->command = PREPARED_TIMER_RESUME;
    else if (strcmp(command, "reset") == 0) out->command = PREPARED_TIMER_RESET;
    else return timer_error(error, error_len, "unsupported timer command");
    return true;
}

bool timer_command_execute(const PreparedTimerCommand& command,
                           const TimerExpirySnapshot* expiry_snapshot,
                           char* error, size_t error_len) {
    const ButtonAction* actions = nullptr;
    uint8_t action_count = 0;
    if (command.needs_expiry_snapshot) {
        if (!expiry_snapshot) {
            return timer_error(error, error_len, "missing expiry snapshot");
        }
        actions = expiry_snapshot->actions;
        action_count = expiry_snapshot->count;
    }

    switch (command.command) {
        case PREPARED_TIMER_START:
            if (!timer_configure_and_start(command.timer_id, command.mode,
                                           command.value_ms, actions, action_count)) {
                return timer_error(error, error_len, "timer start configuration rejected");
            }
            return true;
        case PREPARED_TIMER_TOGGLE:
            if (!timer_toggle_prepared(command.timer_id, command.expected_state,
                                       command.mode, command.value_ms,
                                       actions, action_count)) {
                return timer_error(error, error_len, "timer state changed before toggle");
            }
            return true;
        case PREPARED_TIMER_STOP:
            if (!timer_stop(command.timer_id)) {
                return timer_error(error, error_len, "timer control rejected");
            }
            return true;
        case PREPARED_TIMER_PAUSE:
            if (!timer_pause(command.timer_id)) {
                return timer_error(error, error_len, "timer control rejected");
            }
            return true;
        case PREPARED_TIMER_RESUME:
            if (!timer_resume(command.timer_id)) {
                return timer_error(error, error_len, "timer control rejected");
            }
            return true;
        case PREPARED_TIMER_RESET:
            if (!timer_reset(command.timer_id)) {
                return timer_error(error, error_len, "timer control rejected");
            }
            return true;
        case PREPARED_TIMER_SET:
            if (!timer_set_countdown_ms(command.timer_id, command.value_ms)) {
                return timer_error(error, error_len, "set requires countdown mode");
            }
            return true;
        case PREPARED_TIMER_ADJUST:
            if (!timer_adjust(command.timer_id, command.delta_seconds)) {
                return timer_error(error, error_len, "adjust requires countdown mode");
            }
            return true;
    }
    return timer_error(error, error_len, "unsupported timer command");
}

bool timer_command_run(const TimerPayload& payload, char* error, size_t error_len) {
    PreparedTimerCommand command = {};
    if (!timer_command_prepare(payload, &command, error, error_len)) return false;
    TimerExpirySnapshot snapshot = {};
    const TimerExpirySnapshot* snapshot_ptr = nullptr;
    if (command.needs_expiry_snapshot) {
        if (!timer_config_snapshot_expiry(command.timer_id, &snapshot)) {
            return timer_error(error, error_len, "failed to snapshot expiry actions");
        }
        snapshot_ptr = &snapshot;
    }
    return timer_command_execute(command, snapshot_ptr, error, error_len);
}

#endif
