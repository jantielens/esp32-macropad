// Network component — nav-only registration

#include "component_registry.h"

static ComponentDef network_component = {
    .id = "network",
    .category = "device",
    .display_name = "Network",
    .nav_order = 30,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "network"
};

REGISTER_COMPONENT(network);
