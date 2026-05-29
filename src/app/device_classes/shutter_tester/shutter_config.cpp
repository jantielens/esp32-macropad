#include "shutter_config.h"

#if IS_SHUTTER_TESTER

#include "shutter_defaults.h"
#include <string.h>

// NVS keys — unchanged from the legacy config_manager.cpp location to
// preserve wire format across firmware updates.
namespace {
constexpr const char* KEY_PRESET   = "sh_preset";
constexpr const char* KEY_OFFSET_X = "sh_off_x";
constexpr const char* KEY_OFFSET_Y = "sh_off_y";
} // namespace

ShutterTesterConfig shutter_config = {};

void shutter_config_defaults() {
    strlcpy(shutter_config.preset_id, SHUTTER_DEFAULT_PRESET_ID,
            SHUTTER_CONFIG_PRESET_ID_MAX_LEN);
    shutter_config.sensor_offset_x_mm = SHUTTER_DEFAULT_OFFSET_X_MM;
    shutter_config.sensor_offset_y_mm = SHUTTER_DEFAULT_OFFSET_Y_MM;
}

void shutter_config_load(Preferences& prefs) {
    prefs.getString(KEY_PRESET, shutter_config.preset_id,
                    SHUTTER_CONFIG_PRESET_ID_MAX_LEN);
    if (shutter_config.preset_id[0] == '\0') {
        strlcpy(shutter_config.preset_id, SHUTTER_DEFAULT_PRESET_ID,
                SHUTTER_CONFIG_PRESET_ID_MAX_LEN);
    }
    shutter_config.sensor_offset_x_mm =
        prefs.getFloat(KEY_OFFSET_X, SHUTTER_DEFAULT_OFFSET_X_MM);
    shutter_config.sensor_offset_y_mm =
        prefs.getFloat(KEY_OFFSET_Y, SHUTTER_DEFAULT_OFFSET_Y_MM);
}

void shutter_config_save(Preferences& prefs) {
    prefs.putString(KEY_PRESET, shutter_config.preset_id);
    prefs.putFloat(KEY_OFFSET_X, shutter_config.sensor_offset_x_mm);
    prefs.putFloat(KEY_OFFSET_Y, shutter_config.sensor_offset_y_mm);
}

#endif // IS_SHUTTER_TESTER
