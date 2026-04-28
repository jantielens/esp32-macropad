// Factory Reset component — nav-only registration

#include "component_registry.h"

static ComponentDef factory_reset_component = {
    .id = "factory-reset",
    .category = "device",
    .display_name = "Factory Reset",
    .nav_order = 40,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "factory-reset"
};

REGISTER_COMPONENT(factory_reset);
