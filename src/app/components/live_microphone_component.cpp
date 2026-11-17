#include "component_registry.h"
#include "web_portal_live_microphone.h"

static const ComponentAction live_microphone_actions[] = {
    {"stream", HTTP_GET, handleLiveMicrophoneStream, nullptr},
};

static ComponentDef live_microphone_component = {
    .id = "live-microphone",
    .category = "audio",
    .display_name = "Live Microphone",
    .nav_order = 45,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = live_microphone_actions,
    .num_custom_actions = 1,
    .fragment_id = "live-microphone",
    .portal_script = nullptr,
    .portal_style = nullptr,
};
REGISTER_COMPONENT(live_microphone)
