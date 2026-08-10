#include "voice.h"

#if IS_VOICE_ASSISTANT

#include "binding_template.h"

#include <ArduinoJson.h>
#include <string.h>

namespace {
bool voice_binding_resolve(const char* params, char* out, size_t out_len) {
    VoiceSnapshot snapshot = {};
    voice_get_snapshot(&snapshot);
    if (strcmp(params, "status") == 0) {
        strlcpy(out, voice_status_name(snapshot.status), out_len);
        return true;
    }
    if (strcmp(params, "text") == 0) {
        strlcpy(out, snapshot.text, out_len);
        return snapshot.text[0] != '\0';
    }
    return false;
}

void voice_binding_collect(const char*, void*) {}

const char* voice_binding_validate(const char* params) {
    return strcmp(params, "status") == 0 || strcmp(params, "text") == 0
        ? nullptr : "stt key must be status or text";
}

#if HAS_MCP
void voice_binding_describe(void* out_json) {
    JsonObject& out = *static_cast<JsonObject*>(out_json);
    out["syntax"] = "[stt:status|text]";
    out["example"] = "[stt:text]";
    out["keys"] = "status, text";
    out["read_only"] = true;
}
#endif
} // namespace

void voice_binding_init() {
    binding_template_register("stt", voice_binding_resolve, voice_binding_collect);
#if HAS_MCP
    binding_template_set_scheme_describe("stt", voice_binding_describe);
    binding_template_set_scheme_validate("stt", voice_binding_validate);
#endif
}

#endif // IS_VOICE_ASSISTANT