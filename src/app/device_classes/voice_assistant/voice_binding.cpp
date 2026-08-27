#include "voice.h"

#if IS_VOICE_ASSISTANT

#include "binding_template.h"

#include <string.h>

namespace {
enum VoiceBindingKey {
    VOICE_BINDING_STATUS,
    VOICE_BINDING_TEXT,
};

struct VoiceBindingKeyDef {
    const char* name;
    VoiceBindingKey key;
};

constexpr VoiceBindingKeyDef kVoiceBindingKeys[] = {
    {"status", VOICE_BINDING_STATUS},
    {"text", VOICE_BINDING_TEXT},
};

const VoiceBindingKeyDef* find_voice_binding_key(const char* params) {
    if (!params) return nullptr;
    for (const VoiceBindingKeyDef& key : kVoiceBindingKeys) {
        if (strcmp(params, key.name) == 0) return &key;
    }
    return nullptr;
}

BindingResolverStatus voice_binding_resolve(const char* params, char* out, size_t out_len) {
    const VoiceBindingKeyDef* key = find_voice_binding_key(params);
    if (!key) return BINDING_RESOLVER_UNKNOWN;
    if (!out || out_len == 0) return BINDING_RESOLVER_UNAVAILABLE;
    VoiceSnapshot snapshot = {};
    voice_get_snapshot(&snapshot);
    switch (key->key) {
        case VOICE_BINDING_STATUS:
            strlcpy(out, voice_status_name(snapshot.status), out_len);
            return BINDING_RESOLVER_RESOLVED;
        case VOICE_BINDING_TEXT:
            strlcpy(out, snapshot.text, out_len);
                return snapshot.text[0] ? BINDING_RESOLVER_RESOLVED : BINDING_RESOLVER_UNAVAILABLE;
    }
            return BINDING_RESOLVER_UNKNOWN;
}

void voice_binding_collect(const char*, void*) {}

uint8_t voice_binding_key_count() {
    return sizeof(kVoiceBindingKeys) / sizeof(kVoiceBindingKeys[0]);
}

const char* voice_binding_key_at(uint8_t index) {
    return index < voice_binding_key_count() ? kVoiceBindingKeys[index].name : nullptr;
}
} // namespace

void voice_binding_init() {
    binding_template_register("stt", voice_binding_resolve, voice_binding_collect,
                              {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false,
                               voice_binding_key_count, voice_binding_key_at});
}

#endif // IS_VOICE_ASSISTANT