#include "component_registry.h"

namespace {
void power_savings_activity(AsyncWebServerRequest* request) {
    request->send(204);
}

const ComponentAction power_savings_actions[] = {
    {"activity", HTTP_POST, power_savings_activity, nullptr},
};

ComponentDef power_savings_component = {
    .id = "power-savings",
    .category = "device",
    .display_name = "Power Savings",
    .nav_order = 45,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = power_savings_actions,
    .num_custom_actions = 1,
    .fragment_id = "power-savings",
    .portal_script = nullptr,
    .portal_style = nullptr,
};
}

REGISTER_COMPONENT(power_savings);
