// Pad Editor component — nav-only registration

#include "component_registry.h"

static ComponentDef pad_editor_component = {
    .id = "pad-editor",
    .category = "pads",
    .display_name = "Pad Editor",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "pad-editor"
};

REGISTER_COMPONENT(pad_editor);
