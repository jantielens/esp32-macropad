#include "action_registry.h"

#if HAS_IMAGE_LIBRARY

#include "image_library_runtime.h"
#include "log_manager.h"

namespace {
ActionResult dispatch_image_previous(const ButtonAction&, const char* label, uint32_t) {
    char path[IMAGE_LIBRARY_PATH_MAX_LEN];
    if (!image_library_runtime_previous(path, sizeof(path))) LOGW("Action", "%s image_previous: no image", label);
    return ACTION_COMPLETE;
}
bool image_previous_available() { return true; }
void describe_image_previous(JsonObject& action) { action["group"] = "Image library"; action["label"] = "Previous image"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kImagePreviousActionType, ACTION_TYPE_IMAGE_PREVIOUS,
    nullptr, nullptr, dispatch_image_previous, nullptr, describe_image_previous, image_previous_available);
}

#endif // HAS_IMAGE_LIBRARY