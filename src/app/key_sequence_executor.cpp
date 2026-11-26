#include "key_sequence_executor.h"

#include <string.h>
#include <stdio.h>

bool KeySequenceExecutor::start(const char* sequence, uint32_t now, KeyboardLayout layout) {
    if (active() || !sequence || !sequence[0] || strlen(sequence) >= 256) {
        return false;
    }
    literal_ = false;
    allow_controls_ = false;
    layout_ = layout;
    strcpy(sequence_, sequence);
    if (!ks_parse_layout(sequence_, &parsed_, layout)) {
        return false;
    }
    return prepare(now);
}

bool KeySequenceExecutor::start_text(const char* text, uint32_t now, KeyboardLayout layout,
                                     bool allow_controls) {
    if (active() || !text || !text[0] || strlen(text) > KEYBOARD_TEXT_MAX_BYTES) return false;
    literal_ = true;
    allow_controls_ = allow_controls;
    layout_ = layout;
    strcpy(sequence_, text);
    memset(&parsed_, 0, sizeof(parsed_));
    parsed_.count = 1;
    parsed_.steps[0].type = KS_STEP_TEXT;
    parsed_.steps[0].text.start = sequence_;
    parsed_.steps[0].text.length = static_cast<uint16_t>(strlen(text));
    return prepare(now);
}

bool KeySequenceExecutor::read_character(const KsStep& step, uint16_t* offset,
                                         KeyboardCharacter* character) {
    size_t next = *offset;
    uint32_t codepoint = 0;
    if (!keyboard_utf8_next(step.text.start, step.text.length, &next, &codepoint)) {
        snprintf(parsed_.error, sizeof(parsed_.error), "Invalid UTF-8 at byte %u", unsigned(*offset));
        return false;
    }
    if (!literal_ && codepoint == '\\' && next < step.text.length && step.text.start[next] == '"') {
        codepoint = '"';
        ++next;
    }
    if (literal_ && allow_controls_ && (codepoint == '\r' || codepoint == '\n' || codepoint == '\t')) {
        if (codepoint == '\r' && next < step.text.length && step.text.start[next] == '\n') ++next;
        *character = {{{static_cast<uint8_t>(codepoint == '\t' ? 0x2b : 0x28), 0}, {}}, 1};
    } else if (!keyboard_layout_character(layout_, codepoint, character)) {
        snprintf(parsed_.error, sizeof(parsed_.error), "Unsupported character U+%04lX",
                 static_cast<unsigned long>(codepoint));
        return false;
    }
    *offset = static_cast<uint16_t>(next);
    return true;
}

bool KeySequenceExecutor::prepare(uint32_t now) {
    if (!keyboard_layout_name(layout_)) return false;
    uint32_t strokes = 0;
    for (uint8_t index = 0; index < parsed_.count; ++index) {
        const KsStep& step = parsed_.steps[index];
        if (step.type != KS_STEP_TEXT) continue;
        uint16_t offset = 0;
        while (offset < step.text.length) {
            KeyboardCharacter character = {};
            if (!read_character(step, &offset, &character)) return false;
            strokes += character.count;
        }
    }
    step_index_ = 0;
    text_offset_ = 0;
    stroke_index_ = 0;
    character_ = {};
    completed_bytes_ = 0;
    total_bytes_ = static_cast<uint16_t>(strlen(sequence_));
    timeout_ms_ = literal_ ? (strokes * 60 + 10000) : 60000;
    if (timeout_ms_ < 60000) timeout_ms_ = 60000;
    if (timeout_ms_ > 300000) timeout_ms_ = 300000;
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

void KeySequenceExecutor::clear() {
    state_ = State::Idle;
    memset(sequence_, 0, sizeof(sequence_));
    memset(&parsed_, 0, sizeof(parsed_));
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
        if (literal_ && stroke_index_ == character_.count) completed_bytes_ = text_offset_;
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
            if (stroke_index_ == character_.count && text_offset_ >= step.text.length) {
                text_offset_ = 0;
                ++step_index_;
                continue;
            }
            if (stroke_index_ == character_.count) {
                if (!read_character(step, &text_offset_, &character_)) {
                    cancel(sink, context);
                    return Result::Failed;
                }
                stroke_index_ = 0;
            }
            const KeyboardStroke& stroke = character_.strokes[stroke_index_++];
            usage = stroke.usage;
            modifiers = stroke.modifiers;
            if (!literal_ && stroke_index_ == character_.count && text_offset_ == step.text.length) {
                character_ = {};
                stroke_index_ = 0;
                ++step_index_;
                text_offset_ = 0;
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
    completed_bytes_ = total_bytes_;
    state_ = State::Idle;
    return Result::Success;
}