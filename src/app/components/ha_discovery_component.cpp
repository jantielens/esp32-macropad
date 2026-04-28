// Home Assistant component — nav-only registration

#include "component_registry.h"

static ComponentDef ha_discovery_component = {
    .id = "ha-discovery",
    .category = "connectivity",
    .display_name = "Home Assistant",
    .nav_order = 30,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "ha-discovery"
};

REGISTER_COMPONENT(ha_discovery);
