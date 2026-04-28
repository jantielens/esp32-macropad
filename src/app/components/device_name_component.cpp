// Device Name component — nav-only registration

#include "component_registry.h"

static ComponentDef device_name_component = {
    .id = "device-name",
    .category = "device",
    .display_name = "Device Name",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "device-name"
};

REGISTER_COMPONENT(device_name);
