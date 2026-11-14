#include "voice_clips_binding.h"

#if HAS_VOICE_CLIPS && HAS_DISPLAY
#include "binding_template.h"
#include "log_manager.h"
#include "voice_clips.h"
#include <stdio.h>
#include <string.h>

namespace {
const char* const kKeys[] = {"state", "duration_s", "last_clip", "error"};
const BindingParamDoc kParams[] = {{"key", "Recorder snapshot key, see below."}};
const BindingKeyDoc kKeyDocs[] = {
    {"state", "Current recording state", nullptr},
    {"duration_s", "Captured duration in seconds, excluding pauses", nullptr},
    {"last_clip", "ID of the last successfully saved clip", nullptr},
    {"error", "Most recent recorder error", nullptr},
};
const BindingExampleDoc kExamples[] = {
    {"[recorder:state]", "Show the recorder state."},
    {"[recorder:duration_s]", "Show the captured duration."},
};
const BindingSchemeDoc kRecorderDoc = {
    "Device", "Local voice clip recorder status.",
    BINDING_DOC_LIST(kParams), BINDING_DOC_LIST(kKeyDocs), BINDING_DOC_LIST(kExamples),
    nullptr, false, BINDING_DOC_NONE, nullptr, nullptr,
};
const BindingAuthoringKey kAuthoringKeys[] = {
    binding_authoring_text("state", "idle"),
    binding_authoring_integer("duration_s", "s", "0"),
    binding_authoring_text("last_clip", "0000000100000002"),
    binding_authoring_text("error", "No audio captured"),
};
const BindingAuthoringSpec kAuthoring = {BINDING_DOC_LIST(kAuthoringKeys)};

uint8_t key_count() { return sizeof(kKeys) / sizeof(kKeys[0]); }
const char* key_at(uint8_t index) { return index < key_count() ? kKeys[index] : nullptr; }
BindingResolverStatus resolve(const char* key, char* out, size_t size) {
    if (!key || !out || !size) return BINDING_RESOLVER_UNAVAILABLE;
    VoiceClipSnapshot snapshot = {};
    voice_clips_snapshot(&snapshot);
    if (strcmp(key, "state") == 0) strlcpy(out, voice_clips_state_name(snapshot.state), size);
    else if (strcmp(key, "duration_s") == 0)
        snprintf(out, size, "%llu", static_cast<unsigned long long>(snapshot.duration_ms / 1000));
    else if (strcmp(key, "last_clip") == 0) strlcpy(out, snapshot.last_clip, size);
    else if (strcmp(key, "error") == 0) strlcpy(out, snapshot.error, size);
    else return BINDING_RESOLVER_UNKNOWN;
    return BINDING_RESOLVER_RESOLVED;
}
}

void voice_clips_binding_init() {
    if (!binding_template_register("recorder", resolve, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false,
         key_count, key_at, &kRecorderDoc, &kAuthoring})) {
        LOGE("RecorderBind", "Failed to register recorder binding scheme");
    }
}
#endif
