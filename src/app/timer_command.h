#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"
#include "timer_engine.h"

#include <stddef.h>
#include <stdint.h>

enum PreparedTimerCommandType : uint8_t {
    PREPARED_TIMER_START,
    PREPARED_TIMER_TOGGLE,
    PREPARED_TIMER_STOP,
    PREPARED_TIMER_PAUSE,
    PREPARED_TIMER_RESUME,
    PREPARED_TIMER_RESET,
    PREPARED_TIMER_SET,
    PREPARED_TIMER_ADJUST,
};

struct PreparedTimerCommand {
    PreparedTimerCommandType command;
    uint8_t timer_id;
    TimerMode mode;
    TimerState expected_state;
    uint32_t value_ms;
    int32_t delta_seconds;
    bool needs_expiry_snapshot;
};

struct TimerExpirySnapshot;

bool timer_command_prepare(const TimerPayload& payload, PreparedTimerCommand* out,
                           char* error, size_t error_len);
bool timer_command_execute(const PreparedTimerCommand& command,
                           const TimerExpirySnapshot* expiry_snapshot,
                           char* error, size_t error_len);
bool timer_command_run(const TimerPayload& payload, char* error, size_t error_len);

#endif
