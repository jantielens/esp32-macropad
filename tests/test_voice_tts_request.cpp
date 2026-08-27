#include <cstring>
#include <gtest/gtest.h>

#include "device_classes/voice_assistant/voice_tts_request.h"

TEST(VoiceTtsRequest, BuildsEscapedRequestAndValidatesLanguage) {
    char request[2048] = {};
    const int length = voice_tts_build_request_json(
        request, sizeof(request), "A \"quote\"\\newline\n", "gpt-4o-mini-tts", "alloy", "NL",
        "Use a \"Flemish\" accent.");
    ASSERT_GT(length, 0);
    EXPECT_LT(static_cast<size_t>(length), sizeof(request));
    EXPECT_NE(std::strstr(request, "\"model\":\"gpt-4o-mini-tts\""), nullptr);
    EXPECT_NE(std::strstr(request, "\"input\":\"A \\\"quote\\\"\\\\newline\\n\""), nullptr);
    EXPECT_NE(std::strstr(request, "\"voice\":\"alloy\""), nullptr);
    EXPECT_NE(std::strstr(request, "\"response_format\":\"mp3\""), nullptr);
    EXPECT_NE(std::strstr(request, "\"instructions\":\"Speak the input using ISO 639-1 language code NL. Use a \\\"Flemish\\\" accent.\""), nullptr);
    EXPECT_EQ(std::strstr(request, "\"language\""), nullptr);
    EXPECT_TRUE(voice_tts_language_valid(""));
    EXPECT_TRUE(voice_tts_language_valid("en"));
    EXPECT_FALSE(voice_tts_language_valid("eng"));
    EXPECT_FALSE(voice_tts_language_valid("e1"));
    ASSERT_GT(voice_tts_build_request_json(request, sizeof(request), "hello", "gpt-4o-mini-tts", "alloy", "", "Use the exact configured pronunciation."), 0);
    EXPECT_NE(std::strstr(request, "\"instructions\":\"Use the exact configured pronunciation.\""), nullptr);
    ASSERT_GT(voice_tts_build_request_json(request, sizeof(request), "hello", "gpt-4o-mini-tts", "alloy", "", ""), 0);
    EXPECT_EQ(std::strstr(request, "\"instructions\""), nullptr);
    EXPECT_LT(voice_tts_build_request_json(request, 16, "hello", "gpt-4o-mini-tts", "alloy", "", ""), 0);
}