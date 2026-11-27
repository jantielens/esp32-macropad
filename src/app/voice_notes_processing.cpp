#include "board_config.h"

#if HAS_VOICE_NOTES
#include "voice_notes_processing.h"
#include "voice_notes_transcription_request.h"
#include "voice_notes_enrichment_request.h"
#include "voice_notes.h"
#include "voice_notes_file.h"
#include "bounded_psram_json_allocator.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"
#include "time_service.h"
#include "voice_wav.h"
#include "action_continuation.h"
#include "keyboard_hid.h"
#if HAS_DISPLAY && HAS_PSRAM
#include "binding_value_json.h"
#include "loaded_data.h"
#endif
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <ctype.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {
constexpr char TAG[] = "VoiceNotes";
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr uint32_t kPsramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

enum class Work : uint8_t { None, Automatic, Manual, Single, Retranscribe, Regenerate };
enum class Service : uint8_t { Transcription, Enrichment };

struct Job {
    VoiceNotesConfig config;
    char id[17];
    uint32_t ota_epoch;
    uint64_t recording_generation;
};
struct Retry {
    bool setup_blocked = false;
    unsigned failures = 0;
    uint64_t retry_until = 0;
    uint64_t cooldown_until = 0;
};
struct PassService {
    bool attempted = false;
    bool failed = false;
    bool succeeded = false;
    bool unavailable = false;
    bool setup = false;
    bool limited = false;
    uint64_t cooldown = 0;
    unsigned saved = 0;
};

SemaphoreHandle_t g_mutex = nullptr;
VoiceNotesConfig* g_config = nullptr;
VoiceNotesTranscriptionSnapshot g_transcription = {};
VoiceNotesEnrichmentSnapshot g_enrichment = {};
VoiceNotesProcessingSnapshot g_processing = {};
TaskHandle_t g_worker = nullptr;
RtosTaskInternalAlloc g_worker_allocation = {};
Job* g_job = nullptr;
Work g_work = Work::None;
Retry g_retry[2];
bool g_initialized = false;
bool g_pending_tx = false;
bool g_pending_llm = false;
bool g_refresh = true;
char g_last_saved[32] = {};
#if HAS_DISPLAY && HAS_PSRAM
LoadedDataSnapshot* g_latest_transcript = nullptr;

LoadedDataSnapshot* acquire_latest_transcript() {
    if (!g_mutex) return nullptr;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    LoadedDataSnapshot* snapshot = g_latest_transcript;
    if (snapshot) snapshot->retain();
    xSemaphoreGive(g_mutex);
    return snapshot;
}

void publish_latest_transcript(const char* id, const char* text) {
    LoadedDataSnapshot* snapshot = LoadedDataSnapshot::create();
    if (snapshot) {
        snapshot->document()["id"] = JsonString(id, JsonString::Copied);
        snapshot->document()["text"] = JsonString(text, JsonString::Copied);
        if (snapshot->document().overflowed()) {
            snapshot->release();
            snapshot = nullptr;
        }
    }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    LoadedDataSnapshot* previous = g_latest_transcript;
    g_latest_transcript = snapshot;
    xSemaphoreGive(g_mutex);
    if (previous) previous->release();
    if (!snapshot) LOGE(TAG, "Cannot publish latest transcript note=%s", id);
    display_manager_request_binding_refresh();
}
#endif

void type_transcript_if_requested(const char* id, const VoiceNotesRequestResult& response) {
    if (!voice_notes_take_typing_request(id)) return;
    if (response.no_speech || !response.text || !response.text[0]) {
        LOGW(TAG, "Transcript saved; typing skipped note=%s reason=no_speech", id);
        return;
    }
#if (HAS_DISPLAY || HAS_BUTTON) && (HAS_BLE_HID || HAS_USB_HID)
    uint32_t token = 0;
    if (!action_continuation_begin(nullptr, 0, "Voice note typing", &token)) {
        LOGW(TAG, "Transcript saved; typing failed note=%s reason=continuations_busy", id);
        return;
    }
    if (!keyboard_hid_request_text(response.text, token)) {
        action_continuation_release(token);
        LOGW(TAG, "Transcript saved; typing failed note=%s keyboard=%s", id, keyboard_hid_status());
        return;
    }
    action_continuation_mark_pending(token);
    LOGI(TAG, "Transcript saved; typing started note=%s", id);
#else
    LOGW(TAG, "Transcript saved; typing failed note=%s reason=keyboard_unavailable", id);
#endif
}

uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
uint64_t deadline(uint64_t now, uint64_t delay) {
    return delay > UINT64_MAX - now ? UINT64_MAX : now + delay;
}
uint64_t retry_delay(unsigned failures) {
    static const uint32_t seconds[] = {30, 60, 300};
    const unsigned index = failures ? failures - 1 : 0;
    return static_cast<uint64_t>(seconds[index < 3 ? index : 2]) * 1000;
}
void reset_retry(Retry& retry) {
    retry.failures = 0;
    retry.retry_until = 0;
}
void record_failure(Retry& retry) {
    if (retry.failures < 4) ++retry.failures;
    retry.retry_until = deadline(now_ms(), retry_delay(retry.failures));
}
void retry_status(const Retry& retry, char* out, size_t size) {
    if (retry.failures >= 4) strlcpy(out, "Retries halted", size);
    else if (retry.failures) snprintf(out, size, "Retry %u of 3", retry.failures);
    else out[0] = '\0';
}
bool configured(const VoiceNotesServiceConfig& service) {
    return service.endpoint[0] && service.api_key[0] && service.deployment[0];
}
bool service_changed(const VoiceNotesServiceConfig& a, const VoiceNotesServiceConfig& b) {
    return strcmp(a.endpoint, b.endpoint) || strcmp(a.api_key, b.api_key) ||
           strcmp(a.deployment, b.deployment);
}
const char* work_name(Work work) {
    switch (work) {
        case Work::Automatic: return "automatic";
        case Work::Manual: return "manual";
        case Work::Single: return "single";
        case Work::Retranscribe: return "retranscribe";
        case Work::Regenerate: return "regenerate";
        case Work::None: return "none";
    }
    return "none";
}
const char* outcome_name(VoiceNotesRequestOutcome outcome) {
    switch (outcome) {
        case VoiceNotesRequestOutcome::Success: return "saved";
        case VoiceNotesRequestOutcome::Interrupted: return "interrupted";
        case VoiceNotesRequestOutcome::Skip: return "skipped";
        case VoiceNotesRequestOutcome::SetupFailure: return "configuration";
        case VoiceNotesRequestOutcome::RateLimited: return "rate_limited";
        case VoiceNotesRequestOutcome::ResourceFailure: return "resource_failure";
        case VoiceNotesRequestOutcome::NoteFailure: return "note_failure";
        case VoiceNotesRequestOutcome::SharedFailure: return "service_failure";
    }
    return "unknown";
}
void log_result(const char* service, const Job& job, VoiceNotesRequestOutcome outcome,
                int status, uint32_t started, const char* error) {
    const auto level = outcome == VoiceNotesRequestOutcome::Success ||
                       outcome == VoiceNotesRequestOutcome::Interrupted
        ? LOG_LEVEL_INFO : outcome == VoiceNotesRequestOutcome::ResourceFailure
        ? LOG_LEVEL_ERROR : LOG_LEVEL_WARN;
    LOG_AT(level, TAG, "%s note=%s result=%s http=%d elapsed_ms=%lu reason=%.64s",
              service, job.id, outcome_name(outcome), status,
              static_cast<unsigned long>(millis() - started), error ? error : "none");
}
bool nonblank(JsonVariantConst value) {
    if (!value.is<const char*>()) return false;
    JsonString text = value.as<JsonString>();
    if (!text.c_str() || strlen(text.c_str()) != text.size()) return false;
    for (size_t i = 0; i < text.size(); ++i)
        if (!isspace(static_cast<unsigned char>(text.c_str()[i]))) return true;
    return false;
}
VoiceNotesProcessingService service_name(bool tx, bool llm) {
    if (tx && llm) return VoiceNotesProcessingService::Both;
    if (tx) return VoiceNotesProcessingService::Transcription;
    if (llm) return VoiceNotesProcessingService::Enrichment;
    return VoiceNotesProcessingService::None;
}
void set_active(Service service, const char* id) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_processing.service = service == Service::Transcription
        ? VoiceNotesProcessingService::Transcription : VoiceNotesProcessingService::Enrichment;
    if (service == Service::Transcription) {
        g_transcription.state = VoiceNotesTranscriptionState::Preparing;
        strlcpy(g_transcription.note_id, id, sizeof(g_transcription.note_id));
        g_transcription.error[0] = '\0';
    } else {
        g_enrichment.state = VoiceNotesEnrichmentState::Preparing;
        strlcpy(g_enrichment.note_id, id, sizeof(g_enrichment.note_id));
        g_enrichment.error[0] = '\0';
    }
    xSemaphoreGive(g_mutex);
}
void set_tx_state(VoiceNotesTranscriptionState state, const char* error = nullptr) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_transcription.state = state;
    strlcpy(g_transcription.error, error ? error : "", sizeof(g_transcription.error));
    xSemaphoreGive(g_mutex);
}
void set_enrichment_state(VoiceNotesEnrichmentState state, const char* error = nullptr) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_enrichment.state = state;
    strlcpy(g_enrichment.error, error ? error : "", sizeof(g_enrichment.error));
    xSemaphoreGive(g_mutex);
}

void pause_processing() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_processing.service = VoiceNotesProcessingService::None;
    xSemaphoreGive(g_mutex);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

enum class SaveOutcome : uint8_t { Saved, Interrupted, Failed };
SaveOutcome save_document(const Job& job, VoiceNoteArtifact artifact, JsonDocument& document) {
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
        else if (voice_notes_write_artifact(job.id, artifact, serialized, length))
            outcome = SaveOutcome::Saved;
    }
    heap_caps_free(serialized);
    return outcome;
}

VoiceNotesRequestOutcome transcribe(Job& job, bool replace, uint64_t& cooldown) {
    const uint32_t started = millis();
    LOGI(TAG, "Transcription start note=%s", job.id);
    const char* error = nullptr;
    File recording;
    bool opened = false;
    VoiceNotesRequestResult response = {};
    VoiceNotesRequestOutcome outcome = VoiceNotesRequestOutcome::ResourceFailure;
    if (!voice_notes_transcription_checkpoint(job.ota_epoch, job.recording_generation)) {
        error = "Recording or update interrupted transcription";
        outcome = VoiceNotesRequestOutcome::Interrupted;
    } else if (WiFi.status() != WL_CONNECTED) {
        error = "Wi-Fi disconnected";
        outcome = VoiceNotesRequestOutcome::SharedFailure;
    } else {
        const VoiceNoteFileStatus status = voice_notes_open(job.id, &recording);
        if (status == VoiceNoteFileStatus::NotFound) {
            LOGD(TAG, "Transcription skipped note=%s reason=removed", job.id);
            return VoiceNotesRequestOutcome::Skip;
        }
        if (status != VoiceNoteFileStatus::Ready) error = "Recording unavailable";
        else {
            opened = true;
            if (!replace && voice_notes_has_artifact(job.id, VoiceNoteArtifact::Transcript)) {
                voice_notes_close(&recording, job.id);
                LOGD(TAG, "Transcription skipped note=%s reason=already_saved", job.id);
                return VoiceNotesRequestOutcome::Skip;
            }
            uint8_t header[VOICE_WAV_HEADER_BYTES];
            VoiceWavInfo info = {};
            if (recording.read(header, sizeof(header)) != sizeof(header) ||
                !voice_wav_read_header(header, sizeof(header), recording.size(), &info) ||
                !recording.seek(0)) {
                error = "Invalid recording";
                outcome = VoiceNotesRequestOutcome::NoteFailure;
            } else {
                set_tx_state(VoiceNotesTranscriptionState::Transcribing);
                if (!voice_notes_transcription_request(job.config.transcription, recording, job.ota_epoch,
                                                       job.recording_generation, response)) {
                    error = response.error ? response.error : "Transcription request failed";
                    outcome = response.outcome;
                    cooldown = response.cooldown_ms;
                } else if (!voice_notes_transcription_checkpoint(job.ota_epoch, job.recording_generation)) {
                    error = "Recording or update interrupted transcription";
                    outcome = VoiceNotesRequestOutcome::Interrupted;
                } else {
                    set_tx_state(VoiceNotesTranscriptionState::Saving);
                    LOGI(TAG, "Transcript saving note=%s http=%d", job.id, response.status);
                    BoundedPsramJsonAllocator allocator(heap_caps_get_free_size(kPsramCaps));
                    JsonDocument document(&allocator);
                    JsonObject transcript = document["transcript"].to<JsonObject>();
                    transcript["text"] = response.text;
                    transcript["model"] = job.config.transcription.deployment;
                    transcript["created_at"] = time_service_ready() ? static_cast<uint32_t>(time(nullptr)) : 0;
                    const SaveOutcome saved = save_document(job, VoiceNoteArtifact::Transcript, document);
                    if (saved == SaveOutcome::Interrupted) {
                        error = "Recording or update interrupted transcription";
                        outcome = VoiceNotesRequestOutcome::Interrupted;
                    } else if (saved == SaveOutcome::Failed) error = "Could not save transcript";
                    else {
                        outcome = VoiceNotesRequestOutcome::Success;
#if HAS_DISPLAY && HAS_PSRAM
                        publish_latest_transcript(job.id, response.text);
#endif
                        type_transcript_if_requested(job.id, response);
                    }
                }
            }
        }
    }
    if (response.text) heap_caps_free(response.text);
    if (opened) voice_notes_close(&recording, job.id);
    log_result("Transcription", job, outcome, response.status, started, error);
    set_tx_state(error ? VoiceNotesTranscriptionState::Error : VoiceNotesTranscriptionState::Ready, error);
    return outcome;
}

VoiceNotesRequestOutcome enrich(Job& job, bool replace, uint64_t& cooldown) {
    const uint32_t started = millis();
    LOGI(TAG, "Enrichment start note=%s", job.id);
    const char* error = nullptr;
    VoiceNotesRequestOutcome outcome = VoiceNotesRequestOutcome::ResourceFailure;
    VoiceNotesRequestResult response = {};
    File recording;
    bool opened = false;
    if (!voice_notes_transcription_checkpoint(job.ota_epoch, job.recording_generation)) {
        error = "Recording or update interrupted enrichment";
        outcome = VoiceNotesRequestOutcome::Interrupted;
    } else if (WiFi.status() != WL_CONNECTED) {
        error = "Wi-Fi disconnected";
        outcome = VoiceNotesRequestOutcome::SharedFailure;
    } else {
        const VoiceNoteFileStatus status = voice_notes_open(job.id, &recording);
        if (status == VoiceNoteFileStatus::NotFound) {
            LOGD(TAG, "Enrichment skipped note=%s reason=removed", job.id);
            return VoiceNotesRequestOutcome::Skip;
        }
        if (status != VoiceNoteFileStatus::Ready) error = "Recording unavailable";
        else {
            opened = true;
            if (!replace && voice_notes_has_artifact(job.id, VoiceNoteArtifact::Enrichment)) {
                voice_notes_close(&recording, job.id);
                LOGD(TAG, "Enrichment skipped note=%s reason=already_saved", job.id);
                return VoiceNotesRequestOutcome::Skip;
            }
            File transcript_file;
            const VoiceNoteFileStatus transcript_status =
                voice_notes_open_artifact(job.id, VoiceNoteArtifact::Transcript, &transcript_file);
            if (transcript_status != VoiceNoteFileStatus::Ready) {
                error = "Saved transcript unavailable";
                outcome = transcript_status == VoiceNoteFileStatus::NotFound
                    ? VoiceNotesRequestOutcome::NoteFailure : VoiceNotesRequestOutcome::ResourceFailure;
            } else {
                BoundedPsramJsonAllocator allocator(heap_caps_get_free_size(kPsramCaps));
                JsonDocument content(&allocator);
                {
                    JsonDocument transcript(&allocator);
                    const DeserializationError parse = deserializeJson(transcript, transcript_file);
                    transcript_file.close();
                    if (parse || transcript.overflowed()) {
                        error = "Saved transcript is unreadable";
                        outcome = parse == DeserializationError::NoMemory || transcript.overflowed()
                            ? VoiceNotesRequestOutcome::ResourceFailure : VoiceNotesRequestOutcome::NoteFailure;
                    } else if (!nonblank(transcript["transcript"]["text"])) {
                        error = "Saved transcript is invalid";
                        outcome = VoiceNotesRequestOutcome::NoteFailure;
                    } else {
                        set_enrichment_state(VoiceNotesEnrichmentState::Generating);
                        if (!voice_notes_enrichment_request(job.config.llm, transcript["transcript"]["text"],
                                                            job.ota_epoch, job.recording_generation, content, response)) {
                            error = response.error ? response.error : "Enrichment request failed";
                            outcome = response.outcome;
                            cooldown = response.cooldown_ms;
                        } else outcome = VoiceNotesRequestOutcome::Success;
                    }
                }
                if (!error && outcome == VoiceNotesRequestOutcome::Success) {
                    set_enrichment_state(VoiceNotesEnrichmentState::Saving);
                    LOGI(TAG, "Enrichment saving note=%s http=%d", job.id, response.status);
                    JsonDocument document(&allocator);
                    JsonObject saved = document["enrichment"].to<JsonObject>();
                    saved["title"] = content["title"];
                    saved["summary"] = content["summary"];
                    saved["model"] = job.config.llm.deployment;
                    saved["created_at"] = time_service_ready() ? static_cast<uint32_t>(time(nullptr)) : 0;
                    const SaveOutcome save = save_document(job, VoiceNoteArtifact::Enrichment, document);
                    if (save == SaveOutcome::Interrupted) {
                        error = "Recording or update interrupted enrichment";
                        outcome = VoiceNotesRequestOutcome::Interrupted;
                    } else if (save == SaveOutcome::Failed) {
                        error = "Could not save title and summary";
                        outcome = VoiceNotesRequestOutcome::ResourceFailure;
                    }
                }
            }
        }
    }
    if (opened) voice_notes_close(&recording, job.id);
    log_result("Enrichment", job, outcome, response.status, started, error);
    set_enrichment_state(error ? VoiceNotesEnrichmentState::Error : VoiceNotesEnrichmentState::Ready, error);
    return outcome;
}

void update_processing() {
    VoiceNoteSnapshot recorder = {};
    voice_notes_snapshot(&recorder);
    const bool update = ota_activity_is_busy();
    const bool online = WiFi.status() == WL_CONNECTED;
    const uint64_t now = now_ms();
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const VoiceNotesProcessingSnapshot previous = g_processing;
    if (g_work != Work::None) {
        g_processing.state = VoiceNotesProcessingState::Running;
        g_processing.reason = VoiceNotesProcessingReason::None;
    } else {
        g_processing = {};
        if (g_config && g_config->auto_process && (g_pending_tx || g_pending_llm)) {
            const bool tx_configured = g_pending_tx && configured(g_config->transcription) && !g_retry[0].setup_blocked;
            const bool llm_configured = g_pending_llm && configured(g_config->llm) && !g_retry[1].setup_blocked;
            const bool tx = tx_configured && g_retry[0].failures < 4;
            const bool llm = llm_configured && g_retry[1].failures < 4;
            if (!tx && !llm) {
                g_processing.state = VoiceNotesProcessingState::Blocked;
                g_processing.reason = tx_configured || llm_configured
                    ? VoiceNotesProcessingReason::RetriesExhausted : VoiceNotesProcessingReason::Configuration;
                g_processing.service = service_name(g_pending_tx, g_pending_llm);
            } else {
                g_processing.state = VoiceNotesProcessingState::Waiting;
                if (recorder.session_active) g_processing.reason = VoiceNotesProcessingReason::Recording;
                else if (update) g_processing.reason = VoiceNotesProcessingReason::Update;
                else if (!online) g_processing.reason = VoiceNotesProcessingReason::Offline;
                else if ((!tx || now < g_retry[0].retry_until || now < g_retry[0].cooldown_until) &&
                         (!llm || now < g_retry[1].retry_until || now < g_retry[1].cooldown_until))
                    g_processing.reason = VoiceNotesProcessingReason::Retry;
                g_processing.service = service_name(tx, llm);
            }
        }
    }
    const VoiceNotesProcessingSnapshot current = g_processing;
    xSemaphoreGive(g_mutex);
    if ((previous.state != current.state || previous.reason != current.reason ||
         previous.service != current.service) &&
        (current.state == VoiceNotesProcessingState::Blocked ||
         (current.state == VoiceNotesProcessingState::Waiting &&
          current.reason != VoiceNotesProcessingReason::None))) {
        const auto level = current.state == VoiceNotesProcessingState::Blocked
            ? LOG_LEVEL_WARN : LOG_LEVEL_INFO;
        LOG_AT(level, TAG, "Processing state=%s reason=%s service=%s",
                  voice_notes_processing_state_name(current.state),
                  voice_notes_processing_reason_name(current.reason),
                  voice_notes_processing_service_name(current.service));
    }
}

void finish_service(Retry& retry, const PassService& pass, bool completed) {
    if (pass.failed) {
        record_failure(retry);
        const uint64_t delay = retry_delay(retry.failures);
        if (pass.setup) retry.setup_blocked = true;
        if (pass.limited) {
            const uint64_t until = deadline(now_ms(), pass.cooldown ? pass.cooldown : delay);
            if (until > retry.cooldown_until) retry.cooldown_until = until;
        }
    } else if (completed && pass.attempted && pass.succeeded) {
        reset_retry(retry);
    }
}

void log_retry(const char* service, const Retry& retry, const PassService& pass) {
    if (!pass.failed) return;
    if (retry.setup_blocked) {
        LOGW(TAG, "Processing blocked service=%s reason=configuration; update service settings", service);
    } else if (retry.failures >= 4) {
        LOGW(TAG, "Processing blocked service=%s reason=retries_exhausted failures=%u", service, retry.failures);
    } else {
        const uint64_t until = retry.retry_until > retry.cooldown_until
            ? retry.retry_until : retry.cooldown_until;
        const uint64_t now = now_ms();
        LOGW(TAG, "Retry scheduled service=%s retry=%u/3 delay_ms=%llu rate_limited=%u",
             service, retry.failures, static_cast<unsigned long long>(until > now ? until - now : 0),
             unsigned(pass.limited));
    }
}

void worker(void*) {
    uint64_t next_scan = 0;
    for (;;) {
        VoiceNoteSnapshot recorder = {};
        voice_notes_snapshot(&recorder);
        const bool discovery_available = !recorder.session_active && !ota_activity_is_busy();
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        if (recorder.last_clip[0] && strcmp(recorder.last_clip, g_last_saved) != 0) {
            strlcpy(g_last_saved, recorder.last_clip, sizeof(g_last_saved));
            for (auto& retry : g_retry) reset_retry(retry);
            g_refresh = true;
        }
        Work work = g_work;
        const bool scan = discovery_available && work == Work::None && g_config && g_config->auto_process &&
                          (g_refresh || now_ms() >= next_scan);
        const bool scan_llm = scan && configured(g_config->llm);
        if (scan) g_refresh = false;
        xSemaphoreGive(g_mutex);
        if (scan) {
            VoiceNoteCursor cursor = {}, candidate = {};
            const VoiceNoteCandidate tx = voice_notes_next_candidate(cursor, &candidate, true, false);
            const VoiceNoteCandidate llm = scan_llm
                ? voice_notes_next_candidate(cursor, &candidate, false, true) : VoiceNoteCandidate::None;
            xSemaphoreTake(g_mutex, portMAX_DELAY);
            if (tx == VoiceNoteCandidate::Interrupted || llm == VoiceNoteCandidate::Interrupted) {
                g_refresh = true;
            } else {
                g_pending_tx = tx != VoiceNoteCandidate::None;
                g_pending_llm = llm != VoiceNoteCandidate::None;
                next_scan = deadline(now_ms(), 5000);
                if (tx == VoiceNoteCandidate::Error || llm == VoiceNoteCandidate::Error) {
                    for (auto& retry : g_retry) {
                        if (retry.failures < 4 && now_ms() >= retry.retry_until &&
                            now_ms() >= retry.cooldown_until && !retry.setup_blocked)
                            record_failure(retry);
                    }
                }
            }
            xSemaphoreGive(g_mutex);
        }
        update_processing();
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        if (g_work == Work::None && g_config && g_config->auto_process &&
            g_processing.state == VoiceNotesProcessingState::Waiting &&
            g_processing.reason == VoiceNotesProcessingReason::None) {
            Job* job = static_cast<Job*>(heap_caps_malloc(sizeof(Job), kPsramCaps));
            if (job) {
                new (job) Job{};
                voice_notes_snapshot(&recorder);
                if (!recorder.session_active && !ota_activity_is_busy() && WiFi.status() == WL_CONNECTED) {
                    job->config = *g_config;
                    job->ota_epoch = ota_activity_epoch();
                    job->recording_generation = recorder.recording_generation;
                    g_job = job;
                    g_work = Work::Automatic;
                    g_processing = {VoiceNotesProcessingState::Running, VoiceNotesProcessingReason::None,
                                    VoiceNotesProcessingService::None};
                } else heap_caps_free(job);
            } else {
                LOGE(TAG, "Cannot allocate processing job");
                for (auto& retry : g_retry) {
                    record_failure(retry);
                }
            }
        }
        work = g_work;
        Job* job = g_job;
        xSemaphoreGive(g_mutex);
        if (work == Work::None || !job) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }

        LOGI(TAG, "Processing started mode=%s", work_name(work));
        PassService pass[2] = {};
        VoiceNoteCursor cursor = {};
        bool completed = false;
        bool first = true;
        const char* stop_reason = "completed";
        for (;;) {
            if (!voice_notes_transcription_checkpoint(job->ota_epoch, job->recording_generation)) {
                stop_reason = "recording_or_update";
                break;
            }
            if (WiFi.status() != WL_CONNECTED) { stop_reason = "offline"; break; }
            xSemaphoreTake(g_mutex, portMAX_DELAY);
            const bool enabled = g_config && g_config->auto_process;
            if (!first && g_config) job->config = *g_config;
            const uint64_t now = now_ms();
            const bool automatic = work == Work::Automatic;
            bool tx = configured(job->config.transcription) && !pass[0].unavailable &&
                now >= g_retry[0].cooldown_until &&
                (!automatic || (!g_retry[0].setup_blocked && g_retry[0].failures < 4 &&
                                now >= g_retry[0].retry_until));
            bool llm = configured(job->config.llm) && !pass[1].unavailable &&
                now >= g_retry[1].cooldown_until &&
                (!automatic || (!g_retry[1].setup_blocked && g_retry[1].failures < 4 &&
                                now >= g_retry[1].retry_until));
            g_processing.service = VoiceNotesProcessingService::None;
            xSemaphoreGive(g_mutex);
            if (automatic && !enabled) { stop_reason = "automatic_disabled"; break; }
            if (work == Work::Retranscribe) llm = false;
            if (work == Work::Regenerate) tx = false;
            if (!tx && !llm) { stop_reason = "services_unavailable"; break; }
            if (work == Work::Automatic || work == Work::Manual) {
                VoiceNoteCursor next = {};
                const VoiceNoteCandidate selection = voice_notes_next_candidate(cursor, &next, tx, llm);
                if (selection == VoiceNoteCandidate::Interrupted) {
                    stop_reason = "recording_or_update";
                    break;
                }
                if (selection == VoiceNoteCandidate::Error) {
                    pass[0].failed = pass[1].failed = true;
                    stop_reason = "catalog_error";
                    break;
                }
                if (selection == VoiceNoteCandidate::None) { completed = true; break; }
                cursor = next;
                strlcpy(job->id, next.id, sizeof(job->id));
            }
            first = false;
            bool has_tx = voice_notes_has_artifact(job->id, VoiceNoteArtifact::Transcript);
            const bool has_llm = voice_notes_has_artifact(job->id, VoiceNoteArtifact::Enrichment);
            bool stop = false;
            if (tx && (!has_tx || work == Work::Retranscribe) && work != Work::Regenerate) {
                set_active(Service::Transcription, job->id);
                uint64_t cooldown = 0;
                const VoiceNotesRequestOutcome result = transcribe(*job, work == Work::Retranscribe, cooldown);
                PassService& state = pass[0];
                if (result != VoiceNotesRequestOutcome::Skip) state.attempted = true;
                if (result == VoiceNotesRequestOutcome::Success) {
                    state.succeeded = true; ++state.saved; has_tx = true;
                }
                else if (result == VoiceNotesRequestOutcome::Interrupted) {
                    stop_reason = "recording_or_update"; stop = true;
                }
                else if (result != VoiceNotesRequestOutcome::Skip) {
                    state.failed = true;
                    if (result == VoiceNotesRequestOutcome::SetupFailure) { state.setup = state.unavailable = true; }
                    else if (result == VoiceNotesRequestOutcome::RateLimited) {
                        state.limited = state.unavailable = true;
                        state.cooldown = cooldown;
                    } else if (result == VoiceNotesRequestOutcome::ResourceFailure) {
                        pass[1].failed = true;
                        stop_reason = "resource_failure";
                        stop = true;
                    } else if (result != VoiceNotesRequestOutcome::NoteFailure) {
                        stop_reason = outcome_name(result); stop = true;
                    }
                }
                if (!has_tx) llm = false;
                if (!stop && result != VoiceNotesRequestOutcome::Skip) pause_processing();
            }
            if (!stop && has_tx && llm && (!has_llm || work == Work::Regenerate) &&
                work != Work::Retranscribe) {
                if (!voice_notes_transcription_checkpoint(job->ota_epoch, job->recording_generation)) {
                    stop_reason = "recording_or_update";
                    break;
                }
                xSemaphoreTake(g_mutex, portMAX_DELAY);
                const bool still_enabled = work != Work::Automatic || (g_config && g_config->auto_process);
                if (g_config) job->config = *g_config;
                const bool llm_ready = g_config && configured(g_config->llm) &&
                    now_ms() >= g_retry[1].cooldown_until &&
                    (work != Work::Automatic || (!g_retry[1].setup_blocked && g_retry[1].failures < 4 &&
                                                now_ms() >= g_retry[1].retry_until));
                xSemaphoreGive(g_mutex);
                if (!still_enabled) { stop_reason = "automatic_disabled"; break; }
                if (llm_ready) {
                    set_active(Service::Enrichment, job->id);
                    uint64_t cooldown = 0;
                    const VoiceNotesRequestOutcome result = enrich(*job, work == Work::Regenerate, cooldown);
                    PassService& state = pass[1];
                    if (result != VoiceNotesRequestOutcome::Skip) state.attempted = true;
                    if (result == VoiceNotesRequestOutcome::Success) { state.succeeded = true; ++state.saved; }
                    else if (result == VoiceNotesRequestOutcome::Interrupted) {
                        stop_reason = "recording_or_update"; stop = true;
                    }
                    else if (result != VoiceNotesRequestOutcome::Skip) {
                        state.failed = true;
                        if (result == VoiceNotesRequestOutcome::SetupFailure) state.setup = state.unavailable = true;
                        else if (result == VoiceNotesRequestOutcome::RateLimited) {
                            state.limited = state.unavailable = true;
                            state.cooldown = cooldown;
                        } else if (result == VoiceNotesRequestOutcome::ResourceFailure) {
                            pass[0].failed = true;
                            stop_reason = "resource_failure";
                            stop = true;
                        } else if (result != VoiceNotesRequestOutcome::NoteFailure) {
                            stop_reason = outcome_name(result); stop = true;
                        }
                    }
                    if (!stop && result != VoiceNotesRequestOutcome::Skip) pause_processing();
                }
            }
            if (stop) break;
            if (work == Work::Single || work == Work::Retranscribe || work == Work::Regenerate) {
                completed = true;
                break;
            }
        }
        VoiceNotesTranscriptionSnapshot transcription = {};
        voice_notes_transcription_snapshot(&transcription);
        if (transcription.state == VoiceNotesTranscriptionState::Preparing &&
            strcmp(transcription.note_id, job->id) == 0)
            set_tx_state(VoiceNotesTranscriptionState::Error,
                         "Processing stopped before transcription started; retry processing");
        VoiceNotesEnrichmentSnapshot enrichment = {};
        voice_notes_enrichment_snapshot(&enrichment);
        if (enrichment.state == VoiceNotesEnrichmentState::Preparing &&
            strcmp(enrichment.note_id, job->id) == 0)
            set_enrichment_state(VoiceNotesEnrichmentState::Error,
                                 "Processing stopped before enrichment started; retry processing");
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        finish_service(g_retry[0], pass[0], completed);
        finish_service(g_retry[1], pass[1], completed);
        const Retry retries[] = {g_retry[0], g_retry[1]};
        g_job = nullptr;
        g_work = Work::None;
        g_refresh = true;
        g_processing.service = VoiceNotesProcessingService::None;
        xSemaphoreGive(g_mutex);
        log_retry("transcription", retries[0], pass[0]);
        log_retry("enrichment", retries[1], pass[1]);
        LOGI(TAG, "Processing ended mode=%s reason=%s transcribed=%u enriched=%u failed=%u",
             work_name(work), stop_reason, pass[0].saved, pass[1].saved,
             unsigned(pass[0].failed || pass[1].failed));
        heap_caps_free(job);
    }
}
}

#if HAS_DISPLAY && HAS_PSRAM
BindingResolverStatus voice_notes_latest_transcript_resolve(const char* key, char* out, size_t size) {
    if (strcmp(key, "id") && strcmp(key, "text")) return BINDING_RESOLVER_UNKNOWN;
    LoadedDataSnapshot* snapshot = acquire_latest_transcript();
    if (!snapshot) return BINDING_RESOLVER_UNAVAILABLE;
    const JsonVariantConst value = snapshot->data()[key];
    const bool resolved = binding_template_checked_resolution_active()
        ? binding_value_scalar_json(value, out, size)
        : binding_value_scalar_json_preview(value, out, size);
    snapshot->release();
    return resolved ? BINDING_RESOLVER_RESOLVED : BINDING_RESOLVER_UNAVAILABLE;
}

BindingResolverStatus voice_notes_latest_transcript_acquire_text(BindingTextView* out) {
    LoadedDataSnapshot* snapshot = acquire_latest_transcript();
    if (!snapshot) return BINDING_RESOLVER_UNAVAILABLE;
    out->text = snapshot->data()["text"].as<const char*>();
    out->owner = snapshot;
    out->release = [](void* owner) { static_cast<LoadedDataSnapshot*>(owner)->release(); };
    return BINDING_RESOLVER_RESOLVED;
}
#endif

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

bool voice_notes_processing_configure(const VoiceNotesConfig& config) {
    if (!g_mutex) {
        g_mutex = xSemaphoreCreateMutex();
        if (!g_mutex) return false;
    }
    if (!g_config) {
        g_config = static_cast<VoiceNotesConfig*>(heap_caps_malloc(sizeof(*g_config), kPsramCaps));
        if (!g_config) return false;
        new (g_config) VoiceNotesConfig{};
    }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (service_changed(g_config->transcription, config.transcription)) {
        g_retry[0].setup_blocked = false;
        reset_retry(g_retry[0]);
    }
    if (service_changed(g_config->llm, config.llm)) {
        g_retry[1].setup_blocked = false;
        reset_retry(g_retry[1]);
    }
    *g_config = config;
    g_refresh = true;
    bool ready = true;
    if (g_initialized && config.auto_process && !g_worker)
        ready = rtos_create_task_internal_stack(worker, "VoiceNotesProcess",
            kWorkerStackBytes, nullptr, 1, &g_worker, &g_worker_allocation);
    TaskHandle_t handle = g_worker;
    xSemaphoreGive(g_mutex);
    if (handle) xTaskNotifyGive(handle);
    if (!ready) LOGE(TAG, "Cannot create processing worker");
    return ready;
}

bool voice_notes_transcription_configured() {
    if (!g_mutex || !g_config) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const bool result = configured(g_config->transcription);
    xSemaphoreGive(g_mutex);
    return result;
}
bool voice_notes_enrichment_configured() {
    if (!g_mutex || !g_config) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const bool result = configured(g_config->llm);
    xSemaphoreGive(g_mutex);
    return result;
}

bool voice_notes_transcription_checkpoint(uint32_t epoch, uint64_t generation) {
    VoiceNoteSnapshot snapshot = {};
    voice_notes_snapshot(&snapshot);
    return !snapshot.session_active && snapshot.recording_generation == generation &&
           !ota_activity_is_busy() && ota_activity_epoch() == epoch;
}

bool voice_notes_processing_init() {
    if (!g_mutex || !g_config) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_initialized = true;
    bool ready = true;
    if (g_config->auto_process && !g_worker)
        ready = rtos_create_task_internal_stack(worker, "VoiceNotesProcess",
            kWorkerStackBytes, nullptr, 1, &g_worker, &g_worker_allocation);
    TaskHandle_t handle = g_worker;
    xSemaphoreGive(g_mutex);
    if (handle) xTaskNotifyGive(handle);
    if (!ready) LOGE(TAG, "Cannot create processing worker");
    return ready;
}

static VoiceNotesProcessingStart admit(const char* id, Work work, bool needs_tx, bool needs_llm,
                                       bool llm_depends_on_tx = false) {
    if (!g_mutex || !g_config || !g_initialized) return VoiceNotesProcessingStart::Unavailable;
    if (ota_activity_is_busy()) return VoiceNotesProcessingStart::Busy;
    VoiceNoteSnapshot recorder = {};
    voice_notes_snapshot(&recorder);
    if (recorder.session_active) return VoiceNotesProcessingStart::Busy;
    if (WiFi.status() != WL_CONNECTED) return VoiceNotesProcessingStart::Offline;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (g_work != Work::None) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesProcessingStart::Busy;
    }
    const bool tx = needs_tx && configured(g_config->transcription);
    const bool llm = needs_llm && configured(g_config->llm);
    if ((!tx && !llm) || (llm_depends_on_tx && !tx)) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesProcessingStart::NotConfigured;
    }
    const bool runnable_tx = tx && now_ms() >= g_retry[0].cooldown_until;
    const bool runnable_llm = llm && now_ms() >= g_retry[1].cooldown_until &&
                              (!llm_depends_on_tx || runnable_tx);
    if (!runnable_tx && !runnable_llm) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesProcessingStart::Cooldown;
    }
    Job* job = static_cast<Job*>(heap_caps_malloc(sizeof(Job), kPsramCaps));
    if (!job) {
        xSemaphoreGive(g_mutex);
        return VoiceNotesProcessingStart::Unavailable;
    }
    new (job) Job{};
    if (!g_worker && !rtos_create_task_internal_stack(worker, "VoiceNotesProcess",
            kWorkerStackBytes, nullptr, 1, &g_worker, &g_worker_allocation)) {
        heap_caps_free(job);
        xSemaphoreGive(g_mutex);
        return VoiceNotesProcessingStart::Unavailable;
    }
    job->config = *g_config;
    strlcpy(job->id, id ? id : "", sizeof(job->id));
    job->ota_epoch = ota_activity_epoch();
    job->recording_generation = recorder.recording_generation;
    g_job = job;
    g_work = work;
    g_processing = {VoiceNotesProcessingState::Running, VoiceNotesProcessingReason::None,
                    VoiceNotesProcessingService::None};
    if (id && runnable_tx) {
        g_transcription.state = VoiceNotesTranscriptionState::Preparing;
        strlcpy(g_transcription.note_id, id, sizeof(g_transcription.note_id));
        g_transcription.error[0] = '\0';
    } else if (id && runnable_llm) {
        g_enrichment.state = VoiceNotesEnrichmentState::Preparing;
        strlcpy(g_enrichment.note_id, id, sizeof(g_enrichment.note_id));
        g_enrichment.error[0] = '\0';
    }
    if (runnable_tx) { g_retry[0].setup_blocked = false; reset_retry(g_retry[0]); }
    if (runnable_llm) { g_retry[1].setup_blocked = false; reset_retry(g_retry[1]); }
    xSemaphoreGive(g_mutex);
    xTaskNotifyGive(g_worker);
    return VoiceNotesProcessingStart::Accepted;
}

VoiceNotesProcessingStart voice_notes_processing_start() {
    return admit(nullptr, Work::Manual, true, true);
}
VoiceNotesProcessingStart voice_notes_processing_start(const char* id, VoiceNotesProcessingMode mode) {
    if (!voice_notes_id_valid(id)) return VoiceNotesProcessingStart::InvalidId;
    if (!voice_notes_exists(id)) return VoiceNotesProcessingStart::NotFound;
    const bool tx = voice_notes_has_artifact(id, VoiceNoteArtifact::Transcript);
    const bool llm = voice_notes_has_artifact(id, VoiceNoteArtifact::Enrichment);
    if (mode == VoiceNotesProcessingMode::Retranscribe) {
        if (!tx) return VoiceNotesProcessingStart::MissingResult;
        return admit(id, Work::Retranscribe, true, false);
    }
    if (mode == VoiceNotesProcessingMode::Regenerate) {
        if (!tx || !llm) return VoiceNotesProcessingStart::MissingResult;
        return admit(id, Work::Regenerate, false, true);
    }
    if (tx && llm) return VoiceNotesProcessingStart::Unchanged;
    return admit(id, Work::Single, !tx, !llm, !tx);
}

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
        case VoiceNotesProcessingReason::RetriesExhausted: return "retries_exhausted";
    }
    return "none";
}
const char* voice_notes_processing_service_name(VoiceNotesProcessingService service) {
    switch (service) {
        case VoiceNotesProcessingService::None: return "none";
        case VoiceNotesProcessingService::Transcription: return "transcription";
        case VoiceNotesProcessingService::Enrichment: return "enrichment";
        case VoiceNotesProcessingService::Both: return "both";
    }
    return "none";
}
void voice_notes_transcription_snapshot(VoiceNotesTranscriptionSnapshot* out) {
    if (!out) return;
    if (!g_mutex) { *out = {}; return; }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    *out = g_transcription;
    retry_status(g_retry[0], out->retry_status, sizeof(out->retry_status));
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
void voice_notes_enrichment_snapshot(VoiceNotesEnrichmentSnapshot* out) {
    if (!out) return;
    if (!g_mutex) { *out = {}; return; }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    *out = g_enrichment;
    retry_status(g_retry[1], out->retry_status, sizeof(out->retry_status));
    xSemaphoreGive(g_mutex);
}
const char* voice_notes_enrichment_state_name(VoiceNotesEnrichmentState state) {
    switch (state) {
        case VoiceNotesEnrichmentState::Idle: return "idle";
        case VoiceNotesEnrichmentState::Preparing: return "preparing";
        case VoiceNotesEnrichmentState::Generating: return "generating";
        case VoiceNotesEnrichmentState::Saving: return "saving";
        case VoiceNotesEnrichmentState::Ready: return "ready";
        case VoiceNotesEnrichmentState::Error: return "error";
    }
    return "error";
}
#endif
