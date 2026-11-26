#pragma once

#include <stddef.h>
#include <stdint.h>

enum class KeyboardLayout : uint8_t {
    Us, UsInternational, Gb, Fr, Be, De,
    Es, It, Pt, Br, Se, Fi, No, Dk, ChDe, ChFr, Pl, Cz, Tr, Hu,
    Count
};

const char* keyboard_layout_name(KeyboardLayout layout);
bool keyboard_layout_parse(const char* value, KeyboardLayout* layout);

struct KeyboardStroke {
    uint8_t usage;
    uint8_t modifiers;
};

struct KeyboardCharacter {
    KeyboardStroke strokes[2];
    uint8_t count;
};

constexpr size_t KEYBOARD_TEXT_MAX_BYTES = 2048;

bool keyboard_utf8_next(const char* text, size_t length, size_t* offset, uint32_t* codepoint);
bool keyboard_layout_character(KeyboardLayout layout, uint32_t codepoint, KeyboardCharacter* output);
