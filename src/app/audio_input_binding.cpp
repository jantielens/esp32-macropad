#include "audio_input_binding.h"

#include "board_config.h"

#if HAS_AUDIO_INPUT && HAS_DISPLAY

#include "audio_input.h"
#include "binding_template.h"
#include "log_manager.h"

#include <stdio.h>
#include <string.h>

namespace {

enum AudioInputKey {
    AUDIO_INPUT_KEY_RMS,
    AUDIO_INPUT_KEY_PEAK,
    AUDIO_INPUT_KEY_ACTIVE,
};

struct AudioInputKeyDef {
    const char* name;
    AudioInputKey key;
};

constexpr AudioInputKeyDef kAudioInputKeys[] = {
    {"input.rms", AUDIO_INPUT_KEY_RMS},
    {"input.peak", AUDIO_INPUT_KEY_PEAK},
    {"input.active", AUDIO_INPUT_KEY_ACTIVE},
};

const AudioInputKeyDef* find_audio_input_key(const char* params) {
    if (!params) return nullptr;
    for (const AudioInputKeyDef& key : kAudioInputKeys) {
        if (strcmp(params, key.name) == 0) return &key;
    }
    return nullptr;
}

BindingResolverStatus audio_input_binding_resolve(const char* params, char* out, size_t out_len) {
    const AudioInputKeyDef* key = find_audio_input_key(params);
    if (!key) return BINDING_RESOLVER_UNKNOWN;
    if (!out || out_len == 0) return BINDING_RESOLVER_UNAVAILABLE;

    audio_input_meter_request();
    AudioInputMeterSnapshot snapshot = {};
    if (!audio_input_meter_get_snapshot(&snapshot)) return BINDING_RESOLVER_UNAVAILABLE;
    switch (key->key) {
        case AUDIO_INPUT_KEY_RMS:
            snprintf(out, out_len, "%u", snapshot.rms);
            break;
        case AUDIO_INPUT_KEY_PEAK:
            snprintf(out, out_len, "%u", snapshot.peak);
            break;
        case AUDIO_INPUT_KEY_ACTIVE:
            strlcpy(out, snapshot.active ? "true" : "false", out_len);
            break;
    }
    return BINDING_RESOLVER_RESOLVED;
}

void audio_input_binding_collect(const char*, void*) {}

uint8_t audio_input_binding_key_count() {
    return sizeof(kAudioInputKeys) / sizeof(kAudioInputKeys[0]);
}

const char* audio_input_binding_key_at(uint8_t index) {
    return index < audio_input_binding_key_count() ? kAudioInputKeys[index].name : nullptr;
}

const BindingParamDoc kAudioParams[] = {
    {"key", "Microphone meter key, see below."},
};

const BindingKeyDoc kAudioKeyDocs[] = {
    {"input.rms", "Microphone loudness, RMS level", nullptr},
    {"input.peak", "Microphone peak level", nullptr},
    {"input.active", "true while sound is above the activity threshold", nullptr},
};

const BindingExampleDoc kAudioExamples[] = {
    {"[audio:input.rms]", "Feed a gauge or bar chart with the input level."},
    {"[expr:[audio:input.active]==\"true\"?\"#22c55e\":\"#334155\"]", "Light up while someone speaks."},
};

const BindingSchemeDoc kAudioDoc = {
    "Device", "Live microphone level meter.",
    BINDING_DOC_LIST(kAudioParams), BINDING_DOC_LIST(kAudioKeyDocs), BINDING_DOC_LIST(kAudioExamples),
    nullptr, false, BINDING_DOC_NONE, nullptr, nullptr,
};

} // namespace

void audio_input_binding_init() {
    if (!binding_template_register("audio", audio_input_binding_resolve,
                                   audio_input_binding_collect,
                                   {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false,
                                    audio_input_binding_key_count, audio_input_binding_key_at,
                                    &kAudioDoc})) {
        LOGE("AudioInputBind", "Failed to register audio input binding scheme");
        return;
    }
}

#endif // HAS_AUDIO_INPUT && HAS_DISPLAY