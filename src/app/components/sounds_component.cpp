// Sound Files component — nav-only registration

#include "component_registry.h"

static ComponentDef sounds_component = {
    .id = "sounds",
    .category = "audio",
    .display_name = "Sound Files",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "sounds"
};

REGISTER_COMPONENT(sounds);
