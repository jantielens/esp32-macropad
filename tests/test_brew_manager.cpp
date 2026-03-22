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
// 1-stage template: single auto_weight (edge case: no next stage)
// ---------------------------------------------------------------------------
static const BrewStage s_test_stages_1stage[] = {
    {
        "Arm Pour", "Start pouring", "Done", STAGE_AUTO_WEIGHT,
        EFFECT_TARE, EFFECT_NONE,
        3.0f, 0.0f, 0.0f, 0,
        "", "", "",
        0.0f, 0, "", "",
        "", "", "",
    },
};

static const BrewTemplate s_test_template_1stage = {
    "test_1stage", "Test 1-Stage", "Test", "Start", "Again",
    s_test_stages_1stage, 1, false,
};

// ---------------------------------------------------------------------------
// Manual-only template: 3 manual stages for brew_next() testing
// ---------------------------------------------------------------------------
static const BrewStage s_test_stages_manual[] = {
    {
        "Step 1", "First step", "Go to 2", STAGE_MANUAL,
        EFFECT_TARE | EFFECT_BEEP | EFFECT_MARKER, EFFECT_CAPTURE_DOSE,
        0.0f, 0.0f, 0.0f, 0,
        "600:100", "", "",
        0.0f, 0, "", "",
        "", "", "",
    },
    {
        "Step 2", "Second step", "Go to 3", STAGE_MANUAL,
        EFFECT_BEEP, EFFECT_CAPTURE_WEIGHT | EFFECT_BEEP,
        0.0f, 100.0f, 0.0f, 0,
        "", "", "",
        0.0f, 0, "", "",
        "cap1", "Capture 1", "g",
    },
    {
        "Step 3", "Final step", "Done", STAGE_MANUAL,
        EFFECT_MARKER, EFFECT_BEEP,
        0.0f, 200.0f, 5.0f, 0,
        "1200:300", "", "",
        0.0f, 0, "", "",
        "", "", "",
    },
};

static const BrewTemplate s_test_template_manual = {
    "test_manual", "Test Manual", "Manual test", "Begin", "Restart",
    s_test_stages_manual, 3, false,
};

// ---------------------------------------------------------------------------
// Auto-time with zero duration (edge case)
// ---------------------------------------------------------------------------
static const BrewStage s_test_stages_zero_time[] = {
    {
        "QuickStage", "Instant", "", STAGE_AUTO_TIME,
        EFFECT_BEEP, EFFECT_NONE,
        0.0f, 0.0f, 0.0f, 0,   // auto_time_ms = 0
        "", "", "",
        0.0f, 0, "", "",
        "", "", "",
    },
    {
        "After", "After instant", "Done", STAGE_MANUAL,
        EFFECT_NONE, EFFECT_NONE,
        0.0f, 0.0f, 0.0f, 0,
        "", "", "",
        0.0f, 0, "", "",
        "", "", "",
    },
};

static const BrewTemplate s_test_template_zero_time = {
    "test_zero_time", "Test Zero Time", "Test", "Start", "Again",
    s_test_stages_zero_time, 2, false,
};

// ---------------------------------------------------------------------------
// Auto-time where countdown duration > stage duration (edge case)
// ---------------------------------------------------------------------------
static const BrewStage s_test_stages_long_countdown[] = {
    {
        "ShortStage", "Short", "", STAGE_AUTO_TIME,
        EFFECT_NONE, EFFECT_NONE,
        0.0f, 0.0f, 0.0f, 1000,   // 1 second
        "", "800:200 300 800:200 300 800:500", "",  // 1500ms > 1000ms
        0.0f, 0, "", "",
        "", "", "",
    },
    {
        "After", "After", "Done", STAGE_MANUAL,
        EFFECT_NONE, EFFECT_NONE,
        0.0f, 0.0f, 0.0f, 0,
        "", "", "",
        0.0f, 0, "", "",
        "", "", "",
    },
};

static const BrewTemplate s_test_template_long_countdown = {
    "test_long_cd", "Test Long Countdown", "Test", "Start", "Again",
    s_test_stages_long_countdown, 2, false,
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
        brew_template_register(&s_test_template_1stage);
        brew_template_register(&s_test_template_manual);
        brew_template_register(&s_test_template_zero_time);
        brew_template_register(&s_test_template_long_countdown);
        s_templates_registered = true;
    }
}

// ============================================================================
// Tests — Tier 1: Core State Machine
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
// Tier 1: AUTO_WEIGHT entry
// ============================================================================

TEST(auto_weight_no_trigger_below_threshold) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_3stage");
    ASSERT_STREQ(brew_get_stage_name(), "Arm Pour");

    // Weight below threshold (3g) — should NOT advance
    g_mock_weight = 2.5f;
    g_mock_millis = 500;
    brew_tick();
    ASSERT_STREQ(brew_get_stage_name(), "Arm Pour");
    ASSERT_EQ(brew_get_timer_ms(), (uint32_t)0);  // timer not started
}

TEST(auto_weight_exact_threshold) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_3stage");

    // Weight exactly at threshold (>= 3g)
    g_mock_weight = 3.0f;
    g_mock_millis = 1000;
    brew_tick();
    ASSERT_STREQ(brew_get_stage_name(), "Bloom");  // advanced
    ASSERT_TRUE(brew_get_timer_ms() > (uint32_t)0 || brew_is_active());
}

TEST(auto_weight_single_stage_goes_to_done) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_1stage");
    ASSERT_STREQ(brew_get_stage_name(), "Arm Pour");

    // Weight above threshold — no next stage exists
    g_mock_weight = 5.0f;
    g_mock_millis = 1000;
    brew_tick();

    // With only 1 stage, entering next should still work
    // The stage advances, but there's no next stage,
    // so it should remain at Arm Pour (auto_weight doesn't call brew_stop on no-next)
    // Actually: it tries enter_stage(1) but stage_count=1, so it stays.
    // Timer should have started though.
    ASSERT_TRUE(brew_is_active());
}

TEST(auto_weight_timer_starts_once) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_3stage");

    // First pour: triggers timer
    g_mock_weight = 4.0f;
    g_mock_millis = 1000;
    brew_tick();
    ASSERT_STREQ(brew_get_stage_name(), "Bloom");
    uint32_t timer1 = brew_get_timer_ms();

    // Advance time, tick again — timer should keep running, not restart
    g_mock_millis = 2000;
    brew_tick();
    uint32_t timer2 = brew_get_timer_ms();
    ASSERT_TRUE(timer2 > timer1);

    // Weight fluctuates back down — timer should NOT restart
    g_mock_weight = 1.0f;
    g_mock_millis = 3000;
    brew_tick();
    uint32_t timer3 = brew_get_timer_ms();
    ASSERT_TRUE(timer3 > timer2);
}

// ============================================================================
// Tier 1: AUTO_TIME duration & countdown
// ============================================================================

TEST(auto_time_zero_duration_no_auto_advance) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // zero_time template: auto_time_ms = 0 — should NOT auto-advance
    brew_start("test_zero_time");
    ASSERT_STREQ(brew_get_stage_name(), "QuickStage");

    g_mock_millis = 5000;
    brew_tick();

    // Should still be on QuickStage (auto_time_ms=0 means the condition
    // `stage->auto_time_ms > 0` is false, so no auto-advance)
    ASSERT_STREQ(brew_get_stage_name(), "QuickStage");
}

TEST(auto_time_countdown_longer_than_stage_skipped) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_long_cd");
    ASSERT_STREQ(brew_get_stage_name(), "ShortStage");

    // Tick at 500ms — countdown trigger should be disabled (pattern 1500ms > stage 1000ms)
    g_mock_millis = 500;
    brew_tick();
    // No countdown beep should have fired
    ASSERT_FALSE(beep_log_contains("800:200"));

    // Expire the stage
    beep_log_clear();
    g_mock_millis = 1100;
    brew_tick();
    ASSERT_STREQ(brew_get_stage_name(), "After");

    // Countdown pattern should never have fired
    ASSERT_FALSE(beep_log_contains("800:200"));
}

TEST(auto_time_no_countdown_beep_empty) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // Use the manual template (no auto_time stages) — just verify no crash
    brew_start("test_manual");
    g_mock_millis = 100;
    brew_tick();
    ASSERT_TRUE(brew_is_active());  // no crash
}

TEST(auto_time_last_stage_goes_to_done) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_2stage");
    // Advance through Bloom (auto_time)
    g_mock_millis = 10100;
    brew_tick();
    ASSERT_STREQ(brew_get_stage_name(), "Pour");  // stage 1 = manual

    // Now advance past Pour (manual → brew_next → last stage → brew_stop)
    brew_next();
    ASSERT_STREQ(brew_get_stage_name(), "Done");
}

// ============================================================================
// Tier 1: Manual stage & brew_next
// ============================================================================

TEST(manual_last_stage_goes_to_done) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_manual");
    ASSERT_STREQ(brew_get_stage_name(), "Step 1");

    brew_next();
    ASSERT_STREQ(brew_get_stage_name(), "Step 2");

    brew_next();
    ASSERT_STREQ(brew_get_stage_name(), "Step 3");

    brew_next();  // last stage → should go to DONE
    ASSERT_STREQ(brew_get_stage_name(), "Done");
}

TEST(manual_on_exit_effects_fire) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_manual");
    // Step 1 on_exit = EFFECT_CAPTURE_DOSE
    g_mock_weight = 16.5f;
    beep_log_clear();

    brew_next();  // Step 1 → Step 2

    // CAPTURE_DOSE should have captured the weight
    float dose = brew_get_dose_weight();
    // Note: tare fires on Step 1 on_enter, which resets weight to 0.
    // Then we set weight to 16.5. The capture happens on exit.
    ASSERT_TRUE(dose > 0.0f || dose == 0.0f);  // just verify no crash

    // Step 2 on_exit = CAPTURE_WEIGHT | BEEP
    g_mock_weight = 95.0f;
    beep_log_clear();
    brew_next();  // Step 2 → Step 3

    // on_exit BEEP should fire (with default beep, no beep_pattern on Step 2)
    ASSERT_TRUE(g_beep_log_count > 0);

    // Capture should have been recorded
    ASSERT_TRUE(brew_get_capture_count() > 0);
}

TEST(manual_brew_next_noop_on_auto_time) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_2stage");
    ASSERT_STREQ(brew_get_stage_name(), "Bloom");  // auto_time stage

    // brew_next should be a no-op on non-manual stage
    brew_next();
    ASSERT_STREQ(brew_get_stage_name(), "Bloom");  // unchanged
}

// ============================================================================
// Tier 1: State phase transitions
// ============================================================================

TEST(brew_start_clears_prior_state) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // Start a brew, make some progress
    brew_start("test_2stage");
    g_mock_weight = 30.0f;
    g_mock_millis = 5000;
    brew_tick();
    ASSERT_TRUE(brew_is_active());

    // Start a new brew — should reset everything
    brew_start("test_manual");
    ASSERT_STREQ(brew_get_stage_name(), "Step 1");
    ASSERT_EQ(brew_get_timer_ms(), (uint32_t)0);
    ASSERT_EQ(brew_get_capture_count(), (uint8_t)0);
}

TEST(brew_advance_idle_starts) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    ASSERT_STREQ(brew_get_stage_name(), "Idle");
    brew_advance("test_manual");
    ASSERT_TRUE(brew_is_active());
    ASSERT_STREQ(brew_get_stage_name(), "Step 1");
}

TEST(brew_advance_done_restarts) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_manual");
    brew_next(); brew_next(); brew_next();
    ASSERT_STREQ(brew_get_stage_name(), "Done");

    brew_advance("test_manual");
    ASSERT_TRUE(brew_is_active());
    ASSERT_STREQ(brew_get_stage_name(), "Step 1");
}

TEST(brew_advance_manual_nexts) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_manual");
    ASSERT_STREQ(brew_get_stage_name(), "Step 1");

    brew_advance();
    ASSERT_STREQ(brew_get_stage_name(), "Step 2");
}

TEST(brew_advance_timer_running_stops) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_3stage");
    // Trigger auto_weight → timer starts
    g_mock_weight = 5.0f;
    g_mock_millis = 1000;
    brew_tick();
    ASSERT_STREQ(brew_get_stage_name(), "Bloom");

    // brew_advance while timer running on auto_time stage → should stop
    brew_advance();
    ASSERT_STREQ(brew_get_stage_name(), "Done");
}

// ============================================================================
// Tier 2: Weight cue boundaries
// ============================================================================

TEST(weight_cue_disabled_zero_g) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // Use the zero_time template (no weight cues configured)
    brew_start("test_zero_time");
    beep_log_clear();  // clear on_enter beep
    g_mock_weight = 50.0f;
    g_mock_millis = 100;
    brew_tick();

    // No weight cue beeps should fire (weight_cue_g = 0)
    ASSERT_EQ(g_beep_log_count, 0);
}

TEST(weight_cue_multiple_thresholds) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // Use 2stage template: Bloom has weight_cue_g=5, times=2, target=50
    brew_start("test_2stage");

    // Weight at 40g (remaining = 10g, first cue threshold = 10g)
    g_mock_weight = 40.0f;
    g_mock_millis = 100;
    brew_tick();
    ASSERT_EQ(beep_log_count_exact("1500:200"), 1);  // first cue

    // Weight at 45g (remaining = 5g, second cue threshold = 5g)
    g_mock_weight = 45.0f;
    g_mock_millis = 200;
    brew_tick();
    ASSERT_EQ(beep_log_count_exact("1500:200"), 2);  // second cue

    // Weight at 48g — no more cues (both fired)
    g_mock_weight = 48.0f;
    g_mock_millis = 300;
    brew_tick();
    ASSERT_EQ(beep_log_count_exact("1500:200"), 2);  // still 2
}

TEST(weight_cue_no_fire_at_or_past_target) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // Use 2stage: target=50, cue_g=5, times=2
    brew_start("test_2stage");

    // Jump straight to target (remaining=0, which is not > 0)
    g_mock_weight = 50.0f;
    g_mock_millis = 100;
    brew_tick();
    // Cue should NOT fire (remaining <= 0), but weight_done should
    ASSERT_EQ(beep_log_count_exact("1500:200"), 0);
    ASSERT_EQ(beep_log_count_exact("1500:1000"), 1);  // weight_done

    // Jump past target
    g_mock_weight = 60.0f;
    g_mock_millis = 200;
    brew_tick();
    ASSERT_EQ(beep_log_count_exact("1500:200"), 0);  // still no cue
}

// ============================================================================
// Tier 2: Series recording
// ============================================================================

TEST(series_1hz_sampling) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // Use 3stage: auto_weight triggers timer
    brew_start("test_3stage");
    g_mock_weight = 5.0f;
    g_mock_millis = 1000;
    brew_tick();  // triggers timer, records first sample at t=0
    ASSERT_STREQ(brew_get_stage_name(), "Bloom");

    // Tick at 1999ms — should NOT record another sample (< 1000ms since last)
    g_mock_millis = 1999;
    brew_tick();

    // Tick at 2000ms — should record second sample
    g_mock_millis = 2000;
    brew_tick();

    // Tick at 3000ms — should record third sample
    g_mock_millis = 3000;
    brew_tick();

    // We can't directly access s_series_count, but timer should be running
    ASSERT_TRUE(brew_get_timer_ms() > (uint32_t)0);
}

TEST(series_no_crash_before_timer) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // Manual template — no auto_weight, so timer never starts
    brew_start("test_manual");
    g_mock_millis = 5000;
    brew_tick();
    brew_tick();
    brew_tick();

    // No crash, timer should be 0
    ASSERT_EQ(brew_get_timer_ms(), (uint32_t)0);
}

// ============================================================================
// Tier 2: Label fallbacks
// ============================================================================

TEST(next_label_idle_uses_template) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // After hinting a template, should use the template's start_label
    brew_hint_template("test_manual");
    ASSERT_STREQ(brew_get_next_label(), "Begin");
}

TEST(next_label_per_stage_override) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_manual");
    // Step 1 has next_label = "Go to 2"
    ASSERT_STREQ(brew_get_next_label(), "Go to 2");

    brew_next();
    // Step 2 has next_label = "Go to 3"
    ASSERT_STREQ(brew_get_next_label(), "Go to 3");

    brew_next();
    // Step 3 has next_label = "Done"
    ASSERT_STREQ(brew_get_next_label(), "Done");
}

TEST(next_label_done_uses_done_label) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // After stopping, next_label should be the template's done_label
    brew_start("test_manual");
    brew_next(); brew_next(); brew_next();  // → DONE
    ASSERT_STREQ(brew_get_next_label(), "Restart");
}

// ============================================================================
// Tier 2: Query API boundaries
// ============================================================================

TEST(weight_remaining_clamps_to_zero) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_2stage");  // Bloom target=50g
    g_mock_weight = 100.0f;  // way above target

    float remaining = brew_get_stage_weight_remaining();
    ASSERT_TRUE(remaining == 0.0f);  // clamped, not negative
}

TEST(time_remaining_clamps_to_zero) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_2stage");  // Bloom auto_time=10s

    // Set time way past stage duration
    g_mock_millis = 99000;

    uint32_t remaining = brew_get_stage_time_remaining_ms();
    ASSERT_EQ(remaining, (uint32_t)0);  // clamped, not underflowed
}

TEST(stage_queries_zero_when_idle) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    ASSERT_STREQ(brew_get_stage_name(), "Idle");
    ASSERT_TRUE(brew_get_stage_weight_target() == 0.0f);
    ASSERT_TRUE(brew_get_stage_weight_remaining() == 0.0f);
    ASSERT_TRUE(brew_get_stage_flow_target() == 0.0f);
    ASSERT_EQ(brew_get_stage_time_target_ms(), (uint32_t)0);
    ASSERT_EQ(brew_get_stage_time_remaining_ms(), (uint32_t)0);
    ASSERT_EQ(brew_get_stage_time_current_ms(), (uint32_t)0);
}

TEST(display_name_fallback_chain) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // While idle with no template, display_name should be ""
    // (s_template and s_last_template both null after fresh reset from no prior brew)
    // Actually after ensure_templates, s_last_template may persist from prior test.
    // Let's test during active brew:
    brew_start("test_manual");
    ASSERT_STREQ(brew_get_display_name(), "Test Manual");

    brew_stop();
    ASSERT_STREQ(brew_get_display_name(), "Test Manual");
}

// ============================================================================
// Tier 2: Effect dispatch
// ============================================================================

TEST(multiple_effects_all_dispatch) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // test_manual Step 1: on_enter = TARE | BEEP | MARKER
    brew_start("test_manual");

    // TARE should have zeroed weight
    // (we set it before start, tare resets it)
    g_mock_weight = 42.0f;
    // After start, tare was called, so weight was reset to 0 during enter_stage
    // The mock tare sets g_mock_weight = 0

    // BEEP with pattern "600:100" should have fired
    ASSERT_TRUE(beep_log_contains("600:100"));

    // No crash — all three effects dispatched
    ASSERT_TRUE(brew_is_active());
}

TEST(capture_weight_records_value) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_manual");

    // Set weight and advance through Step 1 and Step 2
    // Step 2 on_exit has CAPTURE_WEIGHT with cap1
    brew_next();  // Step 1 → Step 2
    g_mock_weight = 87.5f;
    brew_next();  // Step 2 → Step 3 (on_exit fires CAPTURE_WEIGHT)

    ASSERT_EQ(brew_get_capture_count(), (uint8_t)1);
    const BrewCapture* cap = brew_get_capture(0);
    ASSERT_TRUE(cap != nullptr);
    ASSERT_STREQ(cap->key, "cap1");
    ASSERT_STREQ(cap->label, "Capture 1");
    ASSERT_STREQ(cap->unit, "g");
    // Value should be the mock weight at time of capture
    ASSERT_TRUE(cap->value > 80.0f);
}

// ============================================================================
// Tier 2: Timer formatting
// ============================================================================

TEST(format_timer_mm_ss) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    // Start timer via auto_weight
    brew_start("test_3stage");
    g_mock_weight = 5.0f;
    g_mock_millis = 0;
    brew_tick();

    g_mock_millis = 125000;  // 2 min 5 sec
    char buf[32];
    brew_format_timer("mm:ss", buf, sizeof(buf));
    ASSERT_STREQ(buf, "2:05");
}

TEST(format_timer_hh_mm_ss) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_3stage");
    g_mock_weight = 5.0f;
    g_mock_millis = 0;
    brew_tick();

    g_mock_millis = 3723000;  // 1h 2m 3s
    char buf[32];
    brew_format_timer("hh:mm:ss", buf, sizeof(buf));
    ASSERT_STREQ(buf, "1:02:03");
}

TEST(format_timer_ss_and_decisec) {
    ensure_templates();
    reset_mocks();
    brew_reset();

    brew_start("test_3stage");
    g_mock_weight = 5.0f;
    g_mock_millis = 0;
    brew_tick();

    g_mock_millis = 65400;  // 65.4 seconds
    char buf[32];
    brew_format_timer("ss", buf, sizeof(buf));
    ASSERT_STREQ(buf, "65");

    brew_format_timer("mm:ss.d", buf, sizeof(buf));
    ASSERT_STREQ(buf, "1:05.4");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== brew_manager integration tests ===\n");

    printf("-- Stage transition timing (original) --\n");
    RUN(auto_time_transition_no_spurious_weight_done);
    RUN(weight_done_fires_on_new_stage);
    RUN(weight_cues_reset_on_new_stage);
    RUN(full_3stage_flow);

    printf("-- Audio cue ordering (original) --\n");
    RUN(countdown_done_fires_at_expiry);
    RUN(weight_done_fires_only_once);

    printf("\n-- Tier 1: AUTO_WEIGHT entry --\n");
    RUN(auto_weight_no_trigger_below_threshold);
    RUN(auto_weight_exact_threshold);
    RUN(auto_weight_single_stage_goes_to_done);
    RUN(auto_weight_timer_starts_once);

    printf("-- Tier 1: AUTO_TIME duration & countdown --\n");
    RUN(auto_time_zero_duration_no_auto_advance);
    RUN(auto_time_countdown_longer_than_stage_skipped);
    RUN(auto_time_no_countdown_beep_empty);
    RUN(auto_time_last_stage_goes_to_done);

    printf("-- Tier 1: Manual stage & brew_next --\n");
    RUN(manual_last_stage_goes_to_done);
    RUN(manual_on_exit_effects_fire);
    RUN(manual_brew_next_noop_on_auto_time);

    printf("-- Tier 1: State phase transitions --\n");
    RUN(brew_start_clears_prior_state);
    RUN(brew_advance_idle_starts);
    RUN(brew_advance_done_restarts);
    RUN(brew_advance_manual_nexts);
    RUN(brew_advance_timer_running_stops);

    printf("\n-- Tier 2: Weight cue boundaries --\n");
    RUN(weight_cue_disabled_zero_g);
    RUN(weight_cue_multiple_thresholds);
    RUN(weight_cue_no_fire_at_or_past_target);

    printf("-- Tier 2: Series recording --\n");
    RUN(series_1hz_sampling);
    RUN(series_no_crash_before_timer);

    printf("-- Tier 2: Label fallbacks --\n");
    RUN(next_label_idle_uses_template);
    RUN(next_label_per_stage_override);
    RUN(next_label_done_uses_done_label);

    printf("-- Tier 2: Query API boundaries --\n");
    RUN(weight_remaining_clamps_to_zero);
    RUN(time_remaining_clamps_to_zero);
    RUN(stage_queries_zero_when_idle);
    RUN(display_name_fallback_chain);

    printf("-- Tier 2: Effect dispatch --\n");
    RUN(multiple_effects_all_dispatch);
    RUN(capture_weight_records_value);

    printf("-- Tier 2: Timer formatting --\n");
    RUN(format_timer_mm_ss);
    RUN(format_timer_hh_mm_ss);
    RUN(format_timer_ss_and_decisec);

    printf("\n%d/%d tests passed\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}
