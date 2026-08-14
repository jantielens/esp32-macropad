#include "brew_templates.h"

#if HAS_SCALE

#include "log_manager.h"
#include "brew_template_loader.h"
#include <string.h>

#define TAG "BrewTpl"

// ============================================================================
// Registry
// ============================================================================

#define BREW_TEMPLATE_REGISTRY_MAX  16

static const BrewTemplate* s_registry[BREW_TEMPLATE_REGISTRY_MAX];
static uint8_t              s_registry_count = 0;

void brew_template_register(const BrewTemplate* t) {
    if (!t) return;
    // Replace existing entry with same name (dynamic→new, built-in→override)
    for (uint8_t i = 0; i < s_registry_count; i++) {
        if (strcmp(s_registry[i]->name, t->name) == 0) {
            if (s_registry[i]->is_dynamic) {
                delete[] s_registry[i]->stages;
                delete s_registry[i];
            }
            s_registry[i] = t;
            LOGD(TAG, "Replaced template '%s' (%u stages)", t->name, (unsigned)t->stage_count);
            return;
        }
    }
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
    // Fall back to free_pour (guaranteed to exist as built-in)
    if (strcmp(name, "free_pour") != 0) {
        LOGW(TAG, "Template '%s' not found, falling back to free_pour", name);
        return brew_template_find("free_pour");
    }
    LOGE(TAG, "free_pour template missing from registry!");
    return nullptr;
}

uint8_t brew_template_count() {
    return s_registry_count;
}

const BrewTemplate* brew_template_get(uint8_t index) {
    if (index >= s_registry_count) return nullptr;
    return s_registry[index];
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
// Three stages: prepare brewer → Ready (auto-start on weight) → Brewing.
// Tare fires after the full brewing setup is placed on the scale. Recording starts
// automatically when pour is detected and continues until the user taps Finish.

static const BrewStage s_free_pour_stages[] = {
    {
        "Prepare brewer",                                              // name
        "Put the brewer with its filter and ground coffee on the scale, then tap Tare.", // instruction
        "Tare",                                                        // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        2.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Ready to pour",                                               // name
        "Start pouring to begin.",                                    // instruction
        "Waiting",                                                     // next_label
        STAGE_AUTO_WEIGHT,                                              // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        2.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "", "", "", 0.0f, 1, "", "", "", "", ""              // optional fields
    },
    {
        "Brewing",                                                      // name
        "When the brew is complete, tap Finish.",                      // instruction
        "Finish",                                                       // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
};

static const BrewTemplate s_free_pour_template = {
    "free_pour",
    "Free Pour",         // display_name
    "",                  // description
    "Start",             // start_label (Idle)
    "Start",             // done_label  (Done)
    "Tap Start to begin.",
    "Brew complete. Tap Start for a new brew.",
    s_free_pour_stages,
    3,
    false,
};

// ============================================================================
// Built-in: v60
// ============================================================================
// Five stages: place cup, weigh coffee, prepare brewer, auto-start, and brew.

static const BrewStage s_v60_stages[] = {
    {
        "Place cup",                                                    // name
        "Put the empty dosing cup on the scale, then tap Tare.",       // instruction
        "Tare",                                                         // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Weigh coffee",                                                 // name
        "Add coffee, then tap Save dose.",                              // instruction
        "Save dose",                                                    // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_CAPTURE_DOSE,                                            // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Prepare brewer",                                               // name
        "Put the prepared V60 and cup on the scale, then tap Ready.",  // instruction
        "Ready",                                                        // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Ready to pour",                                                // name
        "Start pouring when ready",                                     // instruction
        "Waiting",                                                      // next_label
        STAGE_AUTO_WEIGHT,                                              // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        2.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Brewing",                                                      // name
        "When the brew is complete, tap Finish.",                      // instruction
        "Finish",                                                       // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
};

static const BrewTemplate s_v60_template = {
    "v60",
    "V60 Pour-Over",     // display_name
    "",                  // description
    "Start",             // start_label (Idle)
    "Start",             // done_label  (Done)
    "Tap Start to begin.",
    "Brew complete. Tap Start for a new brew.",
    s_v60_stages,
    5,
    false,
};

// ============================================================================
// Built-in: rao_v60
// ============================================================================
// Seven stages demonstrate dose capture, cumulative water targets, timed stages,
// and flow-rate guidance.

static const BrewStage s_rao_v60_stages[] = {
    {
        "Place cup",                                                    // name
        "Put the empty dosing cup on the scale, then tap Tare.",       // instruction
        "Tare",                                                         // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Weigh coffee",                                                 // name
        "Add coffee to 16 g, then tap Save dose.",                      // instruction
        "Save dose",                                                    // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_CAPTURE_DOSE,                                            // on_exit
        0.0f,                                                           // auto_threshold
        16.0f,                                                          // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Prepare brewer",                                               // name
        "Put the prepared V60 and cup on the scale, then tap Ready.",  // instruction
        "Ready",                                                        // next_label
        STAGE_MANUAL,                                                   // type
        EFFECT_NONE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Ready to pour",                                                // name
        "Start pouring to begin.",                                     // instruction
        "Waiting",                                                      // next_label
        STAGE_AUTO_WEIGHT,                                              // type
        EFFECT_TARE,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        2.0f,                                                           // auto_threshold
        0.0f,                                                           // target_weight (no target; Bloom owns the pour target)
        0.0f,                                                           // target_flow_rate
        0,                                                              // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Bloom",                                                        // name
        "Pour to 50 g at 3 g/s, then let it bloom.",                   // instruction
        "Blooming",                                                     // next_label
        STAGE_AUTO_TIME,                                                // type
        EFFECT_BEEP,                                                    // on_enter
        EFFECT_CAPTURE_WEIGHT,                                          // on_exit
        0.0f,                                                           // auto_threshold
        50.0f,                                                          // target_weight
        3.0f,                                                           // target_flow_rate
        60000,                                                          // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "bloom_water",                                                  // capture_key
        "Bloom Water",                                                  // capture_label
        "g"                                                             // capture_unit
    },
    {
        "Second pour",                                                  // name
        "Pour to 150 g at 4 g/s, then wait.",                          // instruction
        "Pouring",                                                      // next_label
        STAGE_AUTO_TIME,                                                // type
        EFFECT_BEEP,                                                    // on_enter
        EFFECT_NONE,                                                    // on_exit
        0.0f,                                                           // auto_threshold
        150.0f,                                                         // target_weight
        4.0f,                                                           // target_flow_rate
        45000,                                                          // target_time_ms
        "",                                                             // beep_pattern
        "",                                                             // countdown_beep
        "",                                                             // countdown_done_beep
        0.0f,                                                           // weight_cue_g
        1,                                                              // weight_cue_times
        "",                                                             // weight_cue_beep
        "",                                                             // weight_done_beep
        "",                                                             // capture_key
        "",                                                             // capture_label
        ""                                                              // capture_unit
    },
    {
        "Final pour", "Pour to 250 g at 4 g/s. When the brew is complete, tap Finish.", "Finish",
        STAGE_MANUAL, EFFECT_BEEP, EFFECT_NONE,
        0.0f, 250.0f, 4.0f, 75000,
        "", "", "", 0.0f, 1, "", "", "", "", ""
    },
};

static const BrewTemplate s_rao_v60_template = {
    "rao_v60",
    "Advanced V60",                                  // display_name
    "Guided 16 g V60 with bloom, timed pours, and flow targets", // description
    "Start",             // start_label (Idle)
    "Start",             // done_label  (Done)
    "Tap Start to begin.",
    "Brew complete. Tap Start for a new brew.",
    s_rao_v60_stages,
    7,
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

    // Load dynamic templates from persistent storage (may override built-ins)
    brew_template_loader_load();
}

#endif // HAS_SCALE
