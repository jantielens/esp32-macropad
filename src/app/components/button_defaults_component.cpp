// Button Defaults component — migrated from web_portal_button_defaults.cpp

#include "component_registry.h"
#include "board_config.h"
#include "button_defaults.h"
#include "log_manager.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"

#include <ArduinoJson.h>

static void button_defaults_get_config(AsyncWebServerRequest *request) {
    const ButtonDefaults* d = button_defaults_get();

    StaticJsonDocument<1024> doc;
    if (d->bg_color[0])          doc["bg_color"]          = d->bg_color;
    if (d->fg_color[0])          doc["fg_color"]          = d->fg_color;
    if (d->border_color[0])      doc["border_color"]      = d->border_color;
    if (d->border_width[0])      doc["border_width"]      = d->border_width;
    if (d->corner_radius[0])     doc["corner_radius"]     = d->corner_radius;
    if (d->label_top_style[0])   doc["label_top_style"]   = d->label_top_style;
    if (d->label_center_style[0]) doc["label_center_style"] = d->label_center_style;
    if (d->label_bottom_style[0]) doc["label_bottom_style"] = d->label_bottom_style;
    if (d->icon_position == ICON_POS_LEFT)   doc["icon_position"] = "left";
    else if (d->icon_position == ICON_POS_CENTER) doc["icon_position"] = "center";

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

static void button_defaults_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    // Only handle final chunk (small payload, arrives in one piece)
    if (index + len < total) return;

    if (total > 4096) {
        request->send(413, "application/json", "{\"error\":\"payload too large\"}");
        return;
    }

    bool ok = button_defaults_save_raw(data, total);
    if (ok) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(500, "application/json", "{\"error\":\"save failed\"}");
    }
}

static ComponentDef button_defaults_component = {
    .id = "button-defaults",
    .category = "pads",
    .display_name = "Button Defaults",
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
