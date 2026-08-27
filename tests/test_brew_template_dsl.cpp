#include <cstring>
#include <gtest/gtest.h>

#include "brew_template_dsl.h"

TEST(BrewTemplateDsl, ParsesSerializesAndRejectsOverflow) {
    const char* json = R"json({
        "v": 1,
        "name": "test_brew",
        "idle_instruction": "Tap Start to begin.",
        "done_instruction": "Brew complete.",
        "stages": [
            {"name": "Ready", "type": "manual", "target_time_s": 210},
            {"name": "Bloom", "type": "auto_time", "target_time_s": 45}
        ]
    })json";

    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    char error[80];
    ASSERT_EQ(brew_dsl_parse(json, std::strlen(json), &tmpl, &stages, error, sizeof(error)), BREW_DSL_OK);
    ASSERT_NE(tmpl, nullptr); ASSERT_NE(stages, nullptr);
    EXPECT_STREQ(tmpl->idle_instruction, "Tap Start to begin."); EXPECT_STREQ(tmpl->done_instruction, "Brew complete.");
    EXPECT_EQ(tmpl->stages[0].target_time_ms, 210000u); EXPECT_EQ(tmpl->stages[1].target_time_ms, 45000u);

    char serialized[1024];
    ASSERT_GT(brew_dsl_serialize(tmpl, serialized, sizeof(serialized)), 0);
    EXPECT_NE(std::strstr(serialized, "\"idle_instruction\""), nullptr); EXPECT_NE(std::strstr(serialized, "\"done_instruction\""), nullptr);
    EXPECT_NE(std::strstr(serialized, "\"target_time_s\":210"), nullptr); EXPECT_NE(std::strstr(serialized, "\"target_time_s\":45"), nullptr);
    EXPECT_EQ(std::strstr(serialized, "\"auto_time_s\""), nullptr);

    delete[] stages;
    delete tmpl;

    const char* oversized_time = R"json({
        "v": 1,
        "name": "bad_time",
        "stages": [{"name": "Wait", "type": "auto_time", "target_time_s": 4294968}]
    })json";
    tmpl = nullptr;
    stages = nullptr;
    EXPECT_EQ(brew_dsl_parse(oversized_time, std::strlen(oversized_time), &tmpl, &stages, error, sizeof(error)), BREW_DSL_ERR_TARGET_TIME);
    EXPECT_EQ(tmpl, nullptr); EXPECT_EQ(stages, nullptr);
}