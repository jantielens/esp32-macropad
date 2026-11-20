#include "welcome_pad.h"

#if HAS_DISPLAY && HAS_FILESYSTEM && !IS_EPAPER_FRAME

#include "log_manager.h"
#include "pad_config.h"
#include "storage.h"

#include <stdio.h>

bool welcome_pad_provision(bool already_configured) {
    if (already_configured) return true;
    if (!storage_mount()) {
        LOGE("Welcome", "Cannot mount storage for initial welcome pad");
        return false;
    }

    static constexpr char marker[] = "/config/.welcome-pad";
    if (Storage.exists(marker)) return true;

    const StorageMutationGuard guard;
    if (!guard) {
        LOGE("Welcome", "Initial welcome pad write unavailable");
        return false;
    }

    bool has_pads = false;
    for (uint8_t index = 0; index < MAX_PADS; ++index) {
        char path[40];
        pad_config_path(index, path, sizeof(path));
        if (Storage.exists(path)) { has_pads = true; break; }
        // Preserve recoverable saves as well as published pads.
        snprintf(path, sizeof(path), "/config/pad_%u.json.bak", index);
        if (Storage.exists(path)) { has_pads = true; break; }
        snprintf(path, sizeof(path), "/config/pad_%u.json.tmp", index);
        if (Storage.exists(path)) { has_pads = true; break; }
    }

    if (!has_pads) {
        static const char json[] PROGMEM = R"json({
  "name": "Welcome",
  "layout": "grid",
  "cols": 5,
  "rows": 3,
  "button_shadow": "disabled",
  "buttons": [
    {
      "col": 1, "row": 0, "col_span": 3,
      "label_center": "Welcome!",
      "label_bottom": "[expr:[health:wifi_connected]==\"ON\"?\"Make this display yours.\nOpen in your browser:\":\"Make this display yours.\"]",
      "label_center_style": "font:@builtin;align:center;mode:wrap",
      "label_bottom_style": "font:@builtin;align:center;mode:wrap"
    },
    {
      "col": 0, "row": 1, "col_span": 5,
      "label_center": "[expr:[health:wifi_connected]==\"ON\"?[health:wifi_hostname;http://%s.local/|Hostname unavailable]:\"Connect to Wi-Fi\"]",
      "label_center_style": "font:@builtin;align:center;mode:wrap"
    },
    {
      "col": 1, "row": 2, "col_span": 3,
      "label_top": "[expr:[health:wifi_connected]==\"ON\"?\"Or open:\":\"Portal addresses will\"]",
      "label_center": "[expr:[health:wifi_connected]==\"ON\"?[health:wifi_ip;http://%s/|Address unavailable]:\"appear once connected.\"]",
      "label_top_style": "font:@builtin;align:center;mode:wrap",
      "label_center_style": "font:@builtin;align:center;mode:wrap"
    }
  ]
})json";
        char path[32];
        pad_config_path(0, path, sizeof(path));
        // Normal runtime loads the file into its PSRAM-first cache after setup.
        if (!storage_write_atomic(path, reinterpret_cast<const uint8_t*>(json), sizeof(json) - 1)) {
            LOGE("Welcome", "Cannot save initial welcome pad");
            return false;
        }
        LOGI("Welcome", "Created initial welcome pad");
    }

    // Keep deletion final even when the user has not saved device settings yet.
    static constexpr uint8_t done = 1;
    if (!storage_write_atomic(marker, &done, sizeof(done))) {
        LOGE("Welcome", "Cannot persist welcome pad provisioning marker");
        return false;
    }
    return true;
}

#endif
