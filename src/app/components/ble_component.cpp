// BLE Keyboard component — nav-only registration

#include "component_registry.h"

static ComponentDef ble_component = {
    .id = "ble",
    .category = "connectivity",
    .display_name = "BLE Keyboard",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "ble"
};

REGISTER_COMPONENT(ble);
