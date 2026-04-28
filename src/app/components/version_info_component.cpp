// Version Info component — nav-only registration

#include "component_registry.h"

static ComponentDef version_info_component = {
    .id = "version-info",
    .category = "firmware",
    .display_name = "Version Info",
    .nav_order = 30,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "version-info"
};

REGISTER_COMPONENT(version_info);
