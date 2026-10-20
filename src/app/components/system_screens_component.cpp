#include "component_registry.h"

static ComponentDef system_screens_component = {
    .id = "system-screens",
    .category = "display",
    .display_name = "System Screens",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "system-screens"
};

REGISTER_COMPONENT(system_screens);
