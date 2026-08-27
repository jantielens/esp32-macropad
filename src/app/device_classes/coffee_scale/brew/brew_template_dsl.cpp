// ============================================================================
// Brew Template DSL — JSON parser / serializer for brew templates
// ============================================================================
// Host-testable: depends only on ArduinoJson + standard C++ (no ESP32 APIs,
// no filesystem, no Serial).  The thin loader layer (brew_template_loader.cpp)
// handles filesystem I/O and calls these pure-parsing functions.

#include "brew_template_dsl.h"

#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Effect string ↔ bitmask
// ---------------------------------------------------------------------------

struct EffectEntry {
    const char* name;
    BrewEffects  flag;
};

static const EffectEntry s_effects[] = {
    { "tare",           EFFECT_TARE },
    { "capture_dose",   EFFECT_CAPTURE_DOSE },
    { "beep",           EFFECT_BEEP },
    { "capture_weight", EFFECT_CAPTURE_WEIGHT },
    { "marker",         EFFECT_MARKER },
};
static constexpr int s_effects_count = sizeof(s_effects) / sizeof(s_effects[0]);

BrewEffects brew_dsl_parse_effect(const char* name) {
    if (!name || !name[0]) return EFFECT_NONE;
    for (int i = 0; i < s_effects_count; i++) {
        if (strcmp(name, s_effects[i].name) == 0) return s_effects[i].flag;
    }
    return EFFECT_NONE;
}

const char* brew_dsl_effect_name(BrewEffects single_bit) {
    for (int i = 0; i < s_effects_count; i++) {
        if (s_effects[i].flag == single_bit) return s_effects[i].name;
    }
    return nullptr;
}

// Parse a JSON array of effect strings into a combined bitmask.
static BrewEffects parse_effects_array(JsonArrayConst arr) {
    BrewEffects mask = EFFECT_NONE;
    for (const char* s : arr) {
        if (s) mask |= brew_dsl_parse_effect(s);
    }
    return mask;
}

// ---------------------------------------------------------------------------
// Stage type string ↔ enum
// ---------------------------------------------------------------------------

struct StageTypeEntry {
    const char*   name;
    BrewStageType type;
};

static const StageTypeEntry s_stage_types[] = {
    { "manual",      STAGE_MANUAL },
    { "auto_weight", STAGE_AUTO_WEIGHT },
    { "auto_time",   STAGE_AUTO_TIME },
};
static constexpr int s_stage_types_count = sizeof(s_stage_types) / sizeof(s_stage_types[0]);

int brew_dsl_parse_stage_type(const char* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < s_stage_types_count; i++) {
        if (strcmp(name, s_stage_types[i].name) == 0) return (int)s_stage_types[i].type;
    }
    return -1;
}

const char* brew_dsl_stage_type_name(BrewStageType t) {
    for (int i = 0; i < s_stage_types_count; i++) {
        if (s_stage_types[i].type == t) return s_stage_types[i].name;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: safe copy into fixed-size char buffer
// ---------------------------------------------------------------------------

static void safe_copy(char* dst, size_t dst_size, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    strlcpy(dst, src, dst_size);
}

// ---------------------------------------------------------------------------
// Helper: write error message
// ---------------------------------------------------------------------------

static void set_err(char* err_buf, size_t err_buf_len, const char* msg) {
    if (err_buf && err_buf_len > 0) {
        strlcpy(err_buf, msg, err_buf_len);
    }
}

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------

int brew_dsl_parse(const char* json, size_t json_len,
                   BrewTemplate** out_tmpl, BrewStage** out_stages,
                   char* err_buf, size_t err_buf_len) {
    // Init outputs to null
    if (out_tmpl)   *out_tmpl   = nullptr;
    if (out_stages) *out_stages = nullptr;

    if (!json || json_len == 0) {
        set_err(err_buf, err_buf_len, "empty or null JSON input");
        return BREW_DSL_ERR_JSON;
    }

    // Parse JSON
    JsonDocument doc;
    DeserializationError jerr = deserializeJson(doc, json, json_len);
    if (jerr) {
        set_err(err_buf, err_buf_len, jerr.c_str());
        return BREW_DSL_ERR_JSON;
    }

    // Version check
    int version = doc["v"] | 0;
    if (version != 1) {
        set_err(err_buf, err_buf_len, "unsupported version (expected v:1)");
        return BREW_DSL_ERR_VERSION;
    }

    // Template name (required)
    const char* name = doc["name"] | (const char*)nullptr;
    if (!name || !name[0]) {
        set_err(err_buf, err_buf_len, "missing or empty 'name' field");
        return BREW_DSL_ERR_MISSING_NAME;
    }

    // Stages array (required, non-empty)
    JsonArrayConst stages_arr = doc["stages"].as<JsonArrayConst>();
    if (stages_arr.isNull() || stages_arr.size() == 0) {
        set_err(err_buf, err_buf_len, "missing or empty 'stages' array");
        return BREW_DSL_ERR_NO_STAGES;
    }

    int stage_count = (int)stages_arr.size();
    if (stage_count > BREW_DSL_MAX_STAGES) {
        char msg[64];
        snprintf(msg, sizeof(msg), "too many stages (%d, max %d)", stage_count, BREW_DSL_MAX_STAGES);
        set_err(err_buf, err_buf_len, msg);
        return BREW_DSL_ERR_TOO_MANY;
    }

    // Validate all stages before allocating
    for (int i = 0; i < stage_count; i++) {
        JsonObjectConst sobj = stages_arr[i].as<JsonObjectConst>();

        // Stage name required
        const char* sname = sobj["name"] | (const char*)nullptr;
        if (!sname || !sname[0]) {
            char msg[64];
            snprintf(msg, sizeof(msg), "stage %d: missing 'name'", i);
            set_err(err_buf, err_buf_len, msg);
            return BREW_DSL_ERR_STAGE_NAME;
        }

        // Stage type required and must be known
        const char* stype = sobj["type"] | (const char*)nullptr;
        if (brew_dsl_parse_stage_type(stype) < 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "stage %d: unknown type '%s'",
                     i, stype ? stype : "(null)");
            set_err(err_buf, err_buf_len, msg);
            return BREW_DSL_ERR_STAGE_TYPE;
        }

        // target_time_s must fit when converted to the internal millisecond
        // representation. Reject invalid values rather than silently wrapping
        // into a short or zero-duration target.
        JsonVariantConst target_time = sobj["target_time_s"];
        if (!target_time.isNull()) {
            if (!target_time.is<uint32_t>()
                || target_time.as<uint64_t>() > (UINT32_MAX / 1000U)) {
                char msg[64];
                snprintf(msg, sizeof(msg), "stage %d: invalid 'target_time_s'", i);
                set_err(err_buf, err_buf_len, msg);
                return BREW_DSL_ERR_TARGET_TIME;
            }
        }
    }

    // Allocate
    BrewTemplate* tmpl = new (std::nothrow) BrewTemplate();
    BrewStage* stages  = new (std::nothrow) BrewStage[stage_count]();
    if (!tmpl || !stages) {
        delete tmpl;
        delete[] stages;
        set_err(err_buf, err_buf_len, "heap allocation failed");
        return BREW_DSL_ERR_ALLOC;
    }

    // Fill template-level fields
    safe_copy(tmpl->name,         sizeof(tmpl->name),         name);
    safe_copy(tmpl->display_name, sizeof(tmpl->display_name), doc["display_name"] | "");
    safe_copy(tmpl->description,  sizeof(tmpl->description),  doc["description"]  | "");
    safe_copy(tmpl->start_label,  sizeof(tmpl->start_label),  doc["start_label"]  | "");
    safe_copy(tmpl->done_label,   sizeof(tmpl->done_label),   doc["done_label"]   | "");
    safe_copy(tmpl->idle_instruction, sizeof(tmpl->idle_instruction), doc["idle_instruction"] | "");
    safe_copy(tmpl->done_instruction, sizeof(tmpl->done_instruction), doc["done_instruction"] | "");
    tmpl->stages      = stages;
    tmpl->stage_count  = (uint8_t)stage_count;
    tmpl->is_dynamic   = true;

    // Fill stages
    for (int i = 0; i < stage_count; i++) {
        JsonObjectConst sobj = stages_arr[i].as<JsonObjectConst>();
        BrewStage& s = stages[i];

        safe_copy(s.name,        sizeof(s.name),        sobj["name"]        | "");
        safe_copy(s.instruction, sizeof(s.instruction), sobj["instruction"] | "");
        safe_copy(s.next_label,  sizeof(s.next_label),  sobj["next_label"]  | "");

        s.type = (BrewStageType)brew_dsl_parse_stage_type(sobj["type"] | "manual");

        // Effects arrays
        if (sobj["on_enter"].is<JsonArrayConst>()) {
            s.on_enter = parse_effects_array(sobj["on_enter"].as<JsonArrayConst>());
        } else {
            s.on_enter = EFFECT_NONE;
        }
        if (sobj["on_exit"].is<JsonArrayConst>()) {
            s.on_exit = parse_effects_array(sobj["on_exit"].as<JsonArrayConst>());
        } else {
            s.on_exit = EFFECT_NONE;
        }

        // Numeric fields with defaults
        s.auto_threshold  = sobj["auto_threshold"] | 0.0f;
        s.target_weight   = sobj["target_weight"]  | 0.0f;
        s.target_flow_rate = sobj["target_flow_rate"] | 0.0f;

        // target_time_s → target_time_ms. Only auto_time stages auto-advance.
        uint32_t time_s = sobj["target_time_s"] | (uint32_t)0;
        s.target_time_ms = time_s * 1000;

        // Beep pattern (optional, empty string = default beep)
        safe_copy(s.beep_pattern, sizeof(s.beep_pattern), sobj["beep_pattern"] | "");

        // Countdown beep pattern (optional, auto_time stages only)
        safe_copy(s.countdown_beep, sizeof(s.countdown_beep), sobj["countdown_beep"] | "");

        // Countdown done beep (optional, auto_time stages only)
        safe_copy(s.countdown_done_beep, sizeof(s.countdown_done_beep), sobj["countdown_done_beep"] | "");

        // Weight proximity cue
        s.weight_cue_g     = sobj["weight_cue_g"] | 0.0f;
        s.weight_cue_times = sobj["weight_cue_times"] | (uint8_t)1;
        safe_copy(s.weight_cue_beep, sizeof(s.weight_cue_beep), sobj["weight_cue_beep"] | "");

        // Weight done beep (optional)
        safe_copy(s.weight_done_beep, sizeof(s.weight_done_beep), sobj["weight_done_beep"] | "");

        // Capture object
        JsonObjectConst cap = sobj["capture"].as<JsonObjectConst>();
        if (!cap.isNull()) {
            safe_copy(s.capture_key,   sizeof(s.capture_key),   cap["key"]   | "");
            safe_copy(s.capture_label, sizeof(s.capture_label), cap["label"] | "");
            safe_copy(s.capture_unit,  sizeof(s.capture_unit),  cap["unit"]  | "");
        } else {
            s.capture_key[0]   = '\0';
            s.capture_label[0] = '\0';
            s.capture_unit[0]  = '\0';
        }
    }

    *out_tmpl   = tmpl;
    *out_stages = stages;
    return BREW_DSL_OK;
}

// ---------------------------------------------------------------------------
// Serialize
// ---------------------------------------------------------------------------

// Write effect bitmask as JSON array of strings into the provided array.
static void serialize_effects(JsonArray arr, BrewEffects mask) {
    for (int i = 0; i < s_effects_count; i++) {
        if (mask & s_effects[i].flag) {
            arr.add(s_effects[i].name);
        }
    }
}

int brew_dsl_serialize(const BrewTemplate* tmpl, char* buf, size_t buf_len) {
    if (!tmpl || !buf || buf_len == 0) return -1;

    JsonDocument doc;

    doc["v"] = 1;
    doc["name"] = tmpl->name;
    if (tmpl->display_name[0]) doc["display_name"] = tmpl->display_name;
    if (tmpl->description[0])  doc["description"]  = tmpl->description;
    if (tmpl->start_label[0])  doc["start_label"]  = tmpl->start_label;
    if (tmpl->done_label[0])   doc["done_label"]   = tmpl->done_label;
    if (tmpl->idle_instruction[0]) doc["idle_instruction"] = tmpl->idle_instruction;
    if (tmpl->done_instruction[0]) doc["done_instruction"] = tmpl->done_instruction;

    JsonArray stages_arr = doc["stages"].to<JsonArray>();

    for (int i = 0; i < tmpl->stage_count; i++) {
        const BrewStage& s = tmpl->stages[i];
        JsonObject sobj = stages_arr.add<JsonObject>();

        sobj["name"]        = s.name;
        sobj["instruction"] = s.instruction;
        sobj["next_label"]  = s.next_label;
        sobj["type"]        = brew_dsl_stage_type_name(s.type);

        // Effects (only emit if non-zero)
        if (s.on_enter != EFFECT_NONE) {
            serialize_effects(sobj["on_enter"].to<JsonArray>(), s.on_enter);
        }
        if (s.on_exit != EFFECT_NONE) {
            serialize_effects(sobj["on_exit"].to<JsonArray>(), s.on_exit);
        }

        // Numeric fields (only emit if non-zero)
        if (s.auto_threshold != 0.0f)  sobj["auto_threshold"]  = s.auto_threshold;
        if (s.target_weight != 0.0f)   sobj["target_weight"]   = s.target_weight;
        if (s.target_flow_rate != 0.0f) sobj["target_flow_rate"] = s.target_flow_rate;
        if (s.target_time_ms != 0)     sobj["target_time_s"]   = s.target_time_ms / 1000;

        // Beep pattern (only emit if non-empty)
        if (s.beep_pattern[0]) sobj["beep_pattern"] = s.beep_pattern;
        if (s.countdown_beep[0]) sobj["countdown_beep"] = s.countdown_beep;
        if (s.countdown_done_beep[0]) sobj["countdown_done_beep"] = s.countdown_done_beep;

        // Weight proximity cue (only emit if cue_g is non-zero)
        if (s.weight_cue_g != 0.0f) {
            sobj["weight_cue_g"] = s.weight_cue_g;
            if (s.weight_cue_times != 1) sobj["weight_cue_times"] = s.weight_cue_times;
            if (s.weight_cue_beep[0])    sobj["weight_cue_beep"]  = s.weight_cue_beep;
        }
        if (s.weight_done_beep[0]) sobj["weight_done_beep"] = s.weight_done_beep;

        // Capture (only emit if key is non-empty)
        if (s.capture_key[0]) {
            JsonObject cap = sobj["capture"].to<JsonObject>();
            cap["key"]   = s.capture_key;
            cap["label"] = s.capture_label;
            cap["unit"]  = s.capture_unit;
        }
    }

    size_t written = serializeJson(doc, buf, buf_len);
    if (written == 0 || written >= buf_len) return -1;
    return (int)written;
}

// ---------------------------------------------------------------------------
// Beep pattern duration calculator
// ---------------------------------------------------------------------------
// Mirrors the audio DSL parsing logic (space-delimited "freq:dur" tones and
// bare "dur" gaps) but only sums durations without playing anything.
// Pure C — no ESP32 dependencies, host-testable.

uint32_t brew_dsl_beep_duration_ms(const char* pattern) {
    if (!pattern || !pattern[0]) return 0;

    uint32_t total = 0;
    // Work on a local copy since strtok_r is destructive
    char buf[128];
    strlcpy(buf, pattern, sizeof(buf));

    char* saveptr = nullptr;
    char* tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
        char* colon = strchr(tok, ':');
        if (colon) {
            // "freq:dur" — only the duration matters
            uint16_t dur = (uint16_t)atoi(colon + 1);
            if (dur > 0 && dur <= 10000) total += dur;
        } else {
            // bare "dur" — silence gap
            uint16_t dur = (uint16_t)atoi(tok);
            if (dur > 0 && dur <= 10000) total += dur;
        }
        tok = strtok_r(nullptr, " ", &saveptr);
    }
    return total;
}
