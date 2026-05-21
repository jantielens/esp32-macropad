// Setup wizard component — AP-mode first-boot onboarding fragment.
//
// Registered in the "device" category but gated by handlePortalNav so it
// appears ONLY in AP mode (and the other device-category items are hidden
// while it is the only entry). In STA mode it is suppressed entirely; users
// who want to change wifi/network/auth/device-name later use the regular
// fragments.

#include "component_registry.h"

static ComponentDef setup_component = {
    .id = "setup",
    .category = "device",
    .display_name = "Setup",
    .nav_order = 1,  // First in the device category (only entry in AP mode)
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "setup"
};

REGISTER_COMPONENT(setup);
