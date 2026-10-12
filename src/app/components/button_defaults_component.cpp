// Pad and Button Defaults component — migrated from web_portal_button_defaults.cpp

#include "component_registry.h"
#include "board_config.h"
#include "button_defaults.h"
#include "log_manager.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>

static void button_defaults_write_firmware_defaults(JsonObject defaults) {
    defaults["default_pad_bg_color"] = DEFAULT_PAD_BG_COLOR;
    defaults["bg_color"] = DEFAULT_BUTTON_BG_COLOR;
    defaults["fg_color"] = DEFAULT_BUTTON_FG_COLOR;
    defaults["border_color"] = DEFAULT_BUTTON_BORDER_COLOR;
    defaults["border_width"] = DEFAULT_BUTTON_BORDER_WIDTH;
    defaults["corner_radius"] = DEFAULT_BUTTON_CORNER_RADIUS;
    defaults["content_pad"] = DEFAULT_BUTTON_CONTENT_PAD;
    defaults["button_shadow_enabled"] = DEFAULT_BUTTON_SHADOW_ENABLED;
    defaults["button_shadow_type"] = DEFAULT_BUTTON_SHADOW_TYPE == BUTTON_SHADOW_TYPE_DROP ? "drop" : "opaque";
    defaults["button_shadow_color_mode"] = DEFAULT_BUTTON_SHADOW_COLOR_MODE == BUTTON_SHADOW_COLOR_DARKEN_BACKGROUND
        ? "darken_background" : "fixed";
    defaults["button_shadow_color"] = DEFAULT_BUTTON_SHADOW_COLOR;
    defaults["button_shadow_darken_pct"] = DEFAULT_BUTTON_SHADOW_DARKEN_PCT;
    defaults["button_shadow_offset_x_px"] = DEFAULT_BUTTON_SHADOW_OFFSET_X_PX;
    defaults["button_shadow_offset_y_px"] = DEFAULT_BUTTON_SHADOW_OFFSET_Y_PX;
    defaults["button_shadow_drop_blur_px"] = DEFAULT_BUTTON_SHADOW_DROP_BLUR_PX;
    defaults["button_spacing_px"] = DEFAULT_BUTTON_SPACING_PX;
    defaults["pad_inset_top_px"] = DEFAULT_PAD_INSET_TOP_PX;
    defaults["pad_inset_right_px"] = DEFAULT_PAD_INSET_RIGHT_PX;
    defaults["pad_inset_bottom_px"] = DEFAULT_PAD_INSET_BOTTOM_PX;
    defaults["pad_inset_left_px"] = DEFAULT_PAD_INSET_LEFT_PX;
    defaults["pixel_shift_distance_px"] = DEFAULT_PIXEL_SHIFT_DISTANCE;
    defaults["widget_series_color"] = DEFAULT_WIDGET_SERIES_COLOR;
    defaults["widget_series_color_2"] = DEFAULT_WIDGET_SERIES_COLOR_2;
    defaults["widget_series_color_3"] = DEFAULT_WIDGET_SERIES_COLOR_3;
    defaults["widget_series_color_4"] = DEFAULT_WIDGET_SERIES_COLOR_4;
    defaults["widget_track_color"] = DEFAULT_WIDGET_TRACK_COLOR;
    defaults["widget_indicator_color"] = DEFAULT_WIDGET_INDICATOR_COLOR;
    defaults["widget_tick_color"] = DEFAULT_WIDGET_TICK_COLOR;
    defaults["widget_zone_color"] = DEFAULT_WIDGET_ZONE_COLOR;
    defaults["widget_reference_color"] = DEFAULT_WIDGET_REFERENCE_COLOR;
    defaults["widget_table_bg_color"] = DEFAULT_WIDGET_TABLE_BG_COLOR;
    defaults["widget_table_fg_color"] = DEFAULT_WIDGET_TABLE_FG_COLOR;
    defaults["widget_table_header_color"] = DEFAULT_WIDGET_TABLE_HEADER_COLOR;
}

static void button_defaults_get_config(AsyncWebServerRequest *request) {
    WebPortalTiming timing;
    auto doc = make_psram_json_doc(4096);
    button_defaults_load_overrides(*doc);
    timing.mark("defaults");
    button_defaults_write_firmware_defaults((*doc)["firmware_defaults"].to<JsonObject>());

    timing.mark("defaults_json");
    web_portal_send_json_chunked(request, doc, 200, &timing);
}

static void button_defaults_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, button_defaults_save_raw);
}

static ComponentDef button_defaults_component = {
    .id = "button-defaults",
    .category = "pads",
    .display_name = "Pad and Button Defaults",
    .nav_order = 20,
    .get_config = button_defaults_get_config,
    .save_config = nullptr,
    .save_config_body = button_defaults_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "button-defaults"
};

REGISTER_COMPONENT(button_defaults);
