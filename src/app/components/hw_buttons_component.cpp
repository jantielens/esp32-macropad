// Hardware Button Actions component — exposes per-button tap/hold action lists.

#include "board_config.h"

#if HAS_BUTTON

#include "action_parse.h"
#include "component_registry.h"
#include "hw_button_config.h"
#include "log_manager.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>

static void hw_buttons_get_config(AsyncWebServerRequest *request) {
    auto doc = make_psram_json_doc(4096);
    (*doc)["action_format_version"] = 1;
    JsonArray buttons = doc->createNestedArray("buttons");

    if (!hw_button_config_to_json(buttons) || doc->overflowed()) {
        web_portal_send_json_error(request, 409, "Hardware button actions unavailable or incompatible; recreate the document");
        return;
    }

    web_portal_send_json_chunked(request, doc);
}

static void hw_buttons_save_config(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, hw_button_config_save_raw);
}

static ComponentDef hw_buttons_component = {
    .id = "hw-buttons",
    .category = "actions",
    .display_name = "Hardware Buttons",
    .nav_order = 15,  // between swipe (10) and boot (20)
    .get_config = hw_buttons_get_config,
    .save_config = nullptr,
    .save_config_body = hw_buttons_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "hw-buttons"
};

REGISTER_COMPONENT(hw_buttons);

#endif  // HAS_BUTTON
