// WiFi component — nav-only registration

#include "component_registry.h"

static ComponentDef wifi_component = {
    .id = "wifi",
    .category = "device",
    .display_name = "WiFi",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "wifi"
};

REGISTER_COMPONENT(wifi);
