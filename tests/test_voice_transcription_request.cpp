#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "device_classes/voice_assistant/voice_transcription_request.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    char prefix[384] = {};
    const char* boundary = "----test-boundary";

    int length = voice_transcription_build_multipart_prefix(prefix, sizeof(prefix), boundary, "nl");
    check(length > 0 && (size_t)length < sizeof(prefix), "language prefix fits");
    check(std::strstr(prefix, "name=\"language\"\r\n\r\nnl\r\n") != nullptr,
          "configured language is sent to STT");
    check(std::strstr(prefix, "name=\"file\"; filename=\"recording.wav\"") != nullptr,
          "language request includes WAV file part");

    length = voice_transcription_build_multipart_prefix(prefix, sizeof(prefix), boundary, "");
    check(length > 0 && (size_t)length < sizeof(prefix), "auto-detect prefix fits");
    check(std::strstr(prefix, "name=\"language\"") == nullptr,
          "empty language leaves STT auto-detection enabled");

    std::puts("Voice transcription request checks passed");
    return 0;
}