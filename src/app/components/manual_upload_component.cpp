// Manual Upload component — nav-only registration

#include "component_registry.h"

static ComponentDef manual_upload_component = {
    .id = "manual-upload",
    .category = "firmware",
    .display_name = "Manual Upload",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "manual-upload"
};

REGISTER_COMPONENT(manual_upload);
