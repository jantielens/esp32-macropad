#include "board_config.h"

#if HAS_VOICE_NOTES
#include "voice_notes_transcription.h"
#include "voice_notes_transcription_request.h"
#include "voice_notes.h"
#include "voice_notes_file.h"
#include "bounded_psram_json_allocator.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"
#include "time_service.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <string.h>
#include <time.h>

namespace {
constexpr char TAG[] = "VoiceNotes";
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr uint32_t kPsramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

struct Job {
    VoiceNotesTranscriptionConfig config;
    char id[17];
    uint32_t ota_epoch;
};

SemaphoreHandle_t g_mutex = nullptr;
VoiceNotesTranscriptionConfig* g_config = nullptr;
VoiceNotesTranscriptionSnapshot g_snapshot = {};
TaskHandle_t g_worker = nullptr;
RtosTaskInternalAlloc g_worker_allocation = {};
Job* g_job = nullptr;

bool valid_id(const char* id) {
    if (!id || strlen(id) != 16) return false;
    for (size_t i = 0; i < 16; ++i)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    return true;
}

bool configured(const VoiceNotesTranscriptionConfig& config) {
    return config.base_url[0] && config.api_key[0] && config.model[0];
}

void set_state(VoiceNotesTranscriptionState state, const char* error = nullptr) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_snapshot.state = state;
    strlcpy(g_snapshot.error, error ? error : "", sizeof(g_snapshot.error));
    xSemaphoreGive(g_mutex);
    LOGI(TAG, "Transcription stage=%s", voice_notes_transcription_state_name(state));
    if (error) LOGE(TAG, "Transcription failed: %s", error);
}

bool save_transcript(const Job& job, const char* text) {
    BoundedPsramJsonAllocator allocator(heap_caps_get_free_size(kPsramCaps));
    JsonDocument document(&allocator);
    JsonObject transcript = document["transcript"].to<JsonObject>();
    transcript["text"] = text;
    transcript["model"] = job.config.model;
    transcript["created_at"] = time_service_ready() ? static_cast<uint32_t>(time(nullptr)) : 0;
    if (document.overflowed()) return false;
    const size_t length = measureJson(document);
    if (!length || length == SIZE_MAX) return false;
    auto* serialized = static_cast<uint8_t*>(heap_caps_malloc(length + 1, kPsramCaps));
    if (!serialized) return false;
    const size_t written = serializeJson(document, reinterpret_cast<char*>(serialized), length + 1);
    const bool saved = written == length && voice_notes_transcription_checkpoint(job.ota_epoch) &&
                       voice_notes_write_transcript(job.id, serialized, length);
    heap_caps_free(serialized);
    return saved;
}

void worker(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        Job* job = g_job;
        xSemaphoreGive(g_mutex);
        if (!job) continue;

        const uint32_t started = millis();
        LOGI(TAG, "Transcription start note=%s", job->id);
        const char* error = nullptr;
        File recording;
        bool opened = false;
        VoiceNotesRequestResult response = {};
        if (!voice_notes_transcription_checkpoint(job->ota_epoch)) {
            error = "Recording or update interrupted transcription";
        } else if (WiFi.status() != WL_CONNECTED) {
            error = "Wi-Fi disconnected";
        } else if (voice_notes_open(job->id, &recording) != VoiceNoteFileStatus::Ready) {
            error = "Recording unavailable";
        } else {
            opened = true;
            set_state(VoiceNotesTranscriptionState::Transcribing);
            if (!voice_notes_transcription_request(job->config, recording, job->ota_epoch, response)) {
                error = response.error ? response.error : "Transcription request failed";
            } else if (!voice_notes_transcription_checkpoint(job->ota_epoch)) {
                error = "Recording or update interrupted transcription";
            } else {
                set_state(VoiceNotesTranscriptionState::Saving);
                if (!save_transcript(*job, response.text))
                    error = "Could not save transcript";
            }
        }
        if (response.text) heap_caps_free(response.text);
        if (opened) voice_notes_close(&recording, job->id);
        LOGI(TAG, "Transcription note=%s status=%d elapsed_ms=%lu",
             job->id, response.status, static_cast<unsigned long>(millis() - started));
        set_state(error ? VoiceNotesTranscriptionState::Error : VoiceNotesTranscriptionState::Ready, error);
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        g_job = nullptr;
        xSemaphoreGive(g_mutex);
        heap_caps_free(job);
    }
}
}

bool voice_notes_normalize_endpoint(const char* input, char* output, size_t capacity) {
    if (!input || !output || !capacity) return false;
    const size_t length = strlen(input);
    if (!length) { output[0] = '\0'; return true; }
    if (strncmp(input, "https://", 8) != 0 || length <= 8) return false;
    const char* authority = input + 8;
    const char* slash = strchr(authority, '/');
    const size_t authority_length = slash ? static_cast<size_t>(slash - authority) : strlen(authority);
    if (!authority_length || (slash && strcmp(slash, "/") != 0)) return false;
    for (size_t i = 0; i < authority_length; ++i) {
        const unsigned char c = static_cast<unsigned char>(authority[i]);
        if (c <= 32 || c >= 127 || c == '@' || c == '?' || c == '#' || c == '\\') return false;
    }
    const char* end = authority + authority_length;
    const char* port = nullptr;
    if (*authority == '[') {
        const char* close = static_cast<const char*>(memchr(authority, ']', authority_length));
        if (!close || close == authority + 1) return false;
        if (close + 1 != end) {
            if (close[1] != ':') return false;
            port = close + 2;
        }
    } else {
        const char* colon = static_cast<const char*>(memchr(authority, ':', authority_length));
        const char* host_end = colon ? colon : end;
        if (host_end == authority) return false;
        for (const char* p = authority; p < host_end; ++p) {
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return false;
        }
        if (colon) port = colon + 1;
    }
    if (port) {
        if (port == end) return false;
        unsigned value = 0;
        for (const char* p = port; p < end; ++p) {
            if (*p < '0' || *p > '9' || value > 6553) return false;
            value = value * 10 + static_cast<unsigned>(*p - '0');
        }
        if (!value || value > 65535) return false;
    }
    const size_t normalized_length = length + (slash ? 0 : 1);
    if (normalized_length >= capacity) return false;
    memcpy(output, input, length);
    if (!slash) output[length] = '/';
    output[normalized_length] = '\0';
    return true;
}

bool voice_notes_transcription_configure(const VoiceNotesTranscriptionConfig& config) {
    if (!g_mutex) {
        g_mutex = xSemaphoreCreateMutex();
        if (!g_mutex) return false;
    }
    if (!g_config) {
        g_config = static_cast<VoiceNotesTranscriptionConfig*>(heap_caps_malloc(sizeof(*g_config), kPsramCaps));
        if (!g_config) return false;
    }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    *g_config = config;
    xSemaphoreGive(g_mutex);
    return true;
}

bool voice_notes_transcription_configured() {
    if (!g_mutex || !g_config) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const bool result = configured(*g_config);
    xSemaphoreGive(g_mutex);
    return result;
}

bool voice_notes_transcription_checkpoint(uint32_t ota_epoch) {
    VoiceNoteSnapshot snapshot = {};
    voice_notes_snapshot(&snapshot);
    return !snapshot.session_active && !ota_activity_is_busy() && ota_activity_epoch() == ota_epoch;
}

VoiceNotesTranscriptionStart voice_notes_transcription_start(const char* id) {
    if (!valid_id(id)) return VoiceNotesTranscriptionStart::InvalidId;
    if (!g_mutex || !g_config) return VoiceNotesTranscriptionStart::Unavailable;
    if (ota_activity_is_busy()) return VoiceNotesTranscriptionStart::Busy;
    VoiceNoteSnapshot recorder = {};
    voice_notes_snapshot(&recorder);
    if (recorder.session_active) return VoiceNotesTranscriptionStart::Busy;
    if (!voice_notes_exists(id)) return VoiceNotesTranscriptionStart::NotFound;
    if (WiFi.status() != WL_CONNECTED) return VoiceNotesTranscriptionStart::Offline;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (g_job) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesTranscriptionStart::Busy;
    }
    if (!configured(*g_config)) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesTranscriptionStart::NotConfigured;
    }
    Job* job = static_cast<Job*>(heap_caps_malloc(sizeof(Job), kPsramCaps));
    if (!job) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesTranscriptionStart::Unavailable;
    }
    if (!g_worker && !rtos_create_task_internal_stack(worker, "VoiceNotesTranscribe",
            kWorkerStackBytes, nullptr, 1, &g_worker, &g_worker_allocation)) {
        heap_caps_free(job);
        xSemaphoreGive(g_mutex);
        return VoiceNotesTranscriptionStart::Unavailable;
    }
    job->config = *g_config;
    strlcpy(job->id, id, sizeof(job->id));
    job->ota_epoch = ota_activity_epoch();
    g_job = job;
    g_snapshot.state = VoiceNotesTranscriptionState::Preparing;
    strlcpy(g_snapshot.note_id, id, sizeof(g_snapshot.note_id));
    g_snapshot.error[0] = '\0';
    xSemaphoreGive(g_mutex);
    xTaskNotifyGive(g_worker);
    return VoiceNotesTranscriptionStart::Accepted;
}

void voice_notes_transcription_snapshot(VoiceNotesTranscriptionSnapshot* out) {
    if (!out) return;
    if (!g_mutex) { *out = {}; return; }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    *out = g_snapshot;
    xSemaphoreGive(g_mutex);
}

const char* voice_notes_transcription_state_name(VoiceNotesTranscriptionState state) {
    switch (state) {
        case VoiceNotesTranscriptionState::Idle: return "idle";
        case VoiceNotesTranscriptionState::Preparing: return "preparing";
        case VoiceNotesTranscriptionState::Transcribing: return "transcribing";
        case VoiceNotesTranscriptionState::Saving: return "saving";
        case VoiceNotesTranscriptionState::Ready: return "ready";
        case VoiceNotesTranscriptionState::Error: return "error";
    }
    return "error";
}
#endif
