#include "audio_input_binding.h"

#include "board_config.h"

#if HAS_AUDIO_INPUT && HAS_DISPLAY

#include "audio_input.h"
#include "binding_template.h"
#include "log_manager.h"

#include <stdio.h>
#include <string.h>

#if HAS_MCP
#include <ArduinoJson.h>
#endif

namespace {

constexpr const char* kAudioInputKeys[] = {
    "input.rms",
    "input.peak",
    "input.active",
};

bool is_audio_input_key(const char* params) {
    if (!params) return false;
    for (const char* key : kAudioInputKeys) {
        if (strcmp(params, key) == 0) return true;
    }
    return false;
}

bool audio_input_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!is_audio_input_key(params) || !out || out_len == 0) return false;

    audio_input_meter_request();
    AudioInputMeterSnapshot snapshot = {};
    if (!audio_input_meter_get_snapshot(&snapshot)) return false;
    if (strcmp(params, "input.rms") == 0) {
        snprintf(out, out_len, "%u", snapshot.rms);
    } else if (strcmp(params, "input.peak") == 0) {
        snprintf(out, out_len, "%u", snapshot.peak);
    } else {
        strlcpy(out, snapshot.active ? "true" : "false", out_len);
    }
    return true;
}

void audio_input_binding_collect(const char*, void*) {}

const char* audio_input_binding_validate(const char* params) {
    if (!params || !params[0]) return "audio key is required";
    if (is_audio_input_key(params)) return nullptr;
    return "audio key must be input.rms, input.peak, or input.active";
}

#if HAS_MCP
void audio_input_binding_describe(void* out_json) {
    JsonObject& out = *static_cast<JsonObject*>(out_json);
    out["syntax"] = "[audio:input.rms]";
    out["example"] = "[audio:input.rms]";
    out["keys"] = "input.rms, input.peak, input.active";
    out["read_only"] = true;
    out["input"] = "RMS and peak are microphone levels from 0 to 100; sampling runs only while the binding is resolved.";
}
#endif

} // namespace

void audio_input_binding_init() {
    if (!binding_template_register("audio", audio_input_binding_resolve,
                                   audio_input_binding_collect)) {
        LOGE("AudioInputBind", "Failed to register audio input binding scheme");
        return;
    }
#if HAS_MCP
    binding_template_set_scheme_describe("audio", audio_input_binding_describe);
    binding_template_set_scheme_validate("audio", audio_input_binding_validate);
#endif
}

#endif // HAS_AUDIO_INPUT && HAS_DISPLAY