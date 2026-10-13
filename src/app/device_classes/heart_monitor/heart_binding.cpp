#include "heart_recorder.h"

#if IS_HEART_MONITOR
#include "binding_template.h"
#include <stdio.h>
#include <string.h>

namespace heart_monitor {
namespace {
const BindingKeyDoc key_docs[] = {
    {"state", "idle, recording, awaiting_save, saving, saved, or error", nullptr},
    {"elapsed_s", "Current or latest capture duration in seconds", nullptr},
    {"samples", "Number of stored red/IR sample pairs", nullptr},
    {"error", "Latest error reason; unavailable when there is no error", nullptr},
    {"filename", "Last successfully exported file, which may contain an interrupted capture", nullptr},
    {"unsaved", "true while raw samples are retained in PSRAM", nullptr},
    {"bpm", "Experimental IR pulse-rate estimate; unavailable unless recording with usable signal", nullptr},
    {"quality", "measuring, usable, or weak_signal while recording; heuristic, not clinical confidence", nullptr},
    {"pulse", "1 for 200 ms after an accepted detected peak, otherwise 0; not a predicted rhythm", nullptr},
    {"waveform", "Latest causal baseline-removed IR sample; unavailable during startup or when not recording", nullptr},
};

uint8_t key_count() { return sizeof(key_docs) / sizeof(key_docs[0]); }
const char* key_at(uint8_t index) { return index < key_count() ? key_docs[index].key : nullptr; }

BindingResolverStatus resolve(const char* params, char* out, size_t size) {
    if (!params) return BINDING_RESOLVER_UNKNOWN;
    if (!out || !size) return BINDING_RESOLVER_UNAVAILABLE;
    const Snapshot snapshot = heart_recorder_snapshot();
    if (strcmp(params, "state") == 0) snprintf(out, size, "%s", state_name(snapshot.state));
    else if (strcmp(params, "elapsed_s") == 0) snprintf(out, size, "%.1f", snapshot.elapsed_ms / 1000.0);
    else if (strcmp(params, "samples") == 0) snprintf(out, size, "%lu", static_cast<unsigned long>(snapshot.samples));
    else if (strcmp(params, "unsaved") == 0) snprintf(out, size, "%s", snapshot.retained ? "true" : "false");
    else if (strcmp(params, "pulse") == 0) snprintf(out, size, "%u", snapshot.state == State::Recording && snapshot.live.pulse ? 1U : 0U);
    else if (strcmp(params, "bpm") == 0 || strcmp(params, "quality") == 0 || strcmp(params, "waveform") == 0) {
        if (snapshot.state != State::Recording) return BINDING_RESOLVER_UNAVAILABLE;
        if (strcmp(params, "quality") == 0) snprintf(out, size, "%s", bpm_state_name(snapshot.live.state));
        else if (strcmp(params, "bpm") == 0) {
            if (snapshot.live.state != BpmState::Usable) return BINDING_RESOLVER_UNAVAILABLE;
            snprintf(out, size, "%.0f", snapshot.live.bpm);
        } else {
            if (!snapshot.live.waveform_valid) return BINDING_RESOLVER_UNAVAILABLE;
            snprintf(out, size, "%.3f", snapshot.live.waveform);
        }
    }
    else if (strcmp(params, "error") == 0 || strcmp(params, "filename") == 0) {
        const char* value = strcmp(params, "error") == 0 ? snapshot.error : snapshot.filename;
        if (!value[0]) return BINDING_RESOLVER_UNAVAILABLE;
        snprintf(out, size, "%s", value);
    } else return BINDING_RESOLVER_UNKNOWN;
    return BINDING_RESOLVER_RESOLVED;
}

const BindingParamDoc params[] = {{"key", "Recorder state, elapsed_s, samples, error, filename, unsaved, bpm, quality, pulse, or waveform."}};
const BindingExampleDoc examples[] = {
    {"[heart:state]", "Current raw recording state."},
    {"[heart:elapsed_s] s", "Elapsed capture duration."},
    {"[heart:error|None]", "Latest error or a placeholder."},
    {"[heart:filename|No recording saved]", "Last exported recording path."},
    {"[heart:bpm|--] BPM", "Experimental live IR rate."},
    {"[heart:quality|idle]", "Live signal state."},
    {"[heart:pulse]", "Numeric ON/OFF signal for icon color expressions."},
    {"[heart:waveform]", "Numeric filtered IR value for a sparkline."},
};
const BindingSchemeDoc kHeartDoc = {
    "Heart Monitor", "Local raw optical recording and experimental live IR pulse estimates; no SpO2 estimates.",
    BINDING_DOC_LIST(params), BINDING_DOC_LIST(key_docs), BINDING_DOC_LIST(examples),
    nullptr, false, BINDING_DOC_NONE,
    "Quality and peak detection are heuristic, not medical measurements. Pulse starts only with usable signal. Sparkline polling does not retain all 100 Hz samples and carries values across gaps; hide it when not recording. The filename is the last saved capture, not proof of a successful current session. Unsaved PSRAM data is lost on reboot.", nullptr,
};
}
}

void heart_binding_init() {
    using namespace heart_monitor;
    binding_template_register("heart", resolve, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false, key_count, key_at, &kHeartDoc});
}
#endif