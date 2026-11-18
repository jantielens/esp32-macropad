#include "action_registry.h"

#if HAS_IMAGE_LIBRARY

#include "image_library_runtime.h"
#include "log_manager.h"

namespace {
ActionResult dispatch_image_next(const ButtonAction&, const char* label, uint32_t) {
    char path[IMAGE_LIBRARY_PATH_MAX_LEN];
    if (!image_library_runtime_next(path, sizeof(path))) LOGW("Action", "%s image_next: no image", label);
    return ACTION_COMPLETE;
}
ActionResult dispatch_image_next_operation(const ActionResolvedArgument*, const char* label, uint32_t) {
    char path[IMAGE_LIBRARY_PATH_MAX_LEN];
    if (image_library_runtime_next(path, sizeof(path))) return ACTION_COMPLETE;
    LOGW("Action", "%s image_next: no image", label);
    return ACTION_FAILED;
}
const ActionOperationDef kImageNextOperations[] = {
    {"image.next", "Next image", nullptr, 0, dispatch_image_next_operation},
};
bool image_next_available() { return true; }
void describe_image_next(JsonObject& action) { action["group"] = "Image library"; action["label"] = "Next image"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kImageNextActionType, ACTION_TYPE_IMAGE_NEXT, nullptr, nullptr, dispatch_image_next, nullptr, describe_image_next, image_next_available, nullptr, ACTION_EXECUTION_SYNC, true, true, false, nullptr, kImageNextOperations, 1);
}

#endif // HAS_IMAGE_LIBRARY