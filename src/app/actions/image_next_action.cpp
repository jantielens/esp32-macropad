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
bool image_next_available() { return true; }
void describe_image_next(JsonObject& action) { action["group"] = "Image library"; action["label"] = "Next image"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kImageNextActionType, ACTION_TYPE_IMAGE_NEXT,
    nullptr, nullptr, dispatch_image_next, nullptr, describe_image_next, image_next_available);
}

#endif // HAS_IMAGE_LIBRARY