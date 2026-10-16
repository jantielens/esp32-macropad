#include "component_registry.h"
#include "device_dialog.h"
#include "dialog_appearance.h"
#include "log_manager.h"
#include "psram_json_allocator.h"

namespace {
bool dialog_appearance_test_raw(const uint8_t* data, size_t length) {
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    const auto error = deserializeJson(doc, data, length);
    const char* type = doc["type"] | "";
    if (error || (strcmp(type, "notification") && strcmp(type, "confirmation")) ||
        !doc["appearance"].is<JsonObjectConst>()) {
        LOGW("Dialog", "Invalid appearance test request");
        return false;
    }
    DialogTemplates templates;
    BasicJsonDocument<PsramJsonAllocator> appearance_doc(4096);
    appearance_doc[type] = doc["appearance"];
    const char* validation = appearance_doc.overflowed() ? "appearance too large" :
        dialog_appearance_read(appearance_doc.as<JsonObjectConst>(), templates);
    if (validation) {
        LOGW("Dialog", "Invalid test appearance: %s", validation);
        return false;
    }
    const bool confirmation = !strcmp(type, "confirmation");
    if (!device_dialog_test(confirmation, confirmation ? templates.confirmation : templates.notification)) {
        LOGW("Dialog", "Appearance test could not be displayed");
        return false;
    }
    return true;
}

void dialog_appearance_test_post(AsyncWebServerRequest* request, uint8_t* data,
                               size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, dialog_appearance_test_raw, 2048,
        "{\"success\":false,\"error\":\"Dialog test failed: invalid appearance, unavailable input, display, or resources\"}");
}

const ComponentAction dialog_appearance_actions[] = {
    {"test", HTTP_POST, nullptr, dialog_appearance_test_post},
};
}

static ComponentDef dialog_appearance_component = {
    .id = "dialog-appearance",
    .category = "pads",
    .display_name = "Dialog appearance",
    .nav_order = 25,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = dialog_appearance_actions,
    .num_custom_actions = sizeof(dialog_appearance_actions) / sizeof(dialog_appearance_actions[0]),
    .fragment_id = "dialog-appearance"
};

REGISTER_COMPONENT(dialog_appearance);
