// Thresholds component — nav-only registration (API wiring pending backend implementation)

#include "component_registry.h"

static ComponentDef thresholds_component = {
    .id = "thresholds",
    .category = "sensors",
    .display_name = "Thresholds",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "thresholds"
};

REGISTER_COMPONENT(thresholds);
