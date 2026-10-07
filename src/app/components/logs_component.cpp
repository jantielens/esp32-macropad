#include "component_registry.h"

static ComponentDef logs_component = {
    .id = "logs",
    .category = "device",
    .display_name = "Logs",
    .nav_order = 35,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "logs",
    .portal_script = "/portal-logs.js",
    .portal_style = nullptr,
};
REGISTER_COMPONENT(logs);