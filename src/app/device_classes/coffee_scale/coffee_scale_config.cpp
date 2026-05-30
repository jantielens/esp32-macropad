#include "coffee_scale_config.h"

#if IS_COFFEE_SCALE

#include "scale_hal.h"
#include "sensors/scale_smoothing.h"  // SCALE_PRESET_COUNT
#include "log_manager.h"
#include <string.h>
#include <stdlib.h>

namespace {
// NEW NVS keys (Phase 4 redesign):
//   - all <= 14 chars (ESP-IDF NVS limit is 15 bytes incl. null = 14 chars).
//   - sensor-agnostic `scale_` prefix (old `hx711_*` was wrong for NAU7802).
//   - no backward-compat with the legacy feature/coffee-scale branch.
constexpr const char* KEY_SCALE_CAL    = "scale_cal";
constexpr const char* KEY_SCALE_OFS    = "scale_ofs";
constexpr const char* KEY_SCALE_SMOOTH = "scale_smooth";

const char* const SM_NAMES[] = {"Stable", "Balanced", "Responsive"};
} // namespace

#undef TAG
#define TAG "CoffeeScale"

CoffeeScaleConfig coffee_scale_config = {};

void coffee_scale_config_defaults() {
    strlcpy(coffee_scale_config.scale_cal_factor, "1.0", COFFEE_SCALE_CAL_MAX_LEN);
    strlcpy(coffee_scale_config.scale_offset, "0", COFFEE_SCALE_CAL_MAX_LEN);
    coffee_scale_config.scale_smoothing = 1; // Balanced
}

void coffee_scale_config_load(Preferences& prefs) {
    bool have_cal = prefs.isKey(KEY_SCALE_CAL);

    prefs.getString(KEY_SCALE_CAL, coffee_scale_config.scale_cal_factor,
                    COFFEE_SCALE_CAL_MAX_LEN);
    if (coffee_scale_config.scale_cal_factor[0] == '\0') {
        strlcpy(coffee_scale_config.scale_cal_factor, "1.0", COFFEE_SCALE_CAL_MAX_LEN);
    }

    prefs.getString(KEY_SCALE_OFS, coffee_scale_config.scale_offset,
                    COFFEE_SCALE_CAL_MAX_LEN);
    if (coffee_scale_config.scale_offset[0] == '\0') {
        strlcpy(coffee_scale_config.scale_offset, "0", COFFEE_SCALE_CAL_MAX_LEN);
    }

    coffee_scale_config.scale_smoothing = prefs.getUChar(KEY_SCALE_SMOOTH, 1);
    if (coffee_scale_config.scale_smoothing >= SCALE_PRESET_COUNT) {
        coffee_scale_config.scale_smoothing = 1;
    }

    // Apply calibration + smoothing live so subsequent sensor reads use them.
    float factor = strtof(coffee_scale_config.scale_cal_factor, nullptr);
    if (factor == 0.0f) factor = 1.0f;
    scale_set_calibration(factor);
    scale_apply_preset(coffee_scale_config.scale_smoothing);

    LOGI(TAG, "Config loaded: cal=%s ofs=%s smoothing=%s (%u)",
         coffee_scale_config.scale_cal_factor,
         coffee_scale_config.scale_offset,
         SM_NAMES[coffee_scale_config.scale_smoothing],
         coffee_scale_config.scale_smoothing);

    // Auto-tare-on-first-boot rule (Phase 4 carry-forward from Phase 3
    // handoff): if no calibration data was persisted, the scale has never
    // been zeroed against its physical platform. Request a tare so the next
    // weight reading isn't garbage. User still needs to calibrate via portal
    // for accurate readings.
    if (!have_cal) {
        LOGW(TAG, "No calibration in NVS — scale needs calibration via portal");
        scale_request_tare_no_persist();
    }
}

void coffee_scale_config_save(Preferences& prefs) {
    prefs.putString(KEY_SCALE_CAL, coffee_scale_config.scale_cal_factor);
    prefs.putString(KEY_SCALE_OFS, coffee_scale_config.scale_offset);
    prefs.putUChar(KEY_SCALE_SMOOTH, coffee_scale_config.scale_smoothing);
}

#endif // IS_COFFEE_SCALE
