#pragma once

#include "key_sequence.h"

class KeySequenceExecutor {
public:
    using ReportSink = bool (*)(void*, KsUsageType, uint16_t, uint8_t);
    enum class Result { Idle, Running, Success, Failed };

    bool start(const char* sequence, uint32_t now, KeyboardLayout layout = KeyboardLayout::Us);
    bool start_text(const char* text, uint32_t now, KeyboardLayout layout = KeyboardLayout::Us,
                    bool allow_controls = false);
    Result poll(uint32_t now, ReportSink sink, void* context);
    void cancel(ReportSink sink, void* context);
    void clear();
    bool active() const { return state_ != State::Idle; }
    const char* error() const { return parsed_.error; }
    uint32_t timeout_ms() const { return timeout_ms_; }
    uint16_t completed_bytes() const { return completed_bytes_; }
    uint16_t total_bytes() const { return total_bytes_; }

private:
    bool prepare(uint32_t now);
    bool read_character(const KsStep& step, uint16_t* offset, KeyboardCharacter* character);
    enum class State { Idle, Next, Release };
    char sequence_[KEYBOARD_TEXT_MAX_BYTES + 1] = {};
    KsSequence parsed_ = {};
    State state_ = State::Idle;
    uint8_t step_index_ = 0;
    uint16_t text_offset_ = 0;
    uint32_t deadline_ = 0;
    uint32_t next_delay_ = 0;
    KsUsageType held_type_ = KS_USAGE_KEYBOARD;
    KeyboardLayout layout_ = KeyboardLayout::Us;
    KeyboardCharacter character_ = {};
    uint8_t stroke_index_ = 0;
    bool literal_ = false;
    bool allow_controls_ = false;
    uint32_t timeout_ms_ = 60000;
    uint16_t completed_bytes_ = 0;
    uint16_t total_bytes_ = 0;
};