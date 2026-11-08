#include "component_registry.h"

static ComponentDef external_data_component = {
    .id = "external-data",
    .category = "connectivity",
    .display_name = "External Data",
    .nav_order = 40,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "external-data",
    .portal_script = "/portal-external-data.js",
    .portal_style = nullptr,
};
REGISTER_COMPONENT(external_data);
