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
// Two stages: Ready (auto-start on weight) → Brewing (recording).
// Tare fires on entering Ready.

static const BrewStage s_free_pour_stages[] = {
    //  name        instruction                                              next_label  type               on_enter    on_exit      threshold
    { "Ready",   "Place your cup on the scale and start pouring when ready", "Armed",   STAGE_AUTO_WEIGHT, EFFECT_TARE, EFFECT_NONE, 2.0f },
    { "Brewing", "Pouring - Tap Done when brew is finished",                 "Done",    STAGE_RECORDING,   EFFECT_NONE, EFFECT_NONE, 0.0f },
};

static const BrewTemplate s_free_pour_template = {
    "free_pour",
    "Start brew",        // start_label (Idle)
    "Start brew again",  // done_label  (Done)
    s_free_pour_stages,
    2,
    false,
};

// ============================================================================
// Built-in: v60
// ============================================================================
// Four stages:
//   Dosing   — user weighs beans; brew_next() captures dose and moves to Prep cup
//   Prep cup — user grinds and preps; brew_next() tares and moves to Ready
//   Ready    — armed; auto-advances when first water pour exceeds threshold
//   Brewing  — timer running; brew_stop() ends and saves

static const BrewStage s_v60_stages[] = {
    //  name        instruction                                                             next_label   type               on_enter    on_exit              threshold
    { "Dosing",   "Weigh your beans on the scale, then tap Next to log the dose",          "Next",      STAGE_MANUAL,      EFFECT_NONE, EFFECT_CAPTURE_DOSE, 0.0f },
    { "Prep cup", "Remove beans and grind them. Prep cup and V60 on scale, tap Next to arm", "Next",    STAGE_MANUAL,      EFFECT_NONE, EFFECT_NONE,         0.0f },
    { "Ready",    "Start pouring when ready",                                              "Armed",     STAGE_AUTO_WEIGHT, EFFECT_TARE, EFFECT_NONE,         2.0f },
    { "Brewing",  "Pouring - Tap Done when brew is finished",                              "Done",      STAGE_RECORDING,   EFFECT_NONE, EFFECT_NONE,         0.0f },
};

static const BrewTemplate s_v60_template = {
    "v60",
    "Start V60",        // start_label (Idle)
    "Start V60 again",  // done_label  (Done)
    s_v60_stages,
    4,
    false,
};

// ============================================================================
// Init — register all built-ins
// ============================================================================

void brew_templates_init() {
    brew_template_register(&s_free_pour_template);
    brew_template_register(&s_v60_template);
    LOGI(TAG, "Registered %u built-in templates", (unsigned)s_registry_count);
}

#endif // HAS_SENSOR_HX711
