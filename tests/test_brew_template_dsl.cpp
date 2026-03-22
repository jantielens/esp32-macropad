// ============================================================================
// Unit tests for brew_template_dsl — JSON ↔ BrewTemplate parser/serializer
// ============================================================================
// Host-compiled with ArduinoJson. Uses real JSON fixture files from
// tests/fixtures/brew_templates/.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include <ArduinoJson.h>
#include "brew_template_dsl.h"

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------
static int g_tests  = 0;
static int g_passed = 0;

#define TEST(name) static void name()
#define RUN(name) do {                                       \
    g_tests++;                                               \
    printf("  %-60s ", #name);                               \
    name();                                                  \
    g_passed++;                                              \
    printf("PASS\n");                                        \
} while (0)

#define ASSERT_EQ(a, b) do {                                 \
    auto _a = (a); auto _b = (b);                            \
    if (_a != _b) {                                          \
        printf("FAIL\n    %s:%d: %s (%d) != %s (%d)\n",     \
               __FILE__, __LINE__, #a, (int)_a, #b, (int)_b);\
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_STREQ(a, b) do {                              \
    const char* _a = (a); const char* _b = (b);              \
    if (!_a || !_b || strcmp(_a, _b) != 0) {                 \
        printf("FAIL\n    %s:%d: %s (\"%s\") != %s (\"%s\")\n", \
               __FILE__, __LINE__, #a, _a?_a:"(null)",       \
               #b, _b?_b:"(null)");                          \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_FLOAT_EQ(a, b) do {                           \
    float _a = (a); float _b = (b);                          \
    float _d = (_a > _b) ? (_a - _b) : (_b - _a);           \
    if (_d > 1e-4f) {                                        \
        printf("FAIL\n    %s:%d: %s (%.4f) != %s (%.4f)\n", \
               __FILE__, __LINE__, #a, _a, #b, _b);         \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_TRUE(expr) do {                               \
    if (!(expr)) {                                           \
        printf("FAIL\n    %s:%d: %s is false\n",            \
               __FILE__, __LINE__, #expr);                   \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_NULL(ptr) do {                                \
    if ((ptr) != nullptr) {                                  \
        printf("FAIL\n    %s:%d: %s is not null\n",         \
               __FILE__, __LINE__, #ptr);                    \
        assert(false);                                       \
    }                                                        \
} while (0)

// ---------------------------------------------------------------------------
// Helper: load a fixture file into a malloc'd buffer
// ---------------------------------------------------------------------------
static char* load_fixture(const char* filename, size_t* out_len) {
    char path[256];
    snprintf(path, sizeof(path), "tests/fixtures/brew_templates/%s", filename);
    FILE* f = fopen(path, "r");
    if (!f) {
        printf("FAIL\n    Cannot open fixture: %s\n", path);
        assert(false);
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(len + 1);
    size_t rd = fread(buf, 1, len, f);
    buf[rd] = '\0';
    fclose(f);
    if (out_len) *out_len = rd;
    return buf;
}

// Helper: parse a fixture, assert success, return template (caller frees)
static void parse_fixture(const char* filename,
                          BrewTemplate** tmpl, BrewStage** stages) {
    size_t len;
    char* json = load_fixture(filename, &len);
    char err[128] = {};
    int rc = brew_dsl_parse(json, len, tmpl, stages, err, sizeof(err));
    if (rc != BREW_DSL_OK) {
        printf("FAIL\n    brew_dsl_parse(%s) returned %d: %s\n",
               filename, rc, err);
        free(json);
        assert(false);
    }
    free(json);
}

// Helper: free a parsed template
static void free_template(BrewTemplate* tmpl, BrewStage* stages) {
    delete[] stages;
    delete tmpl;
}

// ===========================================================================
// Effect mapping tests
// ===========================================================================

TEST(effect_tare)            { ASSERT_EQ(brew_dsl_parse_effect("tare"),           EFFECT_TARE); }
TEST(effect_capture_dose)    { ASSERT_EQ(brew_dsl_parse_effect("capture_dose"),   EFFECT_CAPTURE_DOSE); }
TEST(effect_beep)            { ASSERT_EQ(brew_dsl_parse_effect("beep"),           EFFECT_BEEP); }
TEST(effect_capture_weight)  { ASSERT_EQ(brew_dsl_parse_effect("capture_weight"), EFFECT_CAPTURE_WEIGHT); }
TEST(effect_marker)          { ASSERT_EQ(brew_dsl_parse_effect("marker"),         EFFECT_MARKER); }
TEST(effect_unknown)         { ASSERT_EQ(brew_dsl_parse_effect("start_pump"),     EFFECT_NONE); }
TEST(effect_empty)           { ASSERT_EQ(brew_dsl_parse_effect(""),               EFFECT_NONE); }
TEST(effect_null)            { ASSERT_EQ(brew_dsl_parse_effect(nullptr),          EFFECT_NONE); }

TEST(effect_name_tare)       { ASSERT_STREQ(brew_dsl_effect_name(EFFECT_TARE),           "tare"); }
TEST(effect_name_dose)       { ASSERT_STREQ(brew_dsl_effect_name(EFFECT_CAPTURE_DOSE),   "capture_dose"); }
TEST(effect_name_beep)       { ASSERT_STREQ(brew_dsl_effect_name(EFFECT_BEEP),           "beep"); }
TEST(effect_name_cap_w)      { ASSERT_STREQ(brew_dsl_effect_name(EFFECT_CAPTURE_WEIGHT), "capture_weight"); }
TEST(effect_name_marker)     { ASSERT_STREQ(brew_dsl_effect_name(EFFECT_MARKER),         "marker"); }
TEST(effect_name_none)       { ASSERT_NULL(brew_dsl_effect_name(EFFECT_NONE)); }
TEST(effect_name_invalid)    { ASSERT_NULL(brew_dsl_effect_name(0x80)); }

// ===========================================================================
// Stage type mapping tests
// ===========================================================================

TEST(type_manual)            { ASSERT_EQ(brew_dsl_parse_stage_type("manual"),      (int)STAGE_MANUAL); }
TEST(type_auto_weight)       { ASSERT_EQ(brew_dsl_parse_stage_type("auto_weight"), (int)STAGE_AUTO_WEIGHT); }
TEST(type_auto_time)         { ASSERT_EQ(brew_dsl_parse_stage_type("auto_time"),   (int)STAGE_AUTO_TIME); }
TEST(type_unknown)           { ASSERT_EQ(brew_dsl_parse_stage_type("quantum"),     -1); }
TEST(type_null)              { ASSERT_EQ(brew_dsl_parse_stage_type(nullptr),        -1); }

TEST(type_name_manual)       { ASSERT_STREQ(brew_dsl_stage_type_name(STAGE_MANUAL),      "manual"); }
TEST(type_name_auto_weight)  { ASSERT_STREQ(brew_dsl_stage_type_name(STAGE_AUTO_WEIGHT), "auto_weight"); }
TEST(type_name_auto_time)    { ASSERT_STREQ(brew_dsl_stage_type_name(STAGE_AUTO_TIME),   "auto_time"); }
TEST(type_name_invalid)      { ASSERT_NULL(brew_dsl_stage_type_name((BrewStageType)99)); }

// ===========================================================================
// Parse: minimal template (only required fields)
// ===========================================================================

TEST(parse_minimal) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("minimal.json", &tmpl, &stages);

    ASSERT_STREQ(tmpl->name, "minimal");
    ASSERT_STREQ(tmpl->display_name, "Minimal Test");
    ASSERT_STREQ(tmpl->description, "");
    ASSERT_STREQ(tmpl->start_label, "");
    ASSERT_STREQ(tmpl->done_label, "");
    ASSERT_EQ(tmpl->stage_count, 1);
    ASSERT_TRUE(tmpl->is_dynamic);

    const BrewStage& s = stages[0];
    ASSERT_STREQ(s.name, "Brew");
    ASSERT_STREQ(s.instruction, "Tap Done when finished");
    ASSERT_STREQ(s.next_label, "Done");
    ASSERT_EQ((int)s.type, (int)STAGE_MANUAL);
    ASSERT_EQ(s.on_enter, EFFECT_NONE);
    ASSERT_EQ(s.on_exit, EFFECT_NONE);
    ASSERT_FLOAT_EQ(s.auto_threshold, 0.0f);
    ASSERT_FLOAT_EQ(s.target_weight, 0.0f);
    ASSERT_FLOAT_EQ(s.target_flow_rate, 0.0f);
    ASSERT_EQ(s.auto_time_ms, (uint32_t)0);
    ASSERT_STREQ(s.capture_key, "");
    ASSERT_STREQ(s.capture_label, "");
    ASSERT_STREQ(s.capture_unit, "");

    free_template(tmpl, stages);
}

// ===========================================================================
// Parse: free_pour (auto_weight + tare effect)
// ===========================================================================

TEST(parse_free_pour) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("free_pour.json", &tmpl, &stages);

    ASSERT_STREQ(tmpl->name, "free_pour");
    ASSERT_STREQ(tmpl->display_name, "Free Pour");
    ASSERT_STREQ(tmpl->start_label, "Start brew");
    ASSERT_STREQ(tmpl->done_label, "Start brew again");
    ASSERT_EQ(tmpl->stage_count, 2);

    // Stage 0: Ready
    ASSERT_STREQ(stages[0].name, "Ready");
    ASSERT_EQ((int)stages[0].type, (int)STAGE_AUTO_WEIGHT);
    ASSERT_EQ(stages[0].on_enter, EFFECT_TARE);
    ASSERT_EQ(stages[0].on_exit, EFFECT_NONE);
    ASSERT_FLOAT_EQ(stages[0].auto_threshold, 2.0f);

    // Stage 1: Brewing
    ASSERT_STREQ(stages[1].name, "Brewing");
    ASSERT_EQ((int)stages[1].type, (int)STAGE_MANUAL);
    ASSERT_EQ(stages[1].on_enter, EFFECT_NONE);

    free_template(tmpl, stages);
}

// ===========================================================================
// Parse: rao_v60 (full-featured: all stage types, effects, captures, targets)
// ===========================================================================

TEST(parse_rao_v60) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("rao_v60.json", &tmpl, &stages);

    ASSERT_STREQ(tmpl->name, "rao_v60");
    ASSERT_STREQ(tmpl->display_name, "James Rao V60");
    ASSERT_STREQ(tmpl->description, "Single-pour V60 with bloom stage and 5:00 target");
    ASSERT_STREQ(tmpl->start_label, "Start Rao V60");
    ASSERT_STREQ(tmpl->done_label, "Brew again");
    ASSERT_EQ(tmpl->stage_count, 6);

    // Stage 0: Place cup — plain manual
    ASSERT_STREQ(stages[0].name, "Place cup");
    ASSERT_EQ((int)stages[0].type, (int)STAGE_MANUAL);
    ASSERT_EQ(stages[0].on_enter, EFFECT_NONE);
    ASSERT_EQ(stages[0].on_exit, EFFECT_NONE);

    // Stage 1: Dose beans — tare on enter, capture_dose on exit, target 16g
    ASSERT_STREQ(stages[1].name, "Dose beans");
    ASSERT_EQ(stages[1].on_enter, EFFECT_TARE);
    ASSERT_EQ(stages[1].on_exit, EFFECT_CAPTURE_DOSE);
    ASSERT_FLOAT_EQ(stages[1].target_weight, 16.0f);

    // Stage 2: Prep — tare on enter
    ASSERT_STREQ(stages[2].name, "Prep");
    ASSERT_EQ(stages[2].on_enter, EFFECT_TARE);

    // Stage 3: Arm pour — auto_weight, tare, threshold 2g
    ASSERT_STREQ(stages[3].name, "Arm pour");
    ASSERT_EQ((int)stages[3].type, (int)STAGE_AUTO_WEIGHT);
    ASSERT_EQ(stages[3].on_enter, EFFECT_TARE);
    ASSERT_FLOAT_EQ(stages[3].auto_threshold, 2.0f);

    // Stage 4: Bloom — auto_time 45s, beep enter, capture_weight exit,
    //          targets (weight 60, flow 6), capture bloom_water
    ASSERT_STREQ(stages[4].name, "Bloom");
    ASSERT_EQ((int)stages[4].type, (int)STAGE_AUTO_TIME);
    ASSERT_EQ(stages[4].on_enter, EFFECT_BEEP);
    ASSERT_EQ(stages[4].on_exit, EFFECT_CAPTURE_WEIGHT);
    ASSERT_EQ(stages[4].auto_time_ms, (uint32_t)45000);
    ASSERT_FLOAT_EQ(stages[4].target_weight, 60.0f);
    ASSERT_FLOAT_EQ(stages[4].target_flow_rate, 6.0f);
    ASSERT_STREQ(stages[4].capture_key, "bloom_water");
    ASSERT_STREQ(stages[4].capture_label, "Bloom Water");
    ASSERT_STREQ(stages[4].capture_unit, "g");

    // Stage 5: Main pour — manual, beep, target 250g, flow 5g/s
    ASSERT_STREQ(stages[5].name, "Main pour");
    ASSERT_EQ((int)stages[5].type, (int)STAGE_MANUAL);
    ASSERT_EQ(stages[5].on_enter, EFFECT_BEEP);
    ASSERT_FLOAT_EQ(stages[5].target_weight, 250.0f);
    ASSERT_FLOAT_EQ(stages[5].target_flow_rate, 5.0f);

    free_template(tmpl, stages);
}

// ===========================================================================
// Parse: all effects combined (bitmask OR)
// ===========================================================================

TEST(parse_all_effects) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("all_effects.json", &tmpl, &stages);

    ASSERT_EQ(stages[0].on_enter, (BrewEffects)(EFFECT_TARE | EFFECT_BEEP | EFFECT_MARKER));
    ASSERT_EQ(stages[0].on_exit,  (BrewEffects)(EFFECT_CAPTURE_DOSE | EFFECT_CAPTURE_WEIGHT | EFFECT_BEEP));
    ASSERT_STREQ(stages[0].capture_key, "test_cap");
    ASSERT_STREQ(stages[0].capture_label, "Test Capture");
    ASSERT_STREQ(stages[0].capture_unit, "ml");
    ASSERT_STREQ(stages[0].beep_pattern, "600:40 40 600:40");

    free_template(tmpl, stages);
}

// ===========================================================================
// Parse: beep_pattern (custom audio cue per stage)
// ===========================================================================

TEST(parse_beep_pattern) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("beep_pattern.json", &tmpl, &stages);

    ASSERT_EQ(tmpl->stage_count, (uint8_t)3);

    // Stage 0: beep effect but no custom pattern → empty (uses default)
    ASSERT_EQ(stages[0].on_enter, EFFECT_BEEP);
    ASSERT_STREQ(stages[0].beep_pattern, "");

    // Stage 1: beep effect with custom pattern
    ASSERT_EQ(stages[1].on_enter, EFFECT_BEEP);
    ASSERT_STREQ(stages[1].beep_pattern, "1000:30 30 1200:30");

    // Stage 2: pattern set but no beep effect (pattern stored, ignored at runtime)
    ASSERT_EQ(stages[2].on_enter, EFFECT_NONE);
    ASSERT_STREQ(stages[2].beep_pattern, "800:100");

    free_template(tmpl, stages);
}

// ===========================================================================
// Parse: countdown_beep field
// ===========================================================================

TEST(parse_countdown_beep) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("countdown.json", &tmpl, &stages);

    ASSERT_EQ(tmpl->stage_count, (uint8_t)3);

    // Stage 0: auto_time with countdown_beep
    ASSERT_EQ((int)stages[0].type, (int)STAGE_AUTO_TIME);
    ASSERT_EQ(stages[0].auto_time_ms, (uint32_t)45000);
    ASSERT_STREQ(stages[0].countdown_beep, "800:200 300 800:200 300 800:500");
    ASSERT_STREQ(stages[0].countdown_done_beep, "800:1000");

    // Stage 1: auto_time without countdown_beep
    ASSERT_EQ((int)stages[1].type, (int)STAGE_AUTO_TIME);
    ASSERT_STREQ(stages[1].countdown_beep, "");
    ASSERT_STREQ(stages[1].countdown_done_beep, "");

    // Stage 2: manual with countdown_beep (stored but ignored at runtime)
    ASSERT_EQ((int)stages[2].type, (int)STAGE_MANUAL);
    ASSERT_STREQ(stages[2].countdown_beep, "1000:100");
    ASSERT_STREQ(stages[2].countdown_done_beep, "1000:500");

    free_template(tmpl, stages);
}

TEST(parse_weight_cue) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("weight_cue.json", &tmpl, &stages);

    ASSERT_EQ(tmpl->stage_count, (uint8_t)3);

    // Stage 0: full weight cue with times and beep pattern
    ASSERT_FLOAT_EQ(stages[0].weight_cue_g, 10.0f);
    ASSERT_EQ(stages[0].weight_cue_times, (uint8_t)3);
    ASSERT_STREQ(stages[0].weight_cue_beep, "800:100 100 800:100");
    ASSERT_FLOAT_EQ(stages[0].target_weight, 200.0f);
    ASSERT_STREQ(stages[0].weight_done_beep, "1500:1000");

    // Stage 1: simple cue (times defaults to 1, no custom beep)
    ASSERT_FLOAT_EQ(stages[1].weight_cue_g, 15.0f);
    ASSERT_EQ(stages[1].weight_cue_times, (uint8_t)1);
    ASSERT_STREQ(stages[1].weight_cue_beep, "");
    ASSERT_STREQ(stages[1].weight_done_beep, "");

    // Stage 2: cue without target weight (stored but ignored at runtime)
    ASSERT_FLOAT_EQ(stages[2].weight_cue_g, 10.0f);
    ASSERT_FLOAT_EQ(stages[2].target_weight, 0.0f);
    ASSERT_STREQ(stages[2].weight_cue_beep, "600:200");
    ASSERT_STREQ(stages[2].weight_done_beep, "");

    free_template(tmpl, stages);
}

// ===========================================================================
// Beep duration calculator
// ===========================================================================

TEST(beep_duration_null) {
    ASSERT_EQ(brew_dsl_beep_duration_ms(nullptr), (uint32_t)0);
}

TEST(beep_duration_empty) {
    ASSERT_EQ(brew_dsl_beep_duration_ms(""), (uint32_t)0);
}

TEST(beep_duration_single_tone) {
    ASSERT_EQ(brew_dsl_beep_duration_ms("1000:200"), (uint32_t)200);
}

TEST(beep_duration_single_gap) {
    ASSERT_EQ(brew_dsl_beep_duration_ms("150"), (uint32_t)150);
}

TEST(beep_duration_tone_gap_tone) {
    // 200 + 100 + 200 = 500
    ASSERT_EQ(brew_dsl_beep_duration_ms("1000:200 100 1000:200"), (uint32_t)500);
}

TEST(beep_duration_countdown_pattern) {
    // 200 + 300 + 200 + 300 + 500 = 1500
    ASSERT_EQ(brew_dsl_beep_duration_ms("800:200 300 800:200 300 800:500"), (uint32_t)1500);
}

TEST(beep_duration_rising_alert) {
    // 100 + 50 + 100 + 50 + 100 = 400
    ASSERT_EQ(brew_dsl_beep_duration_ms("800:100 50 1000:100 50 1200:100"), (uint32_t)400);
}

TEST(beep_duration_ignores_zero) {
    // "0" → 0 duration (not in range 1..10000)
    ASSERT_EQ(brew_dsl_beep_duration_ms("0:0 0"), (uint32_t)0);
}

// ===========================================================================
// Parse: default values (omitted optional fields → zero/empty)
// ===========================================================================

TEST(parse_defaults) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("defaults_test.json", &tmpl, &stages);

    // Stage 0: only required fields
    ASSERT_EQ(stages[0].on_enter, EFFECT_NONE);
    ASSERT_EQ(stages[0].on_exit, EFFECT_NONE);
    ASSERT_FLOAT_EQ(stages[0].auto_threshold, 0.0f);
    ASSERT_FLOAT_EQ(stages[0].target_weight, 0.0f);
    ASSERT_FLOAT_EQ(stages[0].target_flow_rate, 0.0f);
    ASSERT_EQ(stages[0].auto_time_ms, (uint32_t)0);
    ASSERT_STREQ(stages[0].capture_key, "");
    ASSERT_STREQ(stages[0].capture_label, "");
    ASSERT_STREQ(stages[0].capture_unit, "");
    ASSERT_STREQ(stages[0].beep_pattern, "");
    ASSERT_STREQ(stages[0].countdown_beep, "");
    ASSERT_STREQ(stages[0].countdown_done_beep, "");
    ASSERT_FLOAT_EQ(stages[0].weight_cue_g, 0.0f);
    ASSERT_EQ(stages[0].weight_cue_times, (uint8_t)1);
    ASSERT_STREQ(stages[0].weight_cue_beep, "");
    ASSERT_STREQ(stages[0].weight_done_beep, "");

    // Stage 1: auto_weight with threshold
    ASSERT_EQ((int)stages[1].type, (int)STAGE_AUTO_WEIGHT);
    ASSERT_FLOAT_EQ(stages[1].auto_threshold, 3.5f);

    // Stage 2: auto_time_s → auto_time_ms conversion
    ASSERT_EQ((int)stages[2].type, (int)STAGE_AUTO_TIME);
    ASSERT_EQ(stages[2].auto_time_ms, (uint32_t)30000);

    // Template-level defaults
    ASSERT_STREQ(tmpl->description, "");
    ASSERT_STREQ(tmpl->start_label, "");
    ASSERT_STREQ(tmpl->done_label, "");

    free_template(tmpl, stages);
}

// ===========================================================================
// Forward compatibility: unknown top-level & stage fields silently ignored,
// unknown effects map to EFFECT_NONE but don't block parsing
// ===========================================================================

TEST(parse_forward_compat) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("forward_compat.json", &tmpl, &stages);

    ASSERT_STREQ(tmpl->name, "future_fields");
    ASSERT_EQ(tmpl->stage_count, 1);

    // "tare" is known, "start_pump" and "future_effect" are not → only tare applied
    ASSERT_EQ(stages[0].on_enter, EFFECT_TARE);

    free_template(tmpl, stages);
}

// ===========================================================================
// Truncation: overlong strings are safely truncated, no overflow
// ===========================================================================

TEST(parse_truncation) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("truncation.json", &tmpl, &stages);

    // name[24] — truncated to 23 chars + null
    ASSERT_TRUE(strlen(tmpl->name) <= 23);
    ASSERT_TRUE(strlen(tmpl->name) > 0);

    // display_name[48] — truncated to 47 chars + null
    ASSERT_TRUE(strlen(tmpl->display_name) <= 47);
    ASSERT_TRUE(strlen(tmpl->display_name) > 0);

    // description[128] — truncated to 127 chars + null
    ASSERT_TRUE(strlen(tmpl->description) <= 127);

    // start_label[48] — truncated
    ASSERT_TRUE(strlen(tmpl->start_label) <= 47);

    // done_label[48] — truncated
    ASSERT_TRUE(strlen(tmpl->done_label) <= 47);

    // Stage fields
    ASSERT_TRUE(strlen(stages[0].name) <= 23);
    ASSERT_TRUE(strlen(stages[0].instruction) <= 127);
    ASSERT_TRUE(strlen(stages[0].next_label) <= 47);
    ASSERT_TRUE(strlen(stages[0].capture_key) <= 15);
    ASSERT_TRUE(strlen(stages[0].capture_label) <= 23);
    ASSERT_TRUE(strlen(stages[0].capture_unit) <= 7);

    free_template(tmpl, stages);
}

// ===========================================================================
// Error cases
// ===========================================================================

TEST(err_invalid_json) {
    size_t len;
    char* json = load_fixture("err_invalid_json.json", &len);
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    char err[128] = {};

    int rc = brew_dsl_parse(json, len, &tmpl, &stages, err, sizeof(err));
    ASSERT_EQ(rc, BREW_DSL_ERR_JSON);
    ASSERT_NULL(tmpl);
    ASSERT_NULL(stages);
    ASSERT_TRUE(strlen(err) > 0);

    free(json);
}

TEST(err_bad_version) {
    size_t len;
    char* json = load_fixture("err_bad_version.json", &len);
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    char err[128] = {};

    int rc = brew_dsl_parse(json, len, &tmpl, &stages, err, sizeof(err));
    ASSERT_EQ(rc, BREW_DSL_ERR_VERSION);
    ASSERT_NULL(tmpl);

    free(json);
}

TEST(err_missing_name) {
    size_t len;
    char* json = load_fixture("err_missing_name.json", &len);
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    char err[128] = {};

    int rc = brew_dsl_parse(json, len, &tmpl, &stages, err, sizeof(err));
    ASSERT_EQ(rc, BREW_DSL_ERR_MISSING_NAME);
    ASSERT_NULL(tmpl);

    free(json);
}

TEST(err_no_stages) {
    size_t len;
    char* json = load_fixture("err_no_stages.json", &len);
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;

    int rc = brew_dsl_parse(json, len, &tmpl, &stages);
    ASSERT_EQ(rc, BREW_DSL_ERR_NO_STAGES);
    ASSERT_NULL(tmpl);

    free(json);
}

TEST(err_empty_stages) {
    size_t len;
    char* json = load_fixture("err_empty_stages.json", &len);
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;

    int rc = brew_dsl_parse(json, len, &tmpl, &stages);
    ASSERT_EQ(rc, BREW_DSL_ERR_NO_STAGES);
    ASSERT_NULL(tmpl);

    free(json);
}

TEST(err_bad_stage_type) {
    size_t len;
    char* json = load_fixture("err_bad_stage_type.json", &len);
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    char err[128] = {};

    int rc = brew_dsl_parse(json, len, &tmpl, &stages, err, sizeof(err));
    ASSERT_EQ(rc, BREW_DSL_ERR_STAGE_TYPE);
    ASSERT_NULL(tmpl);
    ASSERT_TRUE(strlen(err) > 0);

    free(json);
}

TEST(err_missing_stage_name) {
    size_t len;
    char* json = load_fixture("err_missing_stage_name.json", &len);
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    char err[128] = {};

    int rc = brew_dsl_parse(json, len, &tmpl, &stages, err, sizeof(err));
    ASSERT_EQ(rc, BREW_DSL_ERR_STAGE_NAME);
    ASSERT_NULL(tmpl);

    free(json);
}

TEST(err_null_json) {
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    int rc = brew_dsl_parse(nullptr, 0, &tmpl, &stages);
    ASSERT_EQ(rc, BREW_DSL_ERR_JSON);
    ASSERT_NULL(tmpl);
}

TEST(err_empty_json) {
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    int rc = brew_dsl_parse("", 0, &tmpl, &stages);
    ASSERT_EQ(rc, BREW_DSL_ERR_JSON);
    ASSERT_NULL(tmpl);
}

// ===========================================================================
// Serialization round-trip: parse → serialize → parse → compare
// ===========================================================================

TEST(roundtrip_rao_v60) {
    BrewTemplate* tmpl1; BrewStage* stages1;
    parse_fixture("rao_v60.json", &tmpl1, &stages1);

    // Serialize
    char buf[4096];
    int len = brew_dsl_serialize(tmpl1, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    // Parse the serialized output
    BrewTemplate* tmpl2 = nullptr;
    BrewStage* stages2 = nullptr;
    char err[128] = {};
    int rc = brew_dsl_parse(buf, (size_t)len, &tmpl2, &stages2, err, sizeof(err));
    ASSERT_EQ(rc, BREW_DSL_OK);

    // Compare template-level fields
    ASSERT_STREQ(tmpl1->name, tmpl2->name);
    ASSERT_STREQ(tmpl1->display_name, tmpl2->display_name);
    ASSERT_STREQ(tmpl1->description, tmpl2->description);
    ASSERT_STREQ(tmpl1->start_label, tmpl2->start_label);
    ASSERT_STREQ(tmpl1->done_label, tmpl2->done_label);
    ASSERT_EQ(tmpl1->stage_count, tmpl2->stage_count);

    // Compare every stage field
    for (int i = 0; i < tmpl1->stage_count; i++) {
        const BrewStage& a = stages1[i];
        const BrewStage& b = stages2[i];
        ASSERT_STREQ(a.name, b.name);
        ASSERT_STREQ(a.instruction, b.instruction);
        ASSERT_STREQ(a.next_label, b.next_label);
        ASSERT_EQ((int)a.type, (int)b.type);
        ASSERT_EQ(a.on_enter, b.on_enter);
        ASSERT_EQ(a.on_exit, b.on_exit);
        ASSERT_FLOAT_EQ(a.auto_threshold, b.auto_threshold);
        ASSERT_FLOAT_EQ(a.target_weight, b.target_weight);
        ASSERT_FLOAT_EQ(a.target_flow_rate, b.target_flow_rate);
        ASSERT_EQ(a.auto_time_ms, b.auto_time_ms);
        ASSERT_STREQ(a.capture_key, b.capture_key);
        ASSERT_STREQ(a.capture_label, b.capture_label);
        ASSERT_STREQ(a.capture_unit, b.capture_unit);
        ASSERT_STREQ(a.beep_pattern, b.beep_pattern);
        ASSERT_STREQ(a.countdown_beep, b.countdown_beep);
        ASSERT_STREQ(a.countdown_done_beep, b.countdown_done_beep);
        ASSERT_FLOAT_EQ(a.weight_cue_g, b.weight_cue_g);
        ASSERT_EQ(a.weight_cue_times, b.weight_cue_times);
        ASSERT_STREQ(a.weight_cue_beep, b.weight_cue_beep);
        ASSERT_STREQ(a.weight_done_beep, b.weight_done_beep);
    }

    free_template(tmpl1, stages1);
    free_template(tmpl2, stages2);
}

TEST(roundtrip_minimal) {
    BrewTemplate* tmpl1; BrewStage* stages1;
    parse_fixture("minimal.json", &tmpl1, &stages1);

    char buf[2048];
    int len = brew_dsl_serialize(tmpl1, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    BrewTemplate* tmpl2; BrewStage* stages2;
    int rc = brew_dsl_parse(buf, (size_t)len, &tmpl2, &stages2);
    ASSERT_EQ(rc, BREW_DSL_OK);

    ASSERT_STREQ(tmpl1->name, tmpl2->name);
    ASSERT_EQ(tmpl1->stage_count, tmpl2->stage_count);
    ASSERT_STREQ(stages1[0].name, stages2[0].name);
    ASSERT_EQ((int)stages1[0].type, (int)stages2[0].type);

    free_template(tmpl1, stages1);
    free_template(tmpl2, stages2);
}

// ===========================================================================
// Serialize: buffer too small
// ===========================================================================

TEST(serialize_buffer_too_small) {
    BrewTemplate* tmpl; BrewStage* stages;
    parse_fixture("rao_v60.json", &tmpl, &stages);

    char buf[32];  // way too small for rao_v60
    int len = brew_dsl_serialize(tmpl, buf, sizeof(buf));
    ASSERT_EQ(len, -1);

    free_template(tmpl, stages);
}

// ===========================================================================
// main
// ===========================================================================

int main() {
    printf("=== brew_template_dsl unit tests ===\n");

    printf("\n-- Effect mapping --\n");
    RUN(effect_tare);
    RUN(effect_capture_dose);
    RUN(effect_beep);
    RUN(effect_capture_weight);
    RUN(effect_marker);
    RUN(effect_unknown);
    RUN(effect_empty);
    RUN(effect_null);
    RUN(effect_name_tare);
    RUN(effect_name_dose);
    RUN(effect_name_beep);
    RUN(effect_name_cap_w);
    RUN(effect_name_marker);
    RUN(effect_name_none);
    RUN(effect_name_invalid);

    printf("\n-- Stage type mapping --\n");
    RUN(type_manual);
    RUN(type_auto_weight);
    RUN(type_auto_time);
    RUN(type_unknown);
    RUN(type_null);
    RUN(type_name_manual);
    RUN(type_name_auto_weight);
    RUN(type_name_auto_time);
    RUN(type_name_invalid);

    printf("\n-- Parse: valid templates --\n");
    RUN(parse_minimal);
    RUN(parse_free_pour);
    RUN(parse_rao_v60);
    RUN(parse_all_effects);
    RUN(parse_beep_pattern);
    RUN(parse_countdown_beep);
    RUN(parse_weight_cue);
    RUN(parse_defaults);
    RUN(parse_forward_compat);
    RUN(parse_truncation);

    printf("\n-- Parse: error cases --\n");
    RUN(err_invalid_json);
    RUN(err_bad_version);
    RUN(err_missing_name);
    RUN(err_no_stages);
    RUN(err_empty_stages);
    RUN(err_bad_stage_type);
    RUN(err_missing_stage_name);
    RUN(err_null_json);
    RUN(err_empty_json);

    printf("\n-- Serialization round-trip --\n");
    RUN(roundtrip_rao_v60);
    RUN(roundtrip_minimal);
    RUN(serialize_buffer_too_small);

    printf("\n-- Beep duration calculator --\n");
    RUN(beep_duration_null);
    RUN(beep_duration_empty);
    RUN(beep_duration_single_tone);
    RUN(beep_duration_single_gap);
    RUN(beep_duration_tone_gap_tone);
    RUN(beep_duration_countdown_pattern);
    RUN(beep_duration_rising_alert);
    RUN(beep_duration_ignores_zero);

    printf("\n%d/%d tests passed\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}
