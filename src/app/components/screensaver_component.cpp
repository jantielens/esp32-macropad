// Screen Saver component — nav-only registration

#include "component_registry.h"

static ComponentDef screensaver_component = {
    .id = "screensaver",
    .category = "display",
    .display_name = "Screen Saver",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "screensaver"
};

REGISTER_COMPONENT(screensaver);
