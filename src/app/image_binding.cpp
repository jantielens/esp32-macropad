#include "image_binding.h"

#if HAS_IMAGE_LIBRARY

#include "binding_template.h"
#include "image_library_runtime.h"
#include "log_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <stdio.h>
#include <string.h>

namespace {
portMUX_TYPE s_error_lock = portMUX_INITIALIZER_UNLOCKED;
char s_last_error[128] = "ok";

BindingResolverStatus image_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!params) {
        snprintf(out, out_len, "ERR:bad_image");
        return BINDING_RESOLVER_UNKNOWN;
    }
    if (strcmp(params, "error") == 0) {
        portENTER_CRITICAL(&s_error_lock);
        strlcpy(out, s_last_error, out_len);
        portEXIT_CRITICAL(&s_error_lock);
        return BINDING_RESOLVER_RESOLVED;
    }
    if (strcmp(params, "current") != 0) {
        snprintf(out, out_len, "ERR:bad_image");
        return BINDING_RESOLVER_UNKNOWN;
    }
    return image_library_runtime_current(out, out_len)
        ? BINDING_RESOLVER_RESOLVED : BINDING_RESOLVER_UNAVAILABLE;
}
uint8_t image_binding_key_count() { return 2; }
const char* image_binding_key_at(uint8_t index) {
    static constexpr const char* kKeys[] = {"current", "error"};
    return index < 2 ? kKeys[index] : nullptr;
}
}

void image_binding_set_error(const char* stage, const char* detail) {
    portENTER_CRITICAL(&s_error_lock);
    snprintf(s_last_error, sizeof(s_last_error), "%s:%s", stage ? stage : "unknown",
             detail && detail[0] ? detail : "failed");
    portEXIT_CRITICAL(&s_error_lock);
}

void image_binding_init() {
    if (!binding_template_register("image", image_binding_resolve, nullptr,
                                   {1, 1, 1, 1, BINDING_VALIDATION_STANDARD, false,
                                    image_binding_key_count, image_binding_key_at})) {
        LOGE("ImageBind", "Failed to register image binding scheme");
    }
}

#endif // HAS_IMAGE_LIBRARY