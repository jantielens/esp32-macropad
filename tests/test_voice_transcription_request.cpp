#include <cstring>
#include <gtest/gtest.h>

#include "device_classes/voice_assistant/voice_transcription_request.h"

TEST(VoiceTranscriptionRequest, BuildsLanguageAndAutoDetectPrefixes) {
    char prefix[384] = {};
    const char* boundary = "----test-boundary";

    int length = voice_transcription_build_multipart_prefix(prefix, sizeof(prefix), boundary, "nl");
        ASSERT_GT(length, 0);
        EXPECT_LT(static_cast<size_t>(length), sizeof(prefix));
        EXPECT_NE(std::strstr(prefix, "name=\"language\"\r\n\r\nnl\r\n"), nullptr);
        EXPECT_NE(std::strstr(prefix, "name=\"file\"; filename=\"recording.wav\""), nullptr);

    length = voice_transcription_build_multipart_prefix(prefix, sizeof(prefix), boundary, "");
    ASSERT_GT(length, 0);
    EXPECT_LT(static_cast<size_t>(length), sizeof(prefix));
    EXPECT_EQ(std::strstr(prefix, "name=\"language\""), nullptr);
}