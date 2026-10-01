#include "key_sequence_executor.h"

#include <string.h>

bool KeySequenceExecutor::start(const char* sequence, uint32_t now) {
    if (active() || !sequence || !sequence[0] || strlen(sequence) >= sizeof(sequence_)) {
        return false;
    }
    strcpy(sequence_, sequence);
    if (!ks_parse(sequence_, &parsed_)) {
        return false;
    }
    step_index_ = 0;
    text_offset_ = 0;
    deadline_ = now;
    state_ = State::Next;
    return true;
}

void KeySequenceExecutor::cancel(ReportSink sink, void* context) {
    if (state_ == State::Release && sink) {
        sink(context, held_type_, 0, 0);
    }
    state_ = State::Idle;
}

KeySequenceExecutor::Result KeySequenceExecutor::poll(uint32_t now, ReportSink sink, void* context) {
    if (!active()) {
        return Result::Idle;
    }
    if (static_cast<int32_t>(now - deadline_) < 0) {
        return Result::Running;
    }
    if (!sink) {
        cancel(nullptr, context);
        return Result::Failed;
    }
    if (state_ == State::Release) {
        if (!sink(context, held_type_, 0, 0)) {
            cancel(sink, context);
            return Result::Failed;
        }
        state_ = State::Next;
        deadline_ = now + next_delay_;
        return Result::Running;
    }

    while (step_index_ < parsed_.count) {
        const KsStep& step = parsed_.steps[step_index_];
        if (step.type == KS_STEP_DELAY) {
            ++step_index_;
            deadline_ = now + step.delay.ms;
            return Result::Running;
        }
        uint16_t usage = 0;
        uint8_t modifiers = 0;
        KsUsageType usage_type = KS_USAGE_KEYBOARD;
        if (step.type == KS_STEP_TEXT) {
            if (text_offset_ >= step.text.length) {
                text_offset_ = 0;
                ++step_index_;
                continue;
            }
            char character = step.text.start[text_offset_++];
            if (character == '\\' && text_offset_ < step.text.length &&
                step.text.start[text_offset_] == '"') {
                character = '"';
                ++text_offset_;
            }
            if (!ks_ascii_to_hid(character, &usage, &modifiers)) {
                continue;
            }
            if (text_offset_ == step.text.length) {
                text_offset_ = 0;
                ++step_index_;
            }
        } else {
            usage = step.key.usage;
            modifiers = step.key.modifiers;
            usage_type = step.key.usage_type;
            ++step_index_;
        }
        next_delay_ = text_offset_ == 0 && step_index_ < parsed_.count &&
                      parsed_.steps[step_index_].type != KS_STEP_DELAY ? KS_DEFAULT_DELAY_MS : 0;
        held_type_ = usage_type;
        state_ = State::Release;
        if (!sink(context, usage_type, usage, modifiers)) {
            cancel(sink, context);
            return Result::Failed;
        }
        deadline_ = now + KS_DEFAULT_DELAY_MS;
        return Result::Running;
    }
    state_ = State::Idle;
    return Result::Success;
}