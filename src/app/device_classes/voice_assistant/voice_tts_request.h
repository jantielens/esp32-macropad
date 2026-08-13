#pragma once

#include <stdbool.h>
#include <stddef.h>

// Validates an optional ISO 639-1 language code.
bool voice_tts_language_valid(const char* language);

// Builds the Azure OpenAI speech request JSON. Returns the formatted length,
// or a negative value when an input is invalid or the output does not fit.
int voice_tts_build_request_json(char* out, size_t out_size, const char* text,
                                 const char* model, const char* voice,
                                 const char* language, const char* instructions);