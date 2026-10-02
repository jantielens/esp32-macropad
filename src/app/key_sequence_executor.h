#pragma once

#include "key_sequence.h"

class KeySequenceExecutor {
public:
    using ReportSink = bool (*)(void*, KsUsageType, uint16_t, uint8_t);
    enum class Result { Idle, Running, Success, Failed };

    bool start(const char* sequence, uint32_t now);
    Result poll(uint32_t now, ReportSink sink, void* context);
    void cancel(ReportSink sink, void* context);
    bool active() const { return state_ != State::Idle; }
    const char* error() const { return parsed_.error; }

private:
    enum class State { Idle, Next, Release };
    char sequence_[256] = {};
    KsSequence parsed_ = {};
    State state_ = State::Idle;
    uint8_t step_index_ = 0;
    uint16_t text_offset_ = 0;
    uint32_t deadline_ = 0;
    uint32_t next_delay_ = 0;
    KsUsageType held_type_ = KS_USAGE_KEYBOARD;
};