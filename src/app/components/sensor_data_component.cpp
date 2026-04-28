// Sensor Data component — nav-only registration

#include "component_registry.h"

static ComponentDef sensor_data_component = {
    .id = "sensor-data",
    .category = "sensors",
    .display_name = "Sensor Data",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "sensor-data"
};

REGISTER_COMPONENT(sensor_data);
