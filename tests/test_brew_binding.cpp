// ============================================================================
// Tests for brew_binding — [brew:key] and [brew:key;format] resolution
// ============================================================================
// Host-compiled. Tests the brew binding scheme resolver by putting the brew
// manager into known states and verifying the resolver output.

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

// ---- Scale HAL mock ----
float scale_get_weight()    { return g_mock_weight; }
float scale_get_weight_ema(){ return g_mock_weight; }
float scale_get_flow_rate() { return g_mock_flow_rate; }
void  scale_request_tare_no_persist() { g_mock_weight = 0.0f; }

// ---- brew_log stubs ----
#include "brew_manager.h"
#include "brew_templates.h"
#include "binding_template.h"

extern "C" uint16_t brew_log_save(uint32_t, float, const BrewTemplate*, float,
                                  const BrewSample*, uint16_t,
                                  const BrewMarker*, uint8_t,
                                  const BrewCapture*, uint8_t) { return 1; }
extern "C" void brew_log_init() {}
extern "C" uint16_t brew_log_count() { return 0; }
extern "C" uint16_t brew_log_import_raw(const char*, size_t) { return 0; }

void brew_template_loader_load() {}
void brew_template_loader_reload() {}

// ---- Binding template stubs ----
// We only need the register and resolve functions.
// resolve is called by brew_binding for the "instruction" key.

static binding_resolver_fn     s_resolvers[8]  = {};
static binding_topic_collector_fn s_collectors[8] = {};
static const char*             s_schemes[8]    = {};
static int                     s_scheme_count  = 0;

bool binding_template_register(const char* scheme, binding_resolver_fn resolver,
                               binding_topic_collector_fn collector) {
    if (s_scheme_count >= 8) return false;
    s_schemes[s_scheme_count]    = scheme;
    s_resolvers[s_scheme_count]  = resolver;
    s_collectors[s_scheme_count] = collector;
    s_scheme_count++;
    return true;
}

bool binding_template_has_bindings(const char* label) {
    return label && strchr(label, '[') != nullptr;
}

// Minimal resolve: finds [scheme:params] tokens and calls the registered resolver.
// Only handles single-token strings for simplicity.
bool binding_template_resolve(const char* templ, char* out, size_t out_len) {
    if (!templ || !templ[0]) {
        out[0] = '\0';
        return false;
    }
    // Simple passthrough for non-binding strings
    const char* open = strchr(templ, '[');
    if (!open) {
        strlcpy(out, templ, out_len);
        return false;
    }
    // For test purposes, just copy the template as-is
    strlcpy(out, templ, out_len);
    return true;
}

void binding_template_collect_topics(const char*, void*) {}

// ---- Helper: resolve a brew binding key ----
static bool resolve_brew(const char* params, char* out, size_t out_len) {
    // Find the "brew" resolver
    for (int i = 0; i < s_scheme_count; i++) {
        if (strcmp(s_schemes[i], "brew") == 0) {
            return s_resolvers[i](params, out, out_len);
        }
    }
    return false;
}

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

#define ASSERT_EQ(a, b) do {                                 \
    auto _a = (a); auto _b = (b);                            \
    if (_a != _b) {                                          \
        printf("FAIL\n    %s:%d: %s (%d) != %s (%d)\n",     \
               __FILE__, __LINE__, #a, (int)_a, #b, (int)_b);\
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
}

// Test template: auto_weight → auto_time(10s, target 50g, flow 5.0) → manual
static const BrewStage s_bind_stages[] = {
    {
        "Arm Pour", "Start pouring now", "", STAGE_AUTO_WEIGHT,
        EFFECT_TARE, EFFECT_NONE,
        3.0f, 0.0f, 0.0f, 0,
        "", "", "",
        0.0f, 0, "", "",
        "", "", "",
    },
    {
        "Bloom", "Pour to 50g", "", STAGE_AUTO_TIME,
        EFFECT_BEEP | EFFECT_MARKER, EFFECT_CAPTURE_WEIGHT,
        0.0f, 50.0f, 5.0f, 10000,
        "1200:500", "", "",
        0.0f, 0, "", "",
        "bloom", "Bloom Water", "g",
    },
    {
        "Main Pour", "Pour to 200g", "Done", STAGE_MANUAL,
        EFFECT_MARKER, EFFECT_NONE,
        0.0f, 200.0f, 4.0f, 0,
        "", "", "",
        0.0f, 0, "", "",
        "", "", "",
    },
};

static const BrewTemplate s_bind_template = {
    "bind_test", "Binding Test Template", "For binding tests",
    "Start Brew", "Brew Again",
    s_bind_stages, 3, false,
};

static bool s_init_done = false;

static void ensure_init() {
    if (!s_init_done) {
        brew_templates_init();
        brew_template_register(&s_bind_template);
        // Register brew binding scheme
        // We need to include the brew_binding.cpp init (linked)
        extern void brew_binding_init();
        brew_binding_init();
        s_init_done = true;
    }
}

// ============================================================================
// Tests
// ============================================================================

// --- Basic keys ---

TEST(binding_weight) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_weight = 42.3f;
    char out[64];
    ASSERT_TRUE(resolve_brew("weight", out, sizeof(out)));
    ASSERT_STREQ(out, "42.3");
}

TEST(binding_weight_format) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_weight = 42.375f;
    char out[64];
    ASSERT_TRUE(resolve_brew("weight;%.2f", out, sizeof(out)));
    ASSERT_STREQ(out, "42.38");
}

TEST(binding_flow_rate) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_flow_rate = 3.7f;
    char out[64];
    ASSERT_TRUE(resolve_brew("flow_rate", out, sizeof(out)));
    ASSERT_STREQ(out, "3.7");
}

TEST(binding_stage) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    char out[64];
    ASSERT_TRUE(resolve_brew("stage", out, sizeof(out)));
    ASSERT_STREQ(out, "Arm Pour");

    // Trigger auto_weight
    g_mock_weight = 4.0f;
    g_mock_millis = 100;
    brew_tick();

    ASSERT_TRUE(resolve_brew("stage", out, sizeof(out)));
    ASSERT_STREQ(out, "Bloom");
}

TEST(binding_active) {
    ensure_init();
    reset_mocks();
    brew_reset();

    char out[64];
    brew_start("bind_test");
    ASSERT_TRUE(resolve_brew("active", out, sizeof(out)));
    ASSERT_STREQ(out, "1");

    brew_stop();
    ASSERT_TRUE(resolve_brew("active", out, sizeof(out)));
    ASSERT_STREQ(out, "0");
}

TEST(binding_template_name) {
    ensure_init();
    reset_mocks();
    brew_reset();

    char out[64];
    // Idle: no template
    ASSERT_TRUE(resolve_brew("template", out, sizeof(out)));
    ASSERT_STREQ(out, "Idle");

    brew_start("bind_test");
    ASSERT_TRUE(resolve_brew("template", out, sizeof(out)));
    ASSERT_STREQ(out, "bind_test");
}

TEST(binding_dose) {
    ensure_init();
    reset_mocks();
    brew_reset();

    brew_start("bind_test");
    char out[64];
    // Dose is 0 before capture
    ASSERT_TRUE(resolve_brew("dose", out, sizeof(out)));
    ASSERT_STREQ(out, "0.0");
}

TEST(binding_timer_default) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    // Before timer starts
    char out[64];
    ASSERT_TRUE(resolve_brew("timer", out, sizeof(out)));
    ASSERT_STREQ(out, "0:00");

    // Start timer
    g_mock_weight = 5.0f;
    g_mock_millis = 0;
    brew_tick();

    g_mock_millis = 65000;
    ASSERT_TRUE(resolve_brew("timer", out, sizeof(out)));
    ASSERT_STREQ(out, "1:05");
}

TEST(binding_timer_with_format) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_weight = 5.0f;
    g_mock_millis = 0;
    brew_tick();

    g_mock_millis = 3661000;  // 1h 1m 1s
    char out[64];
    ASSERT_TRUE(resolve_brew("timer;hh:mm:ss", out, sizeof(out)));
    ASSERT_STREQ(out, "1:01:01");
}

// --- water and ratio ---

TEST(binding_water_and_ratio) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    // Simulate dose capture: use a template with CAPTURE_DOSE
    // For simplicity, dose is 0 here, so ratio should show "---"
    char out[64];
    ASSERT_TRUE(resolve_brew("ratio", out, sizeof(out)));
    ASSERT_STREQ(out, "---");
}

// --- Stage weight queries ---

TEST(binding_stage_weight_target) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    // Arm Pour has no target
    char out[64];
    ASSERT_TRUE(resolve_brew("stage_weight_target", out, sizeof(out)));
    ASSERT_STREQ(out, "0");

    // Trigger → Bloom (target=50)
    g_mock_weight = 4.0f;
    g_mock_millis = 100;
    brew_tick();

    ASSERT_TRUE(resolve_brew("stage_weight_target", out, sizeof(out)));
    ASSERT_STREQ(out, "50");
}

TEST(binding_stage_weight_remaining) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_weight = 4.0f;
    g_mock_millis = 100;
    brew_tick();  // → Bloom, target=50

    g_mock_weight = 30.0f;
    char out[64];
    ASSERT_TRUE(resolve_brew("stage_weight_remaining", out, sizeof(out)));
    ASSERT_STREQ(out, "20");  // 50 - 30 = 20, format "%.0f"
}

TEST(binding_stage_weight_pct) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_weight = 4.0f;
    g_mock_millis = 100;
    brew_tick();  // → Bloom, target=50

    g_mock_weight = 25.0f;
    char out[64];
    ASSERT_TRUE(resolve_brew("stage_weight_pct", out, sizeof(out)));
    ASSERT_STREQ(out, "50");  // 25/50 * 100 = 50%

    // Zero target → 0%
    // Advance to auto_time expiry to test later stages
}

TEST(binding_stage_weight_pct_zero_target) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    // Arm Pour has target_weight=0
    g_mock_weight = 1.0f;
    char out[64];
    ASSERT_TRUE(resolve_brew("stage_weight_pct", out, sizeof(out)));
    ASSERT_STREQ(out, "0");  // 0 because target is 0 → no division
}

// --- Stage time queries ---

TEST(binding_stage_time_target) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    // Arm Pour is auto_weight → target=0
    char out[64];
    ASSERT_TRUE(resolve_brew("stage_time_target", out, sizeof(out)));
    ASSERT_STREQ(out, "0");

    // Trigger → Bloom (auto_time=10s)
    g_mock_weight = 4.0f;
    g_mock_millis = 100;
    brew_tick();

    ASSERT_TRUE(resolve_brew("stage_time_target", out, sizeof(out)));
    ASSERT_STREQ(out, "10");
}

TEST(binding_stage_time_remaining) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_weight = 4.0f;
    g_mock_millis = 1000;
    brew_tick();  // → Bloom, auto_time=10s, entered at ~1000ms

    g_mock_millis = 4000;  // 3s elapsed in stage
    char out[64];
    ASSERT_TRUE(resolve_brew("stage_time_remaining", out, sizeof(out)));
    ASSERT_STREQ(out, "7");  // 10 - 3 = 7s
}

TEST(binding_stage_time_pct) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_weight = 4.0f;
    g_mock_millis = 1000;
    brew_tick();  // → Bloom, auto_time=10s

    g_mock_millis = 6000;  // 5s elapsed
    char out[64];
    ASSERT_TRUE(resolve_brew("stage_time_pct", out, sizeof(out)));
    ASSERT_STREQ(out, "50");  // 5/10 * 100 = 50%
}

// --- Stage flow queries ---

TEST(binding_stage_flow_target) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_weight = 4.0f;
    g_mock_millis = 100;
    brew_tick();  // → Bloom, flow_target=5.0

    char out[64];
    ASSERT_TRUE(resolve_brew("stage_flow_target", out, sizeof(out)));
    ASSERT_STREQ(out, "5.0");
}

TEST(binding_stage_flow_pct) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    g_mock_weight = 4.0f;
    g_mock_millis = 100;
    brew_tick();  // → Bloom, flow_target=5.0

    g_mock_flow_rate = 2.5f;
    char out[64];
    ASSERT_TRUE(resolve_brew("stage_flow_pct", out, sizeof(out)));
    ASSERT_STREQ(out, "50");  // 2.5/5.0 * 100 = 50%
}

TEST(binding_stage_flow_pct_zero_target) {
    ensure_init();
    reset_mocks();
    brew_reset();
    brew_start("bind_test");

    // Arm Pour has flow_target=0
    g_mock_flow_rate = 3.0f;
    char out[64];
    ASSERT_TRUE(resolve_brew("stage_flow_pct", out, sizeof(out)));
    ASSERT_STREQ(out, "0");  // no division by zero
}

// --- Display name and labels ---

TEST(binding_display_name) {
    ensure_init();
    reset_mocks();
    brew_reset();

    brew_start("bind_test");
    char out[64];
    ASSERT_TRUE(resolve_brew("display_name", out, sizeof(out)));
    ASSERT_STREQ(out, "Binding Test Template");
}

TEST(binding_next_label) {
    ensure_init();
    reset_mocks();
    brew_reset();

    char out[64];
    // Idle → should use template start_label if hinted
    brew_hint_template("bind_test");
    ASSERT_TRUE(resolve_brew("next_label", out, sizeof(out)));
    ASSERT_STREQ(out, "Start Brew");

    brew_start("bind_test");
    // Active on Arm Pour → next_label is "" → default "Next"
    ASSERT_TRUE(resolve_brew("next_label", out, sizeof(out)));
    ASSERT_STREQ(out, "Next");
}

TEST(binding_instruction) {
    ensure_init();
    reset_mocks();
    brew_reset();

    brew_start("bind_test");
    char out[128];
    ASSERT_TRUE(resolve_brew("instruction", out, sizeof(out)));
    ASSERT_STREQ(out, "Start pouring now");
}

// --- Error cases ---

TEST(binding_unknown_key) {
    ensure_init();
    char out[64];
    bool ok = resolve_brew("nonexistent_key", out, sizeof(out));
    ASSERT_TRUE(!ok);
    ASSERT_STREQ(out, "ERR:bad key");
}

TEST(binding_empty_key) {
    ensure_init();
    char out[64];
    bool ok = resolve_brew("", out, sizeof(out));
    ASSERT_TRUE(!ok);
    ASSERT_STREQ(out, "ERR:no key");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== brew_binding resolver tests ===\n");

    printf("-- Basic keys --\n");
    RUN(binding_weight);
    RUN(binding_weight_format);
    RUN(binding_flow_rate);
    RUN(binding_stage);
    RUN(binding_active);
    RUN(binding_template_name);
    RUN(binding_dose);
    RUN(binding_timer_default);
    RUN(binding_timer_with_format);
    RUN(binding_water_and_ratio);

    printf("\n-- Stage weight queries --\n");
    RUN(binding_stage_weight_target);
    RUN(binding_stage_weight_remaining);
    RUN(binding_stage_weight_pct);
    RUN(binding_stage_weight_pct_zero_target);

    printf("\n-- Stage time queries --\n");
    RUN(binding_stage_time_target);
    RUN(binding_stage_time_remaining);
    RUN(binding_stage_time_pct);

    printf("\n-- Stage flow queries --\n");
    RUN(binding_stage_flow_target);
    RUN(binding_stage_flow_pct);
    RUN(binding_stage_flow_pct_zero_target);

    printf("\n-- Display name and labels --\n");
    RUN(binding_display_name);
    RUN(binding_next_label);
    RUN(binding_instruction);

    printf("\n-- Error cases --\n");
    RUN(binding_unknown_key);
    RUN(binding_empty_key);

    printf("\n%d/%d tests passed\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}
