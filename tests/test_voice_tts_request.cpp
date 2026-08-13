#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "device_classes/voice_assistant/voice_tts_request.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    char request[2048] = {};
    const int length = voice_tts_build_request_json(
        request, sizeof(request), "A \"quote\"\\newline\n", "gpt-4o-mini-tts", "alloy", "NL",
        "Use a \"Flemish\" accent.");
    check(length > 0 && (size_t)length < sizeof(request), "request fits");
    check(std::strstr(request, "\"model\":\"gpt-4o-mini-tts\"") != nullptr,
          "model is included");
    check(std::strstr(request, "\"input\":\"A \\\"quote\\\"\\\\newline\\n\"") != nullptr,
          "input is JSON escaped");
    check(std::strstr(request, "\"voice\":\"alloy\"") != nullptr,
          "voice is included");
    check(std::strstr(request, "\"response_format\":\"mp3\"") != nullptr,
          "MP3 response is requested");
    check(std::strstr(request, "\"instructions\":\"Speak the input using ISO 639-1 language code NL. Use a \\\"Flemish\\\" accent.\"") != nullptr,
          "language and custom instructions are included");
    check(std::strstr(request, "\"language\"") == nullptr,
          "language is not an HTTP request field");
    check(voice_tts_language_valid("") && voice_tts_language_valid("en"),
          "empty and two-letter language codes are valid");
    check(!voice_tts_language_valid("eng") && !voice_tts_language_valid("e1"),
          "invalid language codes are rejected");
    check(voice_tts_build_request_json(request, sizeof(request), "hello", "gpt-4o-mini-tts", "alloy", "",
                                       "Use the exact configured pronunciation.") > 0 &&
          std::strstr(request, "\"instructions\":\"Use the exact configured pronunciation.\"") != nullptr,
          "custom instructions are preserved without a language");
    check(voice_tts_build_request_json(request, sizeof(request), "hello", "gpt-4o-mini-tts", "alloy", "", "") > 0 &&
          std::strstr(request, "\"instructions\"") == nullptr,
          "instructions are omitted when both settings are empty");
    check(voice_tts_build_request_json(request, 16, "hello", "gpt-4o-mini-tts", "alloy", "", "") < 0,
          "small output buffer is rejected");
    std::puts("Voice TTS request checks passed");
    return 0;
}