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
#include "voice_wav.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <new>
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
    uint64_t recording_generation;
};

enum class Work : uint8_t { None, Single, Manual, Automatic };
SemaphoreHandle_t g_mutex = nullptr;
VoiceNotesTranscriptionConfig* g_config = nullptr;
VoiceNotesTranscriptionSnapshot g_snapshot = {};
TaskHandle_t g_worker = nullptr;
RtosTaskInternalAlloc g_worker_allocation = {};
Job* g_job = nullptr;
Work g_work = Work::None;
bool g_initialized = false;
bool g_pending = false;
bool g_refresh = true;
bool g_setup_blocked = false;
unsigned g_failures = 0;
uint64_t g_retry_until = 0;
uint64_t g_cooldown_until = 0;
VoiceNotesProcessingSnapshot g_processing = {};

uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
uint64_t deadline(uint64_t now, uint64_t delay) {
    return delay > UINT64_MAX - now ? UINT64_MAX : now + delay;
}
uint64_t retry_delay(unsigned failures) {
    static const uint32_t seconds[] = {30, 60, 120, 240, 480, 900};
    const unsigned index = failures > 0 ? failures - 1 : 0;
    return static_cast<uint64_t>(seconds[index < 6 ? index : 5]) * 1000;
}

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

enum class SaveOutcome : uint8_t { Saved, Interrupted, Failed };
SaveOutcome save_transcript(const Job& job, const char* text) {
    BoundedPsramJsonAllocator allocator(heap_caps_get_free_size(kPsramCaps));
    JsonDocument document(&allocator);
    JsonObject transcript = document["transcript"].to<JsonObject>();
    transcript["text"] = text;
    transcript["model"] = job.config.model;
    transcript["created_at"] = time_service_ready() ? static_cast<uint32_t>(time(nullptr)) : 0;
    if (document.overflowed()) return SaveOutcome::Failed;
    const size_t length = measureJson(document);
    if (!length || length == SIZE_MAX) return SaveOutcome::Failed;
    auto* serialized = static_cast<uint8_t*>(heap_caps_malloc(length + 1, kPsramCaps));
    if (!serialized) return SaveOutcome::Failed;
    const size_t written = serializeJson(document, reinterpret_cast<char*>(serialized), length + 1);
    SaveOutcome outcome = SaveOutcome::Failed;
    if (written == length) {
        if (!voice_notes_transcription_checkpoint(job.ota_epoch, job.recording_generation))
            outcome = SaveOutcome::Interrupted;
        else if (voice_notes_write_transcript(job.id, serialized, length))
            outcome = SaveOutcome::Saved;
    }
    heap_caps_free(serialized);
    return outcome;
}

VoiceNotesRequestOutcome operation(Job& job, bool replace, uint64_t& cooldown) {
        const uint32_t started = millis();
        LOGI(TAG, "Transcription start note=%s", job.id);
        const char* error = nullptr;
        File recording;
        bool opened = false;
        VoiceNotesRequestResult response = {};
        VoiceNotesRequestOutcome outcome = VoiceNotesRequestOutcome::SharedFailure;
        if (!voice_notes_transcription_checkpoint(job.ota_epoch, job.recording_generation)) {
            error = "Recording or update interrupted transcription";
            outcome = VoiceNotesRequestOutcome::Interrupted;
        } else if (WiFi.status() != WL_CONNECTED) {
            error = "Wi-Fi disconnected";
        } else {
            const VoiceNoteFileStatus status = voice_notes_open(job.id, &recording);
            if (status == VoiceNoteFileStatus::NotFound) {
                set_state(VoiceNotesTranscriptionState::Idle);
                return VoiceNotesRequestOutcome::Success;
            }
            if (status != VoiceNoteFileStatus::Ready) {
                error = "Recording unavailable";
            } else {
                opened = true;
                if (!replace && voice_notes_has_transcript(job.id)) {
                    voice_notes_close(&recording, job.id);
                    set_state(VoiceNotesTranscriptionState::Idle);
                    return VoiceNotesRequestOutcome::Success;
                }
                uint8_t header[VOICE_WAV_HEADER_BYTES];
                VoiceWavInfo info = {};
                if (recording.read(header, sizeof(header)) != sizeof(header) ||
                    !voice_wav_read_header(header, sizeof(header), recording.size(), &info) ||
                    !recording.seek(0)) {
                    error = "Invalid recording";
                    outcome = VoiceNotesRequestOutcome::NoteFailure;
                } else {
                    set_state(VoiceNotesTranscriptionState::Transcribing);
                    if (!voice_notes_transcription_request(job.config, recording, job.ota_epoch,
                                                           job.recording_generation, response)) {
                        error = response.error ? response.error : "Transcription request failed";
                        outcome = response.outcome;
                        cooldown = response.cooldown_ms;
                    } else if (!voice_notes_transcription_checkpoint(job.ota_epoch, job.recording_generation)) {
                        error = "Recording or update interrupted transcription";
                        outcome = VoiceNotesRequestOutcome::Interrupted;
                    } else {
                        set_state(VoiceNotesTranscriptionState::Saving);
                        const SaveOutcome saved = save_transcript(job, response.text);
                        if (saved == SaveOutcome::Interrupted) {
                            error = "Recording or update interrupted transcription";
                            outcome = VoiceNotesRequestOutcome::Interrupted;
                        } else if (saved == SaveOutcome::Failed) error = "Could not save transcript";
                        else outcome = VoiceNotesRequestOutcome::Success;
                    }
                }
            }
        }
        if (response.text) heap_caps_free(response.text);
        if (opened) voice_notes_close(&recording, job.id);
        LOGI(TAG, "Transcription note=%s status=%d elapsed_ms=%lu",
             job.id, response.status, static_cast<unsigned long>(millis() - started));
        set_state(error ? VoiceNotesTranscriptionState::Error : VoiceNotesTranscriptionState::Ready, error);
        return outcome;
}

void update_processing() {
    VoiceNoteSnapshot recorder = {};
    voice_notes_snapshot(&recorder);
    const bool update = ota_activity_is_busy();
    const bool online = WiFi.status() == WL_CONNECTED;
    const uint64_t now = now_ms();
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_processing = {};
    if (g_work != Work::None) g_processing.state = VoiceNotesProcessingState::Running;
    else if (!g_config || !g_config->auto_transcribe || !g_pending) {}
    else if (!configured(*g_config) || g_setup_blocked) {
        g_processing.state = VoiceNotesProcessingState::Blocked;
        g_processing.reason = VoiceNotesProcessingReason::Configuration;
    } else {
        g_processing.state = VoiceNotesProcessingState::Waiting;
        if (recorder.session_active) g_processing.reason = VoiceNotesProcessingReason::Recording;
        else if (update) g_processing.reason = VoiceNotesProcessingReason::Update;
        else if (!online) g_processing.reason = VoiceNotesProcessingReason::Offline;
        else if (now < g_retry_until || now < g_cooldown_until) g_processing.reason = VoiceNotesProcessingReason::Retry;
    }
    xSemaphoreGive(g_mutex);
}

void worker(void*) {
    uint64_t next_scan = 0;
    for (;;) {
        VoiceNoteSnapshot recorder = {};
        voice_notes_snapshot(&recorder);
        const bool discovery_available = !recorder.session_active && !ota_activity_is_busy();
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        Work work = g_work;
        Job* job = g_job;
        const bool scan = discovery_available && work == Work::None && g_config && g_config->auto_transcribe &&
                          (g_refresh || now_ms() >= next_scan);
        if (scan) g_refresh = false;
        xSemaphoreGive(g_mutex);
        if (scan) {
            VoiceNoteCursor cursor = {}, candidate = {};
            const VoiceNoteCandidate result = voice_notes_next_candidate(cursor, &candidate);
            xSemaphoreTake(g_mutex, portMAX_DELAY);
            if (result == VoiceNoteCandidate::Interrupted) {
                g_refresh = true;
            } else {
                g_pending = result != VoiceNoteCandidate::None;
                next_scan = deadline(now_ms(), 5000);
            }
            if (result == VoiceNoteCandidate::Error) {
                if (g_failures < 6) ++g_failures;
                g_retry_until = deadline(now_ms(), retry_delay(g_failures));
            }
            xSemaphoreGive(g_mutex);
        }
        update_processing();
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        if (g_work == Work::None && g_pending && g_config && g_config->auto_transcribe &&
            g_processing.state == VoiceNotesProcessingState::Waiting &&
            g_processing.reason == VoiceNotesProcessingReason::None) {
            job = static_cast<Job*>(heap_caps_malloc(sizeof(Job), kPsramCaps));
            if (job) {
                new (job) Job{};
                VoiceNoteSnapshot recorder = {};
                voice_notes_snapshot(&recorder);
                if (!recorder.session_active && !ota_activity_is_busy() && WiFi.status() == WL_CONNECTED) {
                    job->config = *g_config;
                    job->ota_epoch = ota_activity_epoch();
                    job->recording_generation = recorder.recording_generation;
                    job->id[0] = '\0';
                    g_job = job;
                    g_work = Work::Automatic;
                    g_processing = {VoiceNotesProcessingState::Running, VoiceNotesProcessingReason::None};
                } else {
                    heap_caps_free(job);
                    job = nullptr;
                }
            } else {
                LOGE(TAG, "Cannot allocate processing job");
                if (g_failures < 6) ++g_failures;
                g_retry_until = deadline(now_ms(), retry_delay(g_failures));
            }
        }
        work = g_work;
        job = g_job;
        xSemaphoreGive(g_mutex);
        if (work == Work::None || !job) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }
        bool failed = false;
        bool interrupted = false;
        bool completed = false;
        uint64_t scheduled_delay = 0;
        VoiceNotesRequestOutcome last = VoiceNotesRequestOutcome::Success;
        uint64_t cooldown = 0;
        VoiceNoteCursor cursor = {};
        for (;;) {
            if (work != Work::Single &&
                !voice_notes_transcription_checkpoint(job->ota_epoch, job->recording_generation)) {
                interrupted = true;
                break;
            }
            if (work != Work::Single) {
                VoiceNoteCursor next = {};
                const VoiceNoteCandidate selection = voice_notes_next_candidate(cursor, &next);
                if (selection == VoiceNoteCandidate::Interrupted) {
                    interrupted = true;
                    break;
                }
                if (selection == VoiceNoteCandidate::Error) {
                    failed = true;
                    last = VoiceNotesRequestOutcome::SharedFailure;
                    break;
                }
                if (selection == VoiceNoteCandidate::None) { completed = true; break; }
                cursor = next;
                strlcpy(job->id, next.id, sizeof(job->id));
            }
            xSemaphoreTake(g_mutex, portMAX_DELAY);
            g_snapshot.state = VoiceNotesTranscriptionState::Preparing;
            strlcpy(g_snapshot.note_id, job->id, sizeof(g_snapshot.note_id));
            g_snapshot.error[0] = '\0';
            xSemaphoreGive(g_mutex);
            last = operation(*job, work == Work::Single, cooldown);
            if (last == VoiceNotesRequestOutcome::Interrupted) { interrupted = true; break; }
            if (last != VoiceNotesRequestOutcome::Success) failed = true;
            if (last != VoiceNotesRequestOutcome::Success && last != VoiceNotesRequestOutcome::NoteFailure) break;
            if (work == Work::Single || !voice_notes_transcription_checkpoint(job->ota_epoch, job->recording_generation)) {
                interrupted = work != Work::Single;
                break;
            }
            xSemaphoreTake(g_mutex, portMAX_DELAY);
            const bool enabled = g_config && g_config->auto_transcribe;
            if (g_config) job->config = *g_config;
            xSemaphoreGive(g_mutex);
            if (work == Work::Automatic && !enabled) break;
            if (WiFi.status() != WL_CONNECTED) { failed = true; last = VoiceNotesRequestOutcome::SharedFailure; break; }
        }
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        if (last == VoiceNotesRequestOutcome::SetupFailure) g_setup_blocked = true;
        if (failed && !interrupted) {
            if (g_failures < 6) ++g_failures;
            const uint64_t delay = retry_delay(g_failures);
            scheduled_delay = delay;
            g_retry_until = deadline(now_ms(), delay);
            if (last == VoiceNotesRequestOutcome::RateLimited)
                g_cooldown_until = deadline(now_ms(), cooldown ? cooldown : delay);
        } else if (!failed && !interrupted && completed && work != Work::Single) {
            g_failures = 0;
            g_retry_until = 0;
        }
        g_job = nullptr;
        g_work = Work::None;
        g_refresh = true;
        xSemaphoreGive(g_mutex);
        if (scheduled_delay)
            LOGW(TAG, "Processing pass failed; retry_ms=%llu cooldown_ms=%llu",
                 static_cast<unsigned long long>(scheduled_delay),
                 static_cast<unsigned long long>(last == VoiceNotesRequestOutcome::RateLimited
                                                 ? (cooldown ? cooldown : scheduled_delay) : 0));
        if (last == VoiceNotesRequestOutcome::SetupFailure) LOGW(TAG, "Automatic transcription blocked by settings");
        if (interrupted) LOGI(TAG, "Processing pass interrupted by recording or update");
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
        new (g_config) VoiceNotesTranscriptionConfig{};
    }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const bool changed = strcmp(g_config->base_url, config.base_url) ||
                         strcmp(g_config->api_key, config.api_key) ||
                         strcmp(g_config->model, config.model);
    *g_config = config;
    if (changed) {
        g_setup_blocked = false;
        g_failures = 0;
        g_retry_until = 0;
    }
    g_refresh = true;
    bool ready = true;
    if (g_initialized && config.auto_transcribe && !g_worker)
        ready = rtos_create_task_internal_stack(worker, "VoiceNotesTranscribe",
            kWorkerStackBytes, nullptr, 1, &g_worker, &g_worker_allocation);
    TaskHandle_t worker_handle = g_worker;
    xSemaphoreGive(g_mutex);
    if (worker_handle) xTaskNotifyGive(worker_handle);
    if (!ready) LOGE(TAG, "Cannot create transcription worker");
    return ready;
}

bool voice_notes_transcription_configured() {
    if (!g_mutex || !g_config) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const bool result = configured(*g_config);
    xSemaphoreGive(g_mutex);
    return result;
}

bool voice_notes_transcription_checkpoint(uint32_t ota_epoch, uint64_t generation) {
    VoiceNoteSnapshot snapshot = {};
    voice_notes_snapshot(&snapshot);
    return !snapshot.session_active && snapshot.recording_generation == generation &&
           !ota_activity_is_busy() && ota_activity_epoch() == ota_epoch;
}

bool voice_notes_processing_init() {
    if (!g_mutex || !g_config) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_initialized = true;
    bool ready = true;
    if (g_config->auto_transcribe && !g_worker)
        ready = rtos_create_task_internal_stack(worker, "VoiceNotesTranscribe",
            kWorkerStackBytes, nullptr, 1, &g_worker, &g_worker_allocation);
    TaskHandle_t worker_handle = g_worker;
    xSemaphoreGive(g_mutex);
    if (worker_handle) xTaskNotifyGive(worker_handle);
    if (!ready) LOGE(TAG, "Cannot create transcription worker");
    return ready;
}

static VoiceNotesTranscriptionStart admit(const char* id, Work work) {
    if (!g_mutex || !g_config || !g_initialized) return VoiceNotesTranscriptionStart::Unavailable;
    if (ota_activity_is_busy()) return VoiceNotesTranscriptionStart::Busy;
    VoiceNoteSnapshot recorder = {};
    voice_notes_snapshot(&recorder);
    if (recorder.session_active) return VoiceNotesTranscriptionStart::Busy;
    if (WiFi.status() != WL_CONNECTED) return VoiceNotesTranscriptionStart::Offline;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (g_work != Work::None) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesTranscriptionStart::Busy;
    }
    if (!configured(*g_config)) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesTranscriptionStart::NotConfigured;
    }
    if (now_ms() < g_cooldown_until) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesTranscriptionStart::Cooldown;
    }
    Job* job = static_cast<Job*>(heap_caps_malloc(sizeof(Job), kPsramCaps));
    if (!job) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesTranscriptionStart::Unavailable;
    }
    new (job) Job{};
    if (!g_worker && !rtos_create_task_internal_stack(worker, "VoiceNotesTranscribe",
            kWorkerStackBytes, nullptr, 1, &g_worker, &g_worker_allocation)) {
        heap_caps_free(job);
        xSemaphoreGive(g_mutex);
        return VoiceNotesTranscriptionStart::Unavailable;
    }
    job->config = *g_config;
    strlcpy(job->id, id ? id : "", sizeof(job->id));
    job->ota_epoch = ota_activity_epoch();
    job->recording_generation = recorder.recording_generation;
    g_job = job;
    g_work = work;
    g_processing = {VoiceNotesProcessingState::Running, VoiceNotesProcessingReason::None};
    g_setup_blocked = false;
    g_retry_until = 0;
    if (work == Work::Single) {
        g_snapshot.state = VoiceNotesTranscriptionState::Preparing;
        strlcpy(g_snapshot.note_id, id, sizeof(g_snapshot.note_id));
        g_snapshot.error[0] = '\0';
    }
    xSemaphoreGive(g_mutex);
    xTaskNotifyGive(g_worker);
    return VoiceNotesTranscriptionStart::Accepted;
}

VoiceNotesTranscriptionStart voice_notes_transcription_start(const char* id) {
    if (!valid_id(id)) return VoiceNotesTranscriptionStart::InvalidId;
    if (!voice_notes_exists(id)) return VoiceNotesTranscriptionStart::NotFound;
    return admit(id, Work::Single);
}

VoiceNotesTranscriptionStart voice_notes_processing_start() { return admit(nullptr, Work::Manual); }

void voice_notes_processing_snapshot(VoiceNotesProcessingSnapshot* out) {
    if (!out) return;
    if (!g_mutex) { *out = {}; return; }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    *out = g_processing;
    xSemaphoreGive(g_mutex);
}

const char* voice_notes_processing_state_name(VoiceNotesProcessingState state) {
    switch (state) {
        case VoiceNotesProcessingState::Idle: return "idle";
        case VoiceNotesProcessingState::Waiting: return "waiting";
        case VoiceNotesProcessingState::Running: return "running";
        case VoiceNotesProcessingState::Blocked: return "blocked";
    }
    return "idle";
}

const char* voice_notes_processing_reason_name(VoiceNotesProcessingReason reason) {
    switch (reason) {
        case VoiceNotesProcessingReason::None: return "none";
        case VoiceNotesProcessingReason::Offline: return "offline";
        case VoiceNotesProcessingReason::Recording: return "recording";
        case VoiceNotesProcessingReason::Update: return "update";
        case VoiceNotesProcessingReason::Retry: return "retry";
        case VoiceNotesProcessingReason::Configuration: return "configuration";
    }
    return "none";
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
