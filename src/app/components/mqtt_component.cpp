// MQTT component — nav-only registration

#include "component_registry.h"

static ComponentDef mqtt_component = {
    .id = "mqtt",
    .category = "connectivity",
    .display_name = "MQTT",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "mqtt"
};

REGISTER_COMPONENT(mqtt);
