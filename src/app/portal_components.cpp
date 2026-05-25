// portal_components.cpp — includes component definitions
//
// Arduino build system only compiles .cpp files in the sketch root directory.
// This translation unit centralizes the required component includes so each
// feature module can self-register via REGISTER_COMPONENT() without editing
// web_portal_routes.cpp. Same pattern as display_drivers.cpp.
//
// Components gated by compile flags are simply not included.

#include "board_config.h"

// --- Always-available components (no feature flag) ---
#include "components/setup_component.cpp"
#include "components/wifi_component.cpp"
#include "components/device_name_component.cpp"
#include "components/network_component.cpp"
#if !HAS_EPAPER
// On e-paper boards the Power Mode page is suppressed — e-paper devices
// always run in duty_cycle_epaper, and that mode is set automatically when
// the E-Paper fragment is saved (hidden operating_mode field).
// TODO: allow mode override for debugging on e-paper builds (show mode page
// with duty_cycle_epaper preselected and a "debug only" note).
#include "components/mode_component.cpp"
#endif
#include "components/factory_reset_component.cpp"
#include "components/ota_update_component.cpp"
#include "components/manual_upload_component.cpp"
#include "components/version_info_component.cpp"
#include "components/sensor_data_component.cpp"

// --- Display-gated components ---
#if HAS_DISPLAY
#include "components/display_component.cpp"
#include "components/screensaver_component.cpp"
#include "components/pad_editor_component.cpp"
#include "components/swipe_actions_component.cpp"
#include "components/boot_actions_component.cpp"
#include "components/button_defaults_component.cpp"
#include "components/timers_component.cpp"
#endif // HAS_DISPLAY

// --- MQTT-gated components ---
#if HAS_MQTT
#include "components/mqtt_component.cpp"
#include "components/ha_discovery_component.cpp"
#endif // HAS_MQTT

// --- BLE-gated components ---
#if HAS_BLE_HID
#include "components/ble_component.cpp"
#endif // HAS_BLE_HID

// --- Audio-gated components ---
#if HAS_AUDIO
#include "components/volume_component.cpp"
#endif // HAS_AUDIO

#if HAS_SOUND_PLAYER
#include "components/sounds_component.cpp"
#endif // HAS_SOUND_PLAYER

// --- E-Paper-gated components ---
#if HAS_EPAPER
#include "components/epaper_component.cpp"
#endif // HAS_EPAPER
