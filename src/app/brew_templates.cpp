#include "brew_templates.h"

#if HAS_SENSOR_HX711

#include "log_manager.h"
#include <string.h>

#define TAG "BrewTpl"

// ============================================================================
// Registry
// ============================================================================

#define BREW_TEMPLATE_REGISTRY_MAX  8

static const BrewTemplate* s_registry[BREW_TEMPLATE_REGISTRY_MAX];
static uint8_t              s_registry_count = 0;

void brew_template_register(const BrewTemplate* t) {
    if (!t) return;
    if (s_registry_count >= BREW_TEMPLATE_REGISTRY_MAX) {
        LOGW(TAG, "Registry full, cannot register '%s'", t->name);
        return;
    }
    s_registry[s_registry_count++] = t;
    LOGD(TAG, "Registered template '%s' (%u stages)", t->name, (unsigned)t->stage_count);
}

const BrewTemplate* brew_template_find(const char* name) {
    if (!name || !name[0]) name = "free_pour";
    for (uint8_t i = 0; i < s_registry_count; i++) {
        if (strcmp(s_registry[i]->name, name) == 0) return s_registry[i];
    }
    LOGW(TAG, "Template '%s' not found", name);
    return nullptr;
}

void brew_templates_clear_dynamic() {
    uint8_t dst = 0;
    for (uint8_t i = 0; i < s_registry_count; i++) {
        if (s_registry[i]->is_dynamic) {
            // Free heap-allocated stages array and template struct.
            // Both must have been allocated with new[]/new.
            delete[] s_registry[i]->stages;
            delete s_registry[i];
            LOGD(TAG, "Freed dynamic template slot %u", (unsigned)i);
        } else {
            s_registry[dst++] = s_registry[i];
        }
    }
    s_registry_count = dst;
}

// ============================================================================
// Built-in: free_pour
// ============================================================================
// Two stages: Ready (auto-start on weight) → Brewing (manual stop).
// Tare fires on entering Ready. Recording starts automatically when pour is
// detected and continues until the user taps Done.

static const BrewStage s_free_pour_stages[] = {
    {
        "Ready",                                                       // name
        "Place your cup on the scale and start pouring when ready",     // instruction
        "Armed",                                                       // next_label
        STAGE_AUTO_WEIGHT,                                              // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        2.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Brewing",                                                      // name
        "Pouring - Tap Done when brew is finished",                     // instruction
        "Done",                                                         // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
};

static const BrewTemplate s_free_pour_template = {
    "free_pour",
    "Free Pour",         // display_name
    "",                  // description
    "Start brew",        // start_label (Idle)
    "Start brew again",  // done_label  (Done)
    s_free_pour_stages,
    2,
    false,
};

// ============================================================================
// Built-in: v60
// ============================================================================
// Five stages:
//   Place cup — user places empty dosing cup on scale; tare fires at start
//   Dosing    — tares on enter (zeroes out cup); on_exit captures dose
//   Prep cup  — user grinds and preps; brew_next() tares and moves to Ready
//   Ready     — armed; auto-advances when first water pour exceeds threshold
//               (this starts the timer + recording)
//   Brewing   — recording continues; user taps Done to finish

static const BrewStage s_v60_stages[] = {
    {
        "Place cup",                                                    // name
        "Place your empty dosing cup on the scale, then tap Weigh beans", // instruction
        "Weigh beans",                                                  // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Dosing",                                                       // name
        "Add beans to the cup, then tap Log dose when done",            // instruction
        "Log dose",                                                     // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_CAPTURE_DOSE,                                            // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Prep cup",                                                     // name
        "Remove beans and grind them. Prep cup and V60 on scale, tap Next to arm", // instruction
        "Next",                                                         // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Ready",                                                        // name
        "Start pouring when ready",                                     // instruction
        "Armed",                                                        // next_label
        STAGE_AUTO_WEIGHT,                                              // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        2.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Brewing",                                                      // name
        "Pouring - Tap Done when brew is finished",                     // instruction
        "Done",                                                         // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
};

static const BrewTemplate s_v60_template = {
    "v60",
    "V60 Pour-Over",     // display_name
    "",                  // description
    "Start V60",         // start_label (Idle)
    "Start V60 again",   // done_label  (Done)
    s_v60_stages,
    5,
    false,
};

// ============================================================================
// Built-in: rao_v60
// ============================================================================
// Six stages exercising all new building blocks:
//   Place cup — user places empty dosing cup on scale; tare fires at start
//   Dose beans — tares on enter (zeroes out cup); captures dose weight on exit
//   Prep      — manual, tares on enter
//   Arm pour  — auto_weight, tares, starts timer+recording on first pour
//   Bloom     — auto_time 45s, beep on enter, captures bloom water on exit, target flow 6 g/s
//   Main pour — manual, beep on enter, target 250g, target flow 5 g/s, user taps Done

static const BrewStage s_rao_v60_stages[] = {
    {
        "Place cup",                                                    // name
        "Place your empty dosing cup on the scale, then tap Tare scale", // instruction
        "Tare scale",                                                   // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Dose beans",                                                   // name
        "Add beans to the cup, tap Log dose when done",                 // instruction
        "Log dose",                                                     // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_CAPTURE_DOSE,                                            // on_exit
        0.0f,                                                           // auto_threshold
        16.0f,                                                          // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Prep",                                                         // name
        "Grind beans, rinse filter, place cup + V60 on scale. Tap Ready to arm", // instruction
        "Ready",                                                        // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Arm pour",                                                     // name
        "Start pouring when ready",                                     // instruction
        "Armed",                                                        // next_label
        STAGE_AUTO_WEIGHT,                                              // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        2.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight (no target; Bloom owns the pour target)
        0.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Bloom",                                                        // name
        "Pour to [brew:stage_weight_target]g, then swirl gently. [brew:stage_time_remaining]s remaining", // instruction
        "Blooming...",                                                  // next_label
        STAGE_AUTO_TIME,                                                // type
        EFFECT_BEEP,                                                    // on_enter
        EFFECT_CAPTURE_WEIGHT,                                          // on_exit
        0.0f,                                                           // auto_threshold
        60.0f,                                                          // target_weight
        6.0f,                                                           // target_flow_rate
        45000,                                                          // auto_time_ms
        "bloom_water",                                                  // capture_key
        "Bloom Water",                                                  // capture_label
        "g"                                                             // capture_unit
    },
    {
        "Main pour",                                                    // name
        "Pour steadily in circles to [brew:stage_weight_target]g, tap Done when finished", // instruction
        "Done",                                                         // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_BEEP,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        250.0f,                                                         // target_weight
        5.0f,                                                           // target_flow_rate
        0,                                                              // auto_time_ms
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
};

static const BrewTemplate s_rao_v60_template = {
    "rao_v60",
    "James Rao V60",                                     // display_name
    "Single-pour V60 with bloom stage and 5:00 target",  // description
    "Start Rao V60",     // start_label (Idle)
    "Brew again",        // done_label  (Done)
    s_rao_v60_stages,
    6,
    false,
};

// ============================================================================
// Init — register all built-ins
// ============================================================================

void brew_templates_init() {
    brew_template_register(&s_free_pour_template);
    brew_template_register(&s_v60_template);
    brew_template_register(&s_rao_v60_template);
    LOGI(TAG, "Registered %u built-in templates", (unsigned)s_registry_count);
}

#endif // HAS_SENSOR_HX711
