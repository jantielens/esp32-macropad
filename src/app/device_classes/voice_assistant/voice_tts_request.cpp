#include "voice_tts_request.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {
bool append_char(char* out, size_t out_size, size_t* used, char value) {
    if (*used + 1 >= out_size) return false;
    out[(*used)++] = value;
    out[*used] = '\0';
    return true;
}

bool append_text(char* out, size_t out_size, size_t* used, const char* value) {
    for (; *value; ++value) {
        const unsigned char c = (unsigned char)*value;
        switch (c) {
            case '"': if (!append_char(out, out_size, used, '\\') || !append_char(out, out_size, used, '"')) return false; break;
            case '\\': if (!append_char(out, out_size, used, '\\') || !append_char(out, out_size, used, '\\')) return false; break;
            case '\b': if (!append_char(out, out_size, used, '\\') || !append_char(out, out_size, used, 'b')) return false; break;
            case '\f': if (!append_char(out, out_size, used, '\\') || !append_char(out, out_size, used, 'f')) return false; break;
            case '\n': if (!append_char(out, out_size, used, '\\') || !append_char(out, out_size, used, 'n')) return false; break;
            case '\r': if (!append_char(out, out_size, used, '\\') || !append_char(out, out_size, used, 'r')) return false; break;
            case '\t': if (!append_char(out, out_size, used, '\\') || !append_char(out, out_size, used, 't')) return false; break;
            default:
                if (c < 0x20) {
                    if (*used + 6 >= out_size) return false;
                    const int written = snprintf(out + *used, out_size - *used, "\\u%04x", c);
                    if (written != 6) return false;
                    *used += 6;
                } else if (!append_char(out, out_size, used, (char)c)) {
                    return false;
                }
        }
    }
    return true;
}

bool append_literal(char* out, size_t out_size, size_t* used, const char* value) {
    while (*value && append_char(out, out_size, used, *value++)) {}
    return *value == '\0';
}

bool append_instructions(char* out, size_t out_size, size_t* used, const char* language,
                         const char* instructions) {
    const bool has_language = language && language[0];
    const bool has_instructions = instructions && instructions[0];
    if (!has_language && !has_instructions) return true;
    if (!append_literal(out, out_size, used, ",\"instructions\":\"")) return false;
    if (has_language &&
        (!append_literal(out, out_size, used, "Speak the input using ISO 639-1 language code ") ||
         !append_text(out, out_size, used, language) ||
         !append_literal(out, out_size, used, "."))) {
        return false;
    }
    if (has_instructions &&
        ((has_language && !append_char(out, out_size, used, ' ')) ||
         !append_text(out, out_size, used, instructions))) {
        return false;
    }
    return append_char(out, out_size, used, '"');
}

} // namespace

bool voice_tts_language_valid(const char* language) {
    if (!language || !language[0]) return true;
    return language[0] && language[1] && !language[2] &&
           isalpha((unsigned char)language[0]) && isalpha((unsigned char)language[1]);
}

int voice_tts_build_request_json(char* out, size_t out_size, const char* text,
                                 const char* model, const char* voice,
                                 const char* language, const char* instructions) {
    if (!out || !out_size || !text || !text[0] || !model || !model[0] || !voice || !voice[0] ||
        !voice_tts_language_valid(language)) {
        return -1;
    }
    size_t used = 0;
    out[0] = '\0';
    if (!append_literal(out, out_size, &used, "{\"model\":\"") ||
        !append_text(out, out_size, &used, model) ||
        !append_literal(out, out_size, &used, "\",\"input\":\"") ||
        !append_text(out, out_size, &used, text) ||
        !append_literal(out, out_size, &used, "\",\"voice\":\"") ||
        !append_text(out, out_size, &used, voice) ||
        !append_literal(out, out_size, &used, "\",\"response_format\":\"mp3\"") ||
        !append_instructions(out, out_size, &used, language, instructions) ||
        !append_literal(out, out_size, &used, "}")) {
        return -1;
    }
    return (int)used;
}