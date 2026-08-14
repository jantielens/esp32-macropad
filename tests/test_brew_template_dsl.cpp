#include <cassert>
#include <cstdio>
#include <cstring>

#include "brew_template_dsl.h"

int main() {
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
    assert(brew_dsl_parse(json, std::strlen(json), &tmpl, &stages,
                          error, sizeof(error)) == BREW_DSL_OK);
    assert(std::strcmp(tmpl->idle_instruction, "Tap Start to begin.") == 0);
    assert(std::strcmp(tmpl->done_instruction, "Brew complete.") == 0);
    assert(tmpl->stages[0].target_time_ms == 210000);
    assert(tmpl->stages[1].target_time_ms == 45000);

    char serialized[1024];
    assert(brew_dsl_serialize(tmpl, serialized, sizeof(serialized)) > 0);
    assert(std::strstr(serialized, "\"idle_instruction\"") != nullptr);
    assert(std::strstr(serialized, "\"done_instruction\"") != nullptr);
    assert(std::strstr(serialized, "\"target_time_s\":210") != nullptr);
    assert(std::strstr(serialized, "\"target_time_s\":45") != nullptr);
    assert(std::strstr(serialized, "\"auto_time_s\"") == nullptr);

    delete[] stages;
    delete tmpl;

    const char* oversized_time = R"json({
        "v": 1,
        "name": "bad_time",
        "stages": [{"name": "Wait", "type": "auto_time", "target_time_s": 4294968}]
    })json";
    tmpl = nullptr;
    stages = nullptr;
    assert(brew_dsl_parse(oversized_time, std::strlen(oversized_time), &tmpl, &stages,
                          error, sizeof(error)) == BREW_DSL_ERR_TARGET_TIME);
    assert(tmpl == nullptr);
    assert(stages == nullptr);

    std::puts("brew_template_dsl: PASS");
    return 0;
}