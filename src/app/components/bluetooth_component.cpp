#include "component_registry.h"
static ComponentDef bluetooth_component = {
    .id = "bluetooth",
    .category = "connectivity",
    .display_name = "Bluetooth",
    .nav_order = 25,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "bluetooth",
    .portal_script = "/portal-bluetooth.js",
};
REGISTER_COMPONENT(bluetooth)
