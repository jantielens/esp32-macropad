// Operating Mode component — nav-only registration

#include "component_registry.h"

static ComponentDef mode_component = {
    .id = "mode",
    .category = "device",
    .display_name = "Operating Mode",
    .nav_order = 40,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "mode"
};

REGISTER_COMPONENT(mode);
