// ============================================================================
// Integration tests for brew_manager — stage transitions and audio cue timing
// ============================================================================
// Host-compiled. Tests the brew_tick() state machine with mock time, weight,
// and audio. Catches stale-pointer bugs, guard-flag poisoning, and beep
// ordering issues across stage transitions.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

// ---- Mock globals ----
uint32_t g_mock_millis    = 0;
float    g_mock_weight    = 0.0f;
float    g_mock_flow_rate = 0.0f;

#include "audio.h"

AudioBeepCall g_beep_log[AUDIO_BEEP_LOG_MAX];
int           g_beep_log_count = 0;

void audio_beep(const char* pattern, uint8_t volume_override) {
    if (g_beep_log_count < AUDIO_BEEP_LOG_MAX) {
        auto& e = g_beep_log[g_beep_log_count++];
        if (pattern && pattern[0])
            strlcpy(e.pattern, pattern, sizeof(e.pattern));
        else
            e.pattern[0] = '\0';
        e.volume = volume_override;
    }
}

// ---- HX711 mock implementations (declarations come from real hx711_sensor.h) ----
float hx711_get_weight()    { return g_mock_weight; }
float hx711_get_flow_rate() { return g_mock_flow_rate; }
void  hx711_request_tare_no_persist() { g_mock_weight = 0.0f; }

// Stub: brew_log_save (no LittleFS on host — extern "C" to match brew_log.h)
#include "brew_manager.h"
#include "brew_templates.h"

extern "C" uint16_t brew_log_save(uint32_t, float, const BrewTemplate*, float,
                                  const BrewSample*, uint16_t,
                                  const BrewMarker*, uint8_t,
                                  const BrewCapture*, uint8_t) { return 1; }
extern "C" void brew_log_init() {}
extern "C" uint16_t brew_log_count() { return 0; }
extern "C" uint16_t brew_log_import_raw(const char*, size_t) { return 0; }

// Stub: brew_template_loader (no LittleFS on host)
void brew_template_loader_load() {}
void brew_template_loader_reload() {}

// ---- Include the real brew_manager and brew_templates via linked objects ----
// (brew_manager.cpp, brew_templates.cpp, brew_template_dsl.cpp linked at compile)

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

#define ASSERT_TRUE(expr) do {                               \
    if (!(expr)) {                                           \
        printf("FAIL\n    %s:%d: %s is false\n",            \
               __FILE__, __LINE__, #expr);                   \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_FALSE(expr) do {                              \
    if ((expr)) {                                            \
        printf("FAIL\n    %s:%d: %s is true\n",             \
               __FILE__, __LINE__, #expr);                   \
        assert(false);                                       \
    }                                                        \
} while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void reset_mocks() {
    g_mock_millis    = 0;
    g_mock_weight    = 0.0f;
    g_mock_flow_rate = 0.0f;
    g_beep_log_count = 0;
    memset(g_beep_log, 0, sizeof(g_beep_log));
}

// Check if any beep in the log contains the given pattern substring
static bool beep_log_contains(const char* pattern) {
    for (int i = 0; i < g_beep_log_count; i++) {
        if (strstr(g_beep_log[i].pattern, pattern)) return true;
    }
    return false;
}

// Count beeps matching a pattern exactly
static int beep_log_count_exact(const char* pattern) {
    int count = 0;
    for (int i = 0; i < g_beep_log_count; i++) {
        if (strcmp(g_beep_log[i].pattern, pattern) == 0) count++;
    }
    return count;
}

// Clear beep log (for mid-test checkpoints)
static void beep_log_clear() {
    g_beep_log_count = 0;
}

// ---------------------------------------------------------------------------
// Test templates — 2-stage: auto_time(10s) → manual, both with weight targets
// ---------------------------------------------------------------------------

static const BrewStage s_test_stages_2stage[] = {
    // Stage 0: auto_time, 10 seconds, target weight 50g
    {
        /* name */             "Bloom",
        /* instruction */      "Pour to 50g",
        /* next_label */       "",
        /* type */             STAGE_AUTO_TIME,
        /* on_enter */         EFFECT_BEEP | EFFECT_MARKER,
        /* on_exit */          EFFECT_CAPTURE_WEIGHT,
        /* auto_threshold */   0.0f,
        /* target_weight */    50.0f,
        /* target_flow_rate */ 0.0f,
        /* auto_time_ms */     10000,
        /* beep_pattern */     "1200:500",
        /* countdown_beep */   "800:200 800 800:200",
        /* countdown_done_beep */ "800:1000",
        /* weight_cue_g */     5.0f,
        /* weight_cue_times */ 2,
        /* weight_cue_beep */  "1500:200",
        /* weight_done_beep */ "1500:1000",
        /* capture_key */      "bloom",
        /* capture_label */    "Bloom",
        /* capture_unit */     "g",
    },
    // Stage 1: manual, target weight 150g
    {
        /* name */             "Pour",
        /* instruction */      "Pour to 150g",
        /* next_label */       "Done",
        /* type */             STAGE_MANUAL,
        /* on_enter */         EFFECT_MARKER,
        /* on_exit */          EFFECT_BEEP,
        /* auto_threshold */   0.0f,
        /* target_weight */    150.0f,
        /* target_flow_rate */ 0.0f,
        /* auto_time_ms */     0,
        /* beep_pattern */     "1200:300",
        /* countdown_beep */   "",
        /* countdown_done_beep */ "",
        /* weight_cue_g */     5.0f,
        /* weight_cue_times */ 2,
        /* weight_cue_beep */  "1500:200",
        /* weight_done_beep */ "1500:1000",
        /* capture_key */      "",
        /* capture_label */    "",
        /* capture_unit */     "",
    },
};

static const BrewTemplate s_test_template_2stage = {
    /* name */         "test_2stage",
    /* display_name */ "Test 2-Stage",
    /* description */  "Test template",
    /* start_label */  "Start",
    /* done_label */   "Again",
    /* stages */       s_test_stages_2stage,
    /* stage_count */  2,
    /* is_dynamic */   false,
};

// 3-stage template: auto_weight → auto_time(5s) → manual
static const BrewStage s_test_stages_3stage[] = {
    // Stage 0: auto_weight trigger at 3g
    {
        "Arm Pour", "Start pouring", "", STAGE_AUTO_WEIGHT,
        EFFECT_TARE, EFFECT_NONE,
        /* auto_threshold */ 3.0f,
        /* target_weight */ 0.0f, /* target_flow */ 0.0f,
        /* auto_time_ms */ 0,
        "", "", "",         // no beep patterns
        0.0f, 0, "", "",   // no weight cues
        "", "", "",         // no capture
    },
    // Stage 1: auto_time 5s, target 50g, with beeps
    {
        "Bloom", "Pour to 50g", "", STAGE_AUTO_TIME,
        EFFECT_BEEP | EFFECT_MARKER, EFFECT_NONE,
        0.0f, /* target_weight */ 50.0f, 0.0f,
        /* auto_time_ms */ 5000,
        "1200:500",                      // on_enter beep
        "800:200 800 800:200",           // countdown
        "800:1000",                      // countdown_done_beep
        5.0f, 2, "1500:200", "1500:1000", // weight cues
        "", "", "",
    },
    // Stage 2: manual, target 150g
    {
        "Pour", "Pour to 150g", "Done", STAGE_MANUAL,
        EFFECT_MARKER, EFFECT_NONE,
        0.0f, /* target_weight */ 150.0f, 0.0f,
        0, "", "", "",
        5.0f, 2, "1500:200", "1500:1000",
        "", "", "",
    },
};

static const BrewTemplate s_test_template_3stage = {
    "test_3stage", "Test 3-Stage", "Test", "Start", "Again",
    s_test_stages_3stage, 3, false,
};

// ---------------------------------------------------------------------------
// Setup: register test templates
// ---------------------------------------------------------------------------
static bool s_templates_registered = false;

static void ensure_templates() {
    if (!s_templates_registered) {
        // Initialize built-in templates first (free_pour needed as fallback)
        brew_templates_init();
        brew_template_register(&s_test_template_2stage);
        brew_template_register(&s_test_template_3stage);
        s_templates_registered = true;
    }
}

// ============================================================================
// Tests
// ============================================================================

// ---------------------------------------------------------------------------
// Test: after auto_time stage expires and transitions, the weight_done_beep
// of the OLD stage must NOT fire spuriously against the new stage.
// This is the exact bug caught in the v60_multi_pour brew report.
// ---------------------------------------------------------------------------
TEST(auto_time_transition_no_spurious_weight_done) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // Start brew with 2-stage template
    brew_start("test_2stage");
    ASSERT_TRUE(brew_is_active());
    ASSERT_STREQ(brew_get_stage_name(), "Bloom");

    // Simulate: weight at 55g (above Bloom target of 50), time just before expiry
    g_mock_weight = 55.0f;
    g_mock_millis = 100;  // stage entered at ~0
    brew_tick();

    // Weight done should fire during Bloom (weight >= 50g target)
    ASSERT_TRUE(beep_log_contains("1500:1000"));

    // Clear log, advance time past the 10s auto_time
    beep_log_clear();
    g_mock_millis = 10100;  // 10.1 seconds after stage entry
    brew_tick();

    // Should have transitioned to "Pour" stage
    ASSERT_STREQ(brew_get_stage_name(), "Pour");

    // The "1500:1000" weight_done_beep should NOT have fired again from
    // the stale Bloom stage pointer. Only countdown_done "800:1000" may fire.
    int spurious_weight_done = beep_log_count_exact("1500:1000");
    ASSERT_EQ(spurious_weight_done, 0);
}

// ---------------------------------------------------------------------------
// Test: weight_done_beep fires correctly on the NEW stage after transition
// (i.e., s_weight_done_fired was properly reset, not poisoned)
// ---------------------------------------------------------------------------
TEST(weight_done_fires_on_new_stage) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_2stage");
    g_mock_weight = 55.0f;  // above Bloom target
    g_mock_millis = 100;
    brew_tick();

    // Transition: advance past auto_time
    g_mock_millis = 10100;
    brew_tick();
    ASSERT_STREQ(brew_get_stage_name(), "Pour");

    // Clear beep log, then set weight to reach Pour's target (150g)
    beep_log_clear();
    g_mock_weight = 150.0f;
    g_mock_millis = 11000;
    brew_tick();

    // Pour's weight_done_beep "1500:1000" should fire
    ASSERT_TRUE(beep_log_contains("1500:1000"));
    ASSERT_EQ(beep_log_count_exact("1500:1000"), 1);
}

// ---------------------------------------------------------------------------
// Test: weight cues fire correctly on the new stage (not poisoned by old)
// ---------------------------------------------------------------------------
TEST(weight_cues_reset_on_new_stage) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_2stage");
    // Quickly pass through Bloom: weight above target, time expired
    g_mock_weight = 55.0f;
    g_mock_millis = 10100;
    brew_tick();
    ASSERT_STREQ(brew_get_stage_name(), "Pour");

    // Clear logs, simulate approaching Pour's target (150g)
    beep_log_clear();

    // Weight cue at 10g remaining (140g) — cue_g=5, times=2, first at 10g
    g_mock_weight = 140.0f;
    g_mock_millis = 12000;
    brew_tick();
    ASSERT_TRUE(beep_log_contains("1500:200"));

    beep_log_clear();

    // Weight cue at 5g remaining (145g)
    g_mock_weight = 145.0f;
    g_mock_millis = 13000;
    brew_tick();
    ASSERT_TRUE(beep_log_contains("1500:200"));

    beep_log_clear();

    // Weight done at target (150g)
    g_mock_weight = 150.0f;
    g_mock_millis = 14000;
    brew_tick();
    ASSERT_TRUE(beep_log_contains("1500:1000"));
}

// ---------------------------------------------------------------------------
// Test: full auto_weight → auto_time → manual flow (3-stage template)
// Verifies auto_weight triggers timer, auto_time transitions cleanly.
// ---------------------------------------------------------------------------
TEST(full_3stage_flow) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_3stage");
    ASSERT_STREQ(brew_get_stage_name(), "Arm Pour");

    // Simulate pour detection (weight > 3g threshold)
    g_mock_weight = 4.0f;
    g_mock_millis = 1000;
    brew_tick();

    // Should auto-advance past "Arm Pour" to "Bloom"
    ASSERT_STREQ(brew_get_stage_name(), "Bloom");
    ASSERT_TRUE(beep_log_contains("1200:500")); // Bloom on_enter beep

    // Pour to above target during Bloom
    beep_log_clear();
    g_mock_weight = 55.0f;
    g_mock_millis = 2000;
    brew_tick();
    ASSERT_TRUE(beep_log_contains("1500:1000")); // weight_done in Bloom

    // Expire Bloom auto_time (5s = entered at ~1000ms)
    beep_log_clear();
    g_mock_millis = 6100;
    brew_tick();
    ASSERT_STREQ(brew_get_stage_name(), "Pour");

    // No spurious weight_done from Bloom's stale pointer
    ASSERT_EQ(beep_log_count_exact("1500:1000"), 0);

    // Weight done should work on Pour (150g target)
    beep_log_clear();
    g_mock_weight = 150.0f;
    g_mock_millis = 7000;
    brew_tick();
    ASSERT_TRUE(beep_log_contains("1500:1000"));
}

// ---------------------------------------------------------------------------
// Test: countdown_done_beep fires at auto_time expiry
// ---------------------------------------------------------------------------
TEST(countdown_done_fires_at_expiry) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_2stage");
    g_mock_weight = 10.0f;  // under target, doesn't matter
    g_mock_millis = 100;
    brew_tick();

    // Advance to just past auto_time_ms (10000ms)
    beep_log_clear();
    g_mock_millis = 10100;
    brew_tick();

    // countdown_done_beep "800:1000" should have fired
    ASSERT_TRUE(beep_log_contains("800:1000"));
}

// ---------------------------------------------------------------------------
// Test: weight_done_beep is fire-once within a single stage
// ---------------------------------------------------------------------------
TEST(weight_done_fires_only_once) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_2stage");

    // Weight above target
    g_mock_weight = 55.0f;
    g_mock_millis = 100;
    brew_tick();
    ASSERT_EQ(beep_log_count_exact("1500:1000"), 1);

    // Tick again at higher weight — should NOT fire again
    g_mock_weight = 60.0f;
    g_mock_millis = 200;
    brew_tick();
    ASSERT_EQ(beep_log_count_exact("1500:1000"), 1); // still just 1
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== brew_manager integration tests ===\n");

    printf("-- Stage transition timing --\n");
    RUN(auto_time_transition_no_spurious_weight_done);
    RUN(weight_done_fires_on_new_stage);
    RUN(weight_cues_reset_on_new_stage);
    RUN(full_3stage_flow);

    printf("-- Audio cue ordering --\n");
    RUN(countdown_done_fires_at_expiry);
    RUN(weight_done_fires_only_once);

    printf("\n%d/%d tests passed\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}
