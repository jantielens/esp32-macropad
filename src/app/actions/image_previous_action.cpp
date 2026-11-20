#include "action_registry.h"

#if HAS_IMAGE_LIBRARY

#include "image_library_runtime.h"
#include "log_manager.h"

namespace {
ActionResult dispatch_image_previous_operation(const ActionResolvedArgument*, const char* label, uint32_t) {
    char path[IMAGE_LIBRARY_PATH_MAX_LEN];
    if (image_library_runtime_previous(path, sizeof(path))) return ACTION_COMPLETE;
    LOGW("Action", "%s image_previous: no image", label);
    return ACTION_FAILED;
}
const ActionOperationDef kImagePreviousOperations[] = {
    {"image.previous", "Previous image", nullptr, 0, dispatch_image_previous_operation},
};
bool image_previous_available() { return true; }
void describe_image_previous(JsonObject& action) { action["group"] = "Image library"; action["label"] = "Previous image"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kImagePreviousActionType, ACTION_TYPE_IMAGE_PREVIOUS, describe_image_previous, image_previous_available, ACTION_EXECUTION_SYNC, true, false, kImagePreviousOperations, 1);
}

#endif // HAS_IMAGE_LIBRARY