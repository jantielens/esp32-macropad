#include "voice_notes_enrichment.h"
#include <gtest/gtest.h>
#include <string>

namespace {
std::string completion(const char* content, const char* finish = "stop",
                       const char* refusal = nullptr) {
    JsonDocument envelope;
    JsonObject choice = envelope["choices"].to<JsonArray>().add<JsonObject>();
    choice["finish_reason"] = finish;
    choice["message"]["role"] = "assistant";
    choice["message"]["content"] = content;
    if (refusal) choice["message"]["refusal"] = refusal;
    std::string output;
    serializeJson(envelope, output);
    return output;
}

VoiceNotesEnrichmentParse parse(const std::string& response, JsonDocument& result) {
    JsonDocument envelope;
    return voice_notes_enrichment_parse_response(response.data(), response.size(), envelope, result);
}

class FailingAllocator : public ArduinoJson::Allocator {
    void* allocate(size_t) override { return nullptr; }
    void deallocate(void*) override {}
    void* reallocate(void*, size_t) override { return nullptr; }
};
}

TEST(VoiceNotesEnrichment, BuildsStrictRequestWithEscapedTranscript) {
    JsonDocument request;
    const char* transcript = "A \"quoted\" line\nand a second line";
    ASSERT_TRUE(voice_notes_enrichment_build_request(request, "deployment", transcript));
    std::string serialized;
    serializeJson(request, serialized);
    JsonDocument parsed;
    ASSERT_FALSE(deserializeJson(parsed, serialized));
    EXPECT_STREQ(parsed["model"], "deployment");
    EXPECT_STREQ(parsed["messages"][1]["content"], transcript);
    EXPECT_STREQ(parsed["response_format"]["type"], "json_schema");
    JsonObjectConst wrapper = parsed["response_format"]["json_schema"];
    EXPECT_TRUE(wrapper["strict"].as<bool>());
    EXPECT_FALSE(wrapper["schema"]["additionalProperties"].as<bool>());
    EXPECT_EQ(wrapper["schema"]["required"].size(), 2u);
    EXPECT_STREQ(wrapper["schema"]["properties"]["title"]["type"], "string");
    EXPECT_STREQ(wrapper["schema"]["properties"]["summary"]["type"], "string");
}

TEST(VoiceNotesEnrichment, AcceptsCompleteStructuredResult) {
    JsonDocument result;
    const std::string response = completion(
        "{\"title\":\"Kitchen ideas\",\"summary\":\"Compare storage options.\"}");
    ASSERT_EQ(parse(response, result), VoiceNotesEnrichmentParse::Success);
    EXPECT_STREQ(result["title"], "Kitchen ideas");
    EXPECT_STREQ(result["summary"], "Compare storage options.");
    EXPECT_EQ(parse(completion("{\"title\":\"partial\"}"), result),
              VoiceNotesEnrichmentParse::NoteFailure);
    EXPECT_TRUE(result.isNull());
}

TEST(VoiceNotesEnrichment, RejectsInvalidContentAndCompletion) {
    const char* const invalid[] = {
        "{\"title\":\"\",\"summary\":\"ok\"}",
        "{\"title\":\" \\t\",\"summary\":\"ok\"}",
        "{\"title\":\"ok\",\"summary\":\"ok\",\"extra\":1}",
        "{\"title\":\"ok\",\"summary\":2}",
        "{\"title\":\"a\\u0000b\",\"summary\":\"ok\"}",
        "{\"title\":\"ok\",\"summary\":\"a\\u0000b\"}",
        "{\"title\":\"ok\",\"summary\":\"ok\"} trailing",
        "```json\n{\"title\":\"ok\",\"summary\":\"ok\"}\n```",
        "not json"
    };
    for (const char* text : invalid) {
        JsonDocument result;
        EXPECT_EQ(parse(completion(text), result), VoiceNotesEnrichmentParse::NoteFailure);
    }
    JsonDocument result;
    EXPECT_EQ(parse(completion("{}", "length"), result), VoiceNotesEnrichmentParse::NoteFailure);
    EXPECT_EQ(parse(completion("{}", "stop", "refused"), result), VoiceNotesEnrichmentParse::NoteFailure);
    EXPECT_EQ(parse(completion("{}", "unexpected"), result), VoiceNotesEnrichmentParse::ServiceFailure);
    EXPECT_EQ(parse("{", result), VoiceNotesEnrichmentParse::ServiceFailure);
    EXPECT_EQ(parse("{\"choices\":[]}", result), VoiceNotesEnrichmentParse::ServiceFailure);
    EXPECT_EQ(parse("{\"choices\":[]} trailing", result), VoiceNotesEnrichmentParse::ServiceFailure);
    EXPECT_EQ(parse(R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant"}}]})", result),
              VoiceNotesEnrichmentParse::ServiceFailure);
}

TEST(VoiceNotesEnrichment, AcceptsEmptySummary) {
    JsonDocument result;
    ASSERT_EQ(parse(completion("{\"title\":\"No speech detected\",\"summary\":\"\"}"), result),
              VoiceNotesEnrichmentParse::Success);
    EXPECT_STREQ(result["title"], "No speech detected");
    EXPECT_STREQ(result["summary"], "");
    EXPECT_EQ(parse(completion("{\"title\":\"No speech detected\",\"summary\":null}"), result),
              VoiceNotesEnrichmentParse::NoteFailure);
}

TEST(VoiceNotesEnrichment, ReportsAllocationFailure) {
    FailingAllocator allocator;
    JsonDocument request(&allocator);
    EXPECT_FALSE(voice_notes_enrichment_build_request(request, "deployment", "transcript"));
    JsonDocument envelope(&allocator);
    JsonDocument result;
    const std::string response = completion("{\"title\":\"ok\",\"summary\":\"ok\"}");
    EXPECT_EQ(voice_notes_enrichment_parse_response(response.data(), response.size(), envelope, result),
              VoiceNotesEnrichmentParse::ResourceFailure);
}
