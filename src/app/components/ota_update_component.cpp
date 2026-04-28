// Online Update component — nav-only registration

#include "component_registry.h"

static ComponentDef ota_update_component = {
    .id = "ota-update",
    .category = "firmware",
    .display_name = "Online Update",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "ota-update"
};

REGISTER_COMPONENT(ota_update);
