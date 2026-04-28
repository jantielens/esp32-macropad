// Volume & Beep component — nav-only registration

#include "component_registry.h"

static ComponentDef volume_component = {
    .id = "volume",
    .category = "audio",
    .display_name = "Volume & Beep",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "volume"
};

REGISTER_COMPONENT(volume);
