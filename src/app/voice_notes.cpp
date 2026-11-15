#include "board_config.h"

#if HAS_VOICE_NOTES

#include "voice_notes.h"
#include "voice_notes_file.h"
#include "action_continuation.h"
#include "audio_input.h"
#include "fs_indexed_store.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"
#include "storage.h"
#include "time_service.h"
#include "voice_wav.h"
#include "voice_notes_metadata.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <time.h>

namespace {
constexpr char TAG[] = "VoiceNotes";
constexpr char kDirectory[] = "/storage/voice-notes";
constexpr uint32_t kFramesPerRead = 1024;
constexpr uint32_t kQueueSlots = 16;
constexpr uint32_t kReadTimeoutMs = 100;
constexpr uint64_t kStallTimeoutUs = 2000000;
constexpr uint64_t kCapacityCheckUs = 1000000;
constexpr uint64_t kBaseReserve = 65536;
constexpr size_t kPathSize = 96;
const char* const kIndexFields[] = {"size_bytes", "duration_ms", "sample_rate", "channels"};
FsIndexedStore g_store(kDirectory, kIndexFields, 4, nullptr, 0, ".wav",
                       voice_notes_metadata_read_metadata);
SemaphoreHandle_t g_catalog_mutex = nullptr;
TaskHandle_t g_worker = nullptr;
TaskHandle_t g_capture_worker = nullptr;
RtosTaskInternalAlloc g_worker_allocation = {};
RtosTaskPsramAlloc g_capture_allocation = {};
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
VoiceNoteSnapshot g_snapshot = {};
VoiceNoteCommand g_command = VoiceNoteCommand::Start;
uint32_t g_token = 0;
uint64_t g_command_queued_us = 0;
bool g_command_pending = false;
bool g_command_busy = false;
bool g_terminating = false;
bool g_cancel_requested = false;
bool g_cleanup_blocked = false;
bool g_initialized = false;
char g_reader_ids[4][32] = {};

struct Session {
    File file;
    AudioInputFormat format = {};
    char id[32] = {};
    char temporary[kPathSize] = {};
    char completed[kPathSize] = {};
    int16_t* samples = nullptr;
    int16_t* queue = nullptr;
    uint32_t data_bytes = 0;
    uint64_t next_capacity_check_us = 0;
    uint64_t max_write_us = 0;
    uint64_t max_capacity_us = 0;
    uint64_t free_at_check = 0;
    uint32_t bytes_at_check = 0;
    uint64_t reserve = 0;
    bool owns_input = false;
};

enum class CaptureCommand : uint8_t { None, Start, Pause, Stop };
struct CaptureState {
    Session* session = nullptr;
    CaptureCommand command = CaptureCommand::None;
    bool done = false;
    bool result = false;
    bool running = false;
    bool owned = false;
    bool queue_overrun = false;
    AudioInputReadStatus error = AudioInputReadStatus::Ok;
    uint32_t head = 0;
    uint32_t count = 0;
    size_t frames[kQueueSlots] = {};
    uint64_t last_data_us = 0;
};
CaptureState g_capture = {};

bool cancelled() {
    portENTER_CRITICAL(&g_mux);
    const bool value = g_cancel_requested;
    portEXIT_CRITICAL(&g_mux);
    return value;
}

void capture_worker(void*) {
    for (;;) {
        portENTER_CRITICAL(&g_mux);
        const CaptureCommand command = g_capture.command;
        Session* session = g_capture.session;
        const bool running = g_capture.running;
        const uint32_t count = g_capture.count;
        const uint32_t tail = (g_capture.head + count) % kQueueSlots;
        portEXIT_CRITICAL(&g_mux);

        if (command != CaptureCommand::None) {
            bool result = false;
            if (command == CaptureCommand::Start) {
                result = audio_input_start_capture();
            } else if (command == CaptureCommand::Pause) {
                result = audio_input_pause_capture();
                if (result) result = audio_input_capture_status() == AudioInputReadStatus::Ok;
            } else {
                result = !audio_input_capture_owned() || audio_input_stop_capture();
            }
            const bool owned = audio_input_capture_owned();
            portENTER_CRITICAL(&g_mux);
            g_capture.running = command == CaptureCommand::Start && result;
            g_capture.owned = owned;
            if (command == CaptureCommand::Start && result)
                g_capture.last_data_us = (uint64_t)esp_timer_get_time();
            g_capture.result = result;
            g_capture.done = true;
            g_capture.command = CaptureCommand::None;
            portEXIT_CRITICAL(&g_mux);
            xTaskNotifyGive(g_worker);
            continue;
        }
        if (!running || !session) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (count == kQueueSlots) {
            portENTER_CRITICAL(&g_mux);
            const bool full = g_capture.count == kQueueSlots &&
                              g_capture.command == CaptureCommand::None;
            if (full) {
                g_capture.error = AudioInputReadStatus::Overrun;
                g_capture.queue_overrun = true;
                g_capture.running = false;
            }
            portEXIT_CRITICAL(&g_mux);
            if (full) xTaskNotifyGive(g_worker);
            continue;
        }
        const size_t stride = kFramesPerRead * session->format.channels;
        const AudioInputReadResult read = audio_input_read_frames(
            session->queue + tail * stride, kFramesPerRead, kReadTimeoutMs);
        const uint64_t now = (uint64_t)esp_timer_get_time();
        portENTER_CRITICAL(&g_mux);
        if (read.status == AudioInputReadStatus::Error || read.status == AudioInputReadStatus::Overrun) {
            g_capture.error = read.status;
            g_capture.running = false;
        } else if (read.frames) {
            g_capture.frames[tail] = read.frames;
            ++g_capture.count;
            g_capture.last_data_us = now;
        } else if (now - g_capture.last_data_us >= kStallTimeoutUs) {
            g_capture.error = AudioInputReadStatus::Timeout;
            g_capture.running = false;
        }
        portEXIT_CRITICAL(&g_mux);
        xTaskNotifyGive(g_worker);
    }
}

bool capture_command(Session& session, CaptureCommand command) {
    portENTER_CRITICAL(&g_mux);
    g_capture.session = &session;
    g_capture.done = false;
    g_capture.command = command;
    portEXIT_CRITICAL(&g_mux);
    xTaskNotifyGive(g_capture_worker);
    for (;;) {
        portENTER_CRITICAL(&g_mux);
        const bool done = g_capture.done;
        const bool result = g_capture.result;
        portEXIT_CRITICAL(&g_mux);
        if (done) return result;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void set_error(const char* error, bool terminal) {
    LOGE(TAG, "%s", error);
    portENTER_CRITICAL(&g_mux);
    strlcpy(g_snapshot.error, error, sizeof(g_snapshot.error));
    if (terminal) g_snapshot.state = VoiceNoteState::Error;
    portEXIT_CRITICAL(&g_mux);
}

void set_state(VoiceNoteState state) {
    portENTER_CRITICAL(&g_mux);
    g_snapshot.state = state;
    portEXIT_CRITICAL(&g_mux);
}

bool complete(uint32_t token, bool success, bool finishing = false) {
    portENTER_CRITICAL(&g_mux);
    const bool allowed = success && !g_cancel_requested;
    const bool completed = !token || action_continuation_complete(token, allowed);
    if (token) {
        g_command_busy = false;
        if (allowed && completed) g_snapshot.error[0] = '\0';
    }
    if (finishing) {
        g_snapshot.session_active = g_cleanup_blocked;
        if (!g_cleanup_blocked) g_snapshot.scenario[0] = '\0';
        g_cancel_requested = false;
        g_terminating = false;
    }
    portEXIT_CRITICAL(&g_mux);
    if (token && !completed) LOGE(TAG, "Action continuation expired");
    return !token || (completed && (!success || allowed));
}

bool refresh_capacity(Session& session) {
    const uint64_t total = Storage.totalBytes();
    const uint64_t used = Storage.usedBytes();
    if (!total || used > total) return false;
    session.free_at_check = total - used;
    session.bytes_at_check = session.data_bytes;
    session.next_capacity_check_us = (uint64_t)esp_timer_get_time() + kCapacityCheckUs;
    return true;
}

uint64_t available_after_growth(const Session& session) {
    const uint64_t growth = session.data_bytes - session.bytes_at_check;
    return growth < session.free_at_check ? session.free_at_check - growth : 0;
}

bool remove_temporary(const Session& session) {
    if (!session.temporary[0] || !Storage.exists(session.temporary)) return true;
    if (Storage.remove(session.temporary)) return true;
    LOGE(TAG, "Cannot remove unfinished recording");
    return false;
}

bool cleanup(Session& session) {
    bool input_stopped = true;
    if (session.owns_input) {
        input_stopped = capture_command(session, CaptureCommand::Stop);
        if (input_stopped) session.owns_input = false;
    }
    if (session.file) session.file.close();
    const bool removed = remove_temporary(session);
    if (session.samples) {
        heap_caps_free(session.samples);
        session.samples = nullptr;
    }
    if (session.queue && input_stopped) {
        heap_caps_free(session.queue);
        session.queue = nullptr;
    }
    portENTER_CRITICAL(&g_mux);
    if (input_stopped) g_capture = {};
    g_cleanup_blocked = !input_stopped;
    portEXIT_CRITICAL(&g_mux);
    return input_stopped && removed;
}

bool create_session(Session& session) {
    session = Session{};
    session.format = audio_input_format();
    if (!session.format.sample_rate || !session.format.channels ||
        session.format.channels > 2 || session.format.bits_per_sample != 16) {
        set_error("Unsupported microphone format", true);
        return false;
    }
    session.samples = static_cast<int16_t*>(heap_caps_malloc(
        kFramesPerRead * session.format.channels * sizeof(int16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!session.samples) {
        set_error("Microphone buffer allocation failed", true);
        return false;
    }
    const size_t queue_bytes = kQueueSlots * kFramesPerRead * session.format.channels * sizeof(int16_t);
    session.queue = static_cast<int16_t*>(heap_caps_malloc(queue_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!session.queue) {
        LOGW(TAG, "PSRAM unavailable; allocating recording queue in internal RAM");
        session.queue = static_cast<int16_t*>(heap_caps_malloc(queue_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!session.queue) {
        set_error("Recording queue allocation failed", true);
        return false;
    }
    const String catalog = g_store.list();
    if (catalog.isEmpty()) {
        set_error("Note catalog unavailable", true);
        return false;
    }
    session.reserve = kBaseReserve + 2 * catalog.length();
    if (!refresh_capacity(session) || available_after_growth(session) <= session.reserve) {
        set_error("Insufficient recording storage", true);
        return false;
    }
    bool unique = false;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        snprintf(session.id, sizeof(session.id), "%08lx%08lx",
                 static_cast<unsigned long>(esp_random()),
                 static_cast<unsigned long>(esp_random()));
        snprintf(session.temporary, sizeof(session.temporary), "%s/%s.wav.tmp", kDirectory, session.id);
        snprintf(session.completed, sizeof(session.completed), "%s/%s.wav", kDirectory, session.id);
        if (!Storage.exists(session.temporary) && !Storage.exists(session.completed) &&
            !g_store.exists(session.id)) {
            unique = true;
            break;
        }
    }
    if (!unique) {
        session.temporary[0] = '\0';
        set_error("Cannot allocate a unique note ID", true);
        return false;
    }
    session.file = Storage.open(session.temporary, "w+");
    if (!session.file) {
        set_error("Cannot create recording file", true);
        return false;
    }
    uint8_t header[VOICE_WAV_HEADER_BYTES];
    voice_wav_build_header(header, 0, session.format.sample_rate, session.format.channels, 16);
    if (session.file.write(header, sizeof(header)) != sizeof(header)) {
        set_error("Cannot write WAV header", true);
        return false;
    }
    if (cancelled()) return false;
    const bool started = capture_command(session, CaptureCommand::Start);
    portENTER_CRITICAL(&g_mux);
    session.owns_input = g_capture.owned;
    portEXIT_CRITICAL(&g_mux);
    if (!started) {
        set_error("Microphone capture could not start", true);
        return false;
    }
    return true;
}

bool stop_sampling(Session& session, bool release) {
    if (!session.owns_input) return true;
    const bool stopped = capture_command(session, release ? CaptureCommand::Stop : CaptureCommand::Pause);
    if (!stopped) {
        set_error("Microphone stop failed", true);
        return false;
    }
    if (release) session.owns_input = false;
    return true;
}

enum class QueueResult : uint8_t { Empty, Written, Limit, Error };

struct SaveTiming {
    uint64_t requested_us;
    uint64_t last_us;
    uint64_t queued_us = 0, stop_us = 0, drain_us = 0, header_us = 0;
    uint64_t flush_us = 0, close_us = 0, verify_us = 0, rename_us = 0, index_us = 0;

    explicit SaveTiming(uint64_t requested) {
        last_us = (uint64_t)esp_timer_get_time();
        requested_us = requested ? requested : last_us;
        queued_us = last_us - requested_us;
    }

    void mark(uint64_t& elapsed) {
        const uint64_t now = (uint64_t)esp_timer_get_time();
        elapsed = now - last_us;
        last_us = now;
    }

    void report(uint32_t bytes, bool published) {
        const uint64_t now = (uint64_t)esp_timer_get_time();
        LOGI(TAG, "Save ms: bytes=%lu queued=%llu stop=%llu drain=%llu header=%llu flush=%llu close=%llu verify=%llu rename=%llu index=%llu cleanup=%llu total=%llu published=%u",
             (unsigned long)bytes, (unsigned long long)(queued_us / 1000),
             (unsigned long long)(stop_us / 1000), (unsigned long long)(drain_us / 1000),
             (unsigned long long)(header_us / 1000), (unsigned long long)(flush_us / 1000),
             (unsigned long long)(close_us / 1000), (unsigned long long)(verify_us / 1000),
             (unsigned long long)(rename_us / 1000), (unsigned long long)(index_us / 1000),
             (unsigned long long)((now - last_us) / 1000),
             (unsigned long long)((now - requested_us) / 1000), (unsigned)published);
    }
};

QueueResult write_queued(Session& session) {
    portENTER_CRITICAL(&g_mux);
    const AudioInputReadStatus error = g_capture.error;
    const bool queue_overrun = g_capture.queue_overrun;
    const uint32_t count = g_capture.count;
    const uint32_t head = g_capture.head;
    const size_t frames = count ? g_capture.frames[head] : 0;
    portEXIT_CRITICAL(&g_mux);
    if (error != AudioInputReadStatus::Ok) {
        set_error(queue_overrun ? "Recording buffer overrun" :
                  error == AudioInputReadStatus::Timeout ? "Microphone capture stalled" :
                  "Microphone capture failed or overran", true);
        return QueueResult::Error;
    }
    if (!count) return QueueResult::Empty;
    const uint64_t now = (uint64_t)esp_timer_get_time();
    const uint32_t block = session.format.channels * sizeof(int16_t);
    const uint32_t bytes = frames * block;
    if (now >= session.next_capacity_check_us) {
        const uint64_t started = (uint64_t)esp_timer_get_time();
        const bool available = refresh_capacity(session);
        const uint64_t elapsed = (uint64_t)esp_timer_get_time() - started;
        if (elapsed > session.max_capacity_us) session.max_capacity_us = elapsed;
        if (!available) {
            set_error("Cannot check recording storage", true);
            return QueueResult::Error;
        }
    }
    if (available_after_growth(session) <= session.reserve + bytes ||
        session.data_bytes > (UINT32_MAX - 36U) - bytes) return QueueResult::Limit;
    memcpy(session.samples, session.queue + head * kFramesPerRead * session.format.channels, bytes);
    portENTER_CRITICAL(&g_mux);
    g_capture.head = (head + 1) % kQueueSlots;
    --g_capture.count;
    portEXIT_CRITICAL(&g_mux);
    const uint64_t started = (uint64_t)esp_timer_get_time();
    const size_t written = session.file.write(reinterpret_cast<const uint8_t*>(session.samples), bytes);
    const uint64_t elapsed = (uint64_t)esp_timer_get_time() - started;
    if (elapsed > session.max_write_us) session.max_write_us = elapsed;
    if (written != bytes) {
        set_error("Recording write failed", true);
        return QueueResult::Error;
    }
    session.data_bytes += bytes;
    portENTER_CRITICAL(&g_mux);
    g_snapshot.duration_ms = (uint64_t)(session.data_bytes / block) * 1000 / session.format.sample_rate;
    portEXIT_CRITICAL(&g_mux);
    return QueueResult::Written;
}

QueueResult drain_queue(Session& session) {
    for (;;) {
        if (cancelled()) return QueueResult::Error;
        const QueueResult result = write_queued(session);
        if (result == QueueResult::Empty || result == QueueResult::Error) return result;
        if (result == QueueResult::Limit) {
            LOGI(TAG, "Recording reached storage or WAV size limit");
            portENTER_CRITICAL(&g_mux);
            g_capture.count = 0;
            portEXIT_CRITICAL(&g_mux);
            return result;
        }
    }
}

bool publish(Session& session, SaveTiming& timing) {
    const bool stopped = stop_sampling(session, false);
    timing.mark(timing.stop_us);
    if (!stopped) return false;
    const QueueResult drained = drain_queue(session);
    timing.mark(timing.drain_us);
    if (drained == QueueResult::Error) return false;
    if (!session.data_bytes) {
        set_error("No audio captured", true);
        return false;
    }
    if (cancelled()) return false;
    uint8_t header[VOICE_WAV_HEADER_BYTES];
    voice_wav_build_header(header, session.data_bytes, session.format.sample_rate,
                           session.format.channels, 16);
    const bool header_written = session.file.seek(0) &&
                                session.file.write(header, sizeof(header)) == sizeof(header);
    timing.mark(timing.header_us);
    if (!header_written) {
        set_error("Cannot finalize WAV header", true);
        return false;
    }
    session.file.flush();
    timing.mark(timing.flush_us);
    session.file.close();
    timing.mark(timing.close_us);
    StaticJsonDocument<512> metadata;
    JsonObject object = metadata.to<JsonObject>();
    const FsIndexedStoreReadResult validation = voice_notes_metadata_read_metadata(session.temporary, object);
    timing.mark(timing.verify_us);
    if (validation != FsIndexedStoreReadResult::Valid) {
        set_error("Cannot validate finalized WAV", true);
        return false;
    }
    if (cancelled()) return false;
    if (xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE) {
        set_error("Note catalog unavailable", true);
        return false;
    }
    const StorageMutationGuard publication_guard;
    if (!publication_guard) {
        xSemaphoreGive(g_catalog_mutex);
        set_error("Cannot publish recording", true);
        return false;
    }
    bool renamed = false;
    bool registered = false;
    bool success = false;
    if (!cancelled() && !Storage.exists(session.completed))
        renamed = Storage.rename(session.temporary, session.completed);
    timing.mark(timing.rename_us);
    if (renamed && !cancelled()) {
        const uint32_t created = time_service_ready() ? static_cast<uint32_t>(time(nullptr)) : 0;
        registered = g_store.register_pre_written(session.id, created, object);
    }
    if (registered) {
        portENTER_CRITICAL(&g_mux);
        if (!g_cancel_requested) {
            strlcpy(g_snapshot.last_clip, session.id, sizeof(g_snapshot.last_clip));
            success = true;
        }
        portEXIT_CRITICAL(&g_mux);
    }
    timing.mark(timing.index_us);
    if (!success && renamed) {
        const bool removed = registered ? g_store.remove(session.id) : Storage.remove(session.completed);
        if (!removed) LOGE(TAG, "Cannot remove unpublished note");
    }
    xSemaphoreGive(g_catalog_mutex);
    if (!success && !cancelled()) set_error("Cannot publish recording", true);
    return success;
}

void finish_session(Session& session, uint32_t token, bool succeeded, bool discarded) {
    portENTER_CRITICAL(&g_mux);
    g_terminating = true;
    const uint32_t queued_token = g_command_pending ? g_token : 0;
    g_command_pending = false;
    g_token = 0;
    portEXIT_CRITICAL(&g_mux);
    const bool cleaned = cleanup(session);
    if (!cleaned) set_error("Recording cleanup failed", true);
    portENTER_CRITICAL(&g_mux);
    const bool interrupted = g_cancel_requested;
    if (cleaned && (succeeded || discarded)) {
        g_snapshot.state = VoiceNoteState::Idle;
        g_snapshot.error[0] = '\0';
    }
    portEXIT_CRITICAL(&g_mux);
    complete(token, succeeded && cleaned && !interrupted, !queued_token);
    if (queued_token) complete(queued_token, false, true);
}

void worker(void*) {
    Session session;
    for (;;) {
        portENTER_CRITICAL(&g_mux);
        const bool cancel = g_cancel_requested;
        const bool pending = g_command_pending;
        const VoiceNoteCommand command = g_command;
        const uint32_t token = g_token;
        const uint64_t queued_us = g_command_queued_us;
        if (pending) {
            g_command_pending = false;
            g_token = 0;
        }
        const VoiceNoteState state = g_snapshot.state;
        const bool cleanup_blocked = g_cleanup_blocked;
        portEXIT_CRITICAL(&g_mux);

        if (cancel) {
            set_state(VoiceNoteState::Discarding);
            finish_session(session, token, false, true);
            continue;
        }
        if (pending) {
            if (command == VoiceNoteCommand::Start) {
                if (cleanup_blocked && !cleanup(session)) {
                    set_error("Recording cleanup failed", true);
                    finish_session(session, token, false, cancelled());
                } else if (cancelled() || !create_session(session) || cancelled()) {
                    finish_session(session, token, false, cancelled());
                } else {
                    set_state(VoiceNoteState::Recording);
                    if (!complete(token, true)) {
                        const bool interrupted = cancelled();
                        if (!interrupted) set_error("Start action expired", true);
                        finish_session(session, 0, false, interrupted);
                    }
                }
            } else if (command == VoiceNoteCommand::Pause) {
                if (!stop_sampling(session, false)) finish_session(session, token, false, false);
                else {
                    const QueueResult drained = drain_queue(session);
                    if (drained == QueueResult::Error) finish_session(session, token, false, cancelled());
                    else if (cancelled()) finish_session(session, token, false, true);
                    else if (drained == QueueResult::Limit) {
                        set_state(VoiceNoteState::Saving);
                        SaveTiming timing(0);
                        const bool saved = publish(session, timing);
                        finish_session(session, token, saved, cancelled());
                        timing.report(session.data_bytes, saved);
                    } else {
                        set_state(VoiceNoteState::Paused);
                        if (!complete(token, true)) {
                            if (cancelled()) finish_session(session, 0, false, true);
                            else set_error("Pause action expired", false);
                        }
                    }
                }
            } else if (command == VoiceNoteCommand::Resume) {
                if (!capture_command(session, CaptureCommand::Start)) {
                    set_error("Microphone resume failed", true);
                    finish_session(session, token, false, false);
                } else if (cancelled()) {
                    finish_session(session, token, false, true);
                } else {
                    set_state(VoiceNoteState::Recording);
                    if (!complete(token, true)) {
                        if (cancelled()) finish_session(session, 0, false, true);
                        else set_error("Resume action expired", false);
                    }
                }
            } else if (command == VoiceNoteCommand::Save) {
                SaveTiming timing(queued_us);
                const bool saved = publish(session, timing);
                finish_session(session, token, saved, cancelled());
                timing.report(session.data_bytes, saved);
            } else {
                finish_session(session, token, true, true);
            }
            continue;
        }
        if (state != VoiceNoteState::Recording) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        const QueueResult result = write_queued(session);
        if (result == QueueResult::Empty) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        } else if (result == QueueResult::Limit) {
            set_state(VoiceNoteState::Saving);
            LOGI(TAG, "Recording reached storage or WAV size limit");
            SaveTiming timing(0);
            const bool saved = publish(session, timing);
            finish_session(session, 0, saved, cancelled());
            timing.report(session.data_bytes, saved);
        } else if (result == QueueResult::Error) {
            LOGE(TAG, "Capture timing: max write=%llums capacity=%llums",
                 (unsigned long long)(session.max_write_us / 1000),
                 (unsigned long long)(session.max_capacity_us / 1000));
            finish_session(session, 0, false, false);
        }
    }
}
}

bool voice_notes_init() {
    if (g_initialized) return true;
    if (!storage_mount() || (!Storage.exists("/storage") && !Storage.mkdir("/storage")) ||
        !g_store.begin()) {
        set_error("Recording storage initialization failed", true);
        return false;
    }
    File directory = Storage.open(kDirectory);
    if (!directory || !directory.isDirectory()) {
        set_error("Recording directory unavailable", true);
        return false;
    }
    File entry = directory.openNextFile();
    while (entry) {
        const String name = entry.name();
        entry.close();
        if (name.endsWith(".wav.tmp")) {
            const String path = String(kDirectory) + "/" + name;
            if (!Storage.remove(path.c_str())) LOGE(TAG, "Cannot remove stale %s", path.c_str());
        }
        entry = directory.openNextFile();
    }
    directory.close();
    g_catalog_mutex = xSemaphoreCreateMutex();
    if (!g_catalog_mutex) {
        set_error("Note catalog mutex allocation failed", true);
        return false;
    }
    bool capture_created = rtos_create_task_psram_stack(
        capture_worker, "VoiceCapture", 4096, nullptr, 3, &g_capture_worker, &g_capture_allocation);
    const bool capture_stack_in_psram = capture_created;
    if (!capture_created) {
        LOGW(TAG, "PSRAM unavailable; allocating capture task in internal RAM");
        capture_created = rtos_create_task_internal_stack(
            capture_worker, "VoiceCapture", 4096, nullptr, 3, &g_capture_worker, &g_capture_allocation);
    }
    if (!capture_created) {
        vSemaphoreDelete(g_catalog_mutex);
        g_catalog_mutex = nullptr;
        set_error("Capture task creation failed", true);
        return false;
    }
    LOGI(TAG, "Capture task stack=%s", capture_stack_in_psram ? "PSRAM" : "internal");
    if (!rtos_create_task_internal_stack(worker, "VoiceNotes", 8192, nullptr, 2,
                                         &g_worker, &g_worker_allocation)) {
        vTaskDelete(g_capture_worker);
        heap_caps_free(g_capture_allocation.stack);
        heap_caps_free(g_capture_allocation.tcb);
        g_capture_worker = nullptr;
        vSemaphoreDelete(g_catalog_mutex);
        g_catalog_mutex = nullptr;
        set_error("Recorder task creation failed", true);
        return false;
    }
    g_initialized = true;
    return true;
}

bool voice_notes_command(VoiceNoteCommand command, uint32_t token) {
    if (!g_initialized || !token) return false;
    const uint64_t queued_us = command == VoiceNoteCommand::Save ? (uint64_t)esp_timer_get_time() : 0;
    portENTER_CRITICAL(&g_mux);
    const VoiceNoteState state = g_snapshot.state;
    const bool valid = !g_command_busy && !g_terminating && !g_cancel_requested &&
        ((command == VoiceNoteCommand::Start && (!g_snapshot.session_active || g_cleanup_blocked) &&
          (state == VoiceNoteState::Idle || state == VoiceNoteState::Error)) ||
         (!g_cleanup_blocked &&
          ((command == VoiceNoteCommand::Pause && state == VoiceNoteState::Recording) ||
           (command == VoiceNoteCommand::Resume && state == VoiceNoteState::Paused) ||
           ((command == VoiceNoteCommand::Save || command == VoiceNoteCommand::Discard) &&
            (state == VoiceNoteState::Recording || state == VoiceNoteState::Paused)))));
    if (!valid || ota_activity_is_busy()) {
        strlcpy(g_snapshot.error, "Recorder command unavailable", sizeof(g_snapshot.error));
        portEXIT_CRITICAL(&g_mux);
        return false;
    }
    if (command == VoiceNoteCommand::Start) {
        g_snapshot.state = VoiceNoteState::Starting;
        g_snapshot.duration_ms = 0;
        g_snapshot.error[0] = '\0';
        strlcpy(g_snapshot.scenario, "general", sizeof(g_snapshot.scenario));
        g_snapshot.session_active = true;
    } else {
        g_snapshot.error[0] = '\0';
        if (command == VoiceNoteCommand::Save) g_snapshot.state = VoiceNoteState::Saving;
        if (command == VoiceNoteCommand::Discard) g_snapshot.state = VoiceNoteState::Discarding;
    }
    g_command = command;
    g_command_queued_us = queued_us;
    g_token = token;
    g_command_pending = true;
    g_command_busy = true;
    portEXIT_CRITICAL(&g_mux);
    xTaskNotifyGive(g_worker);
    return true;
}

void voice_notes_snapshot(VoiceNoteSnapshot* out) {
    if (!out) return;
    portENTER_CRITICAL(&g_mux);
    *out = g_snapshot;
    portEXIT_CRITICAL(&g_mux);
}

const char* voice_notes_state_name(VoiceNoteState state) {
    switch (state) {
        case VoiceNoteState::Idle: return "idle";
        case VoiceNoteState::Starting: return "starting";
        case VoiceNoteState::Recording: return "recording";
        case VoiceNoteState::Paused: return "paused";
        case VoiceNoteState::Saving: return "saving";
        case VoiceNoteState::Discarding: return "discarding";
        case VoiceNoteState::Error: return "error";
    }
    return "error";
}

VoiceNoteCancelStatus voice_notes_cancel() {
    if (!g_initialized) return VoiceNoteCancelStatus::Ready;
    portENTER_CRITICAL(&g_mux);
    if (!g_snapshot.session_active) {
        portEXIT_CRITICAL(&g_mux);
        return VoiceNoteCancelStatus::Ready;
    }
    g_cancel_requested = true;
    portEXIT_CRITICAL(&g_mux);
    xTaskNotifyGive(g_worker);
    return VoiceNoteCancelStatus::Pending;
}

bool voice_notes_list(JsonDocument& output) {
    if (!g_initialized || xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE) return false;
    bool valid = g_store.copy_manifest(output);
    JsonArray entries = output["entries"].as<JsonArray>();
    valid = valid && !entries.isNull();
    if (valid) {
        for (JsonObject entry : entries) {
            const char* id = entry["id"];
            if (!id) { valid = false; break; }
            char path[kPathSize];
            snprintf(path, sizeof(path), "%s/%s.json", kDirectory, id);
            bool exists = Storage.exists(path);
            if (!exists) {
                snprintf(path, sizeof(path), "%s/%s.json.bak", kDirectory, id);
                exists = Storage.exists(path);
            }
            entry["has_transcript"] = exists;
        }
    }
    valid = valid && !output.overflowed();
    if (!valid) {
        output.clear();
        LOGE(TAG, "Cannot build note catalog");
    }
    xSemaphoreGive(g_catalog_mutex);
    return valid;
}

bool voice_notes_read_metadata(const char* id, JsonObject output) {
    if (!g_initialized || xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE) return false;
    const bool copied = g_store.copy_entry(id, output);
    xSemaphoreGive(g_catalog_mutex);
    if (!copied) LOGE(TAG, "Cannot read note metadata");
    return copied;
}
bool voice_notes_exists(const char* id) {
    if (!g_initialized || !id || xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE) return false;
    const bool result = g_store.exists(id);
    xSemaphoreGive(g_catalog_mutex);
    return result;
}
VoiceNoteFileStatus voice_notes_open(const char* id, File* file) {
    if (!g_initialized || !id || !file ||
        xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE) return VoiceNoteFileStatus::Error;
    VoiceNoteFileStatus result = VoiceNoteFileStatus::NotFound;
    if (g_store.exists(id)) {
        char* slot = nullptr;
        for (auto& reader_id : g_reader_ids) {
            if (!reader_id[0]) { slot = reader_id; break; }
        }
        if (!slot) result = VoiceNoteFileStatus::Busy;
        else {
            *file = Storage.open(g_store.data_path(id).c_str(), "r");
            if (*file) {
                strlcpy(slot, id, sizeof(g_reader_ids[0]));
                result = VoiceNoteFileStatus::Ready;
            } else result = VoiceNoteFileStatus::Error;
        }
    }
    xSemaphoreGive(g_catalog_mutex);
    return result;
}

void voice_notes_close(File* file, const char* id) {
    if (!file || !id || xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Cannot release note reader");
        return;
    }
    file->close();
    for (auto& reader_id : g_reader_ids) {
        if (strcmp(reader_id, id) == 0) { reader_id[0] = '\0'; break; }
    }
    xSemaphoreGive(g_catalog_mutex);
}

bool voice_notes_has_transcript(const char* id) {
    if (!g_initialized || !id || xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE) return false;
    char path[kPathSize];
    bool result = false;
    if (g_store.exists(id)) {
        snprintf(path, sizeof(path), "%s/%s.json", kDirectory, id);
        result = Storage.exists(path);
        if (!result) {
            snprintf(path, sizeof(path), "%s/%s.json.bak", kDirectory, id);
            result = Storage.exists(path);
        }
    }
    xSemaphoreGive(g_catalog_mutex);
    return result;
}

VoiceNoteFileStatus voice_notes_open_transcript(const char* id, File* file) {
    if (!g_initialized || !id || !file ||
        xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE) return VoiceNoteFileStatus::Error;
    VoiceNoteFileStatus result = VoiceNoteFileStatus::NotFound;
    if (g_store.exists(id)) {
        char path[kPathSize];
        snprintf(path, sizeof(path), "%s/%s.json", kDirectory, id);
        if (!Storage.exists(path)) snprintf(path, sizeof(path), "%s/%s.json.bak", kDirectory, id);
        if (Storage.exists(path)) {
            *file = Storage.open(path, "r");
            result = *file ? VoiceNoteFileStatus::Ready : VoiceNoteFileStatus::Error;
        }
    }
    xSemaphoreGive(g_catalog_mutex);
    return result;
}

bool voice_notes_write_transcript(const char* id, const uint8_t* json, size_t length) {
    if (!g_initialized || !id || !json || !length ||
        xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE) return false;
    bool leased = false;
    for (const auto& reader_id : g_reader_ids) leased |= strcmp(reader_id, id) == 0;
    const bool exists = leased && g_store.exists(id);
    xSemaphoreGive(g_catalog_mutex);
    if (!exists) return false;
    const StorageMutationGuard guard;
    if (!guard) return false;
    char path[kPathSize], backup[kPathSize];
    snprintf(path, sizeof(path), "%s/%s.json", kDirectory, id);
    snprintf(backup, sizeof(backup), "%s/%s.json.bak", kDirectory, id);
    if (!Storage.exists(path) && Storage.exists(backup) && !Storage.rename(backup, path)) {
        LOGE(TAG, "Cannot restore transcript backup for %s", id);
        return false;
    }
    if (!storage_write_atomic(path, json, length)) {
        LOGE(TAG, "Cannot save transcript for %s", id);
        return false;
    }
    return true;
}

VoiceNoteFileStatus voice_notes_remove(const char* id) {
    if (!g_initialized || !id || xSemaphoreTake(g_catalog_mutex, portMAX_DELAY) != pdTRUE)
        return VoiceNoteFileStatus::Error;
    VoiceNoteFileStatus result = VoiceNoteFileStatus::NotFound;
    if (g_store.exists(id)) {
        bool busy = false;
        for (const auto& reader_id : g_reader_ids) busy |= strcmp(reader_id, id) == 0;
        if (busy) result = VoiceNoteFileStatus::Busy;
        else {
            const StorageMutationGuard guard;
            bool removed = bool(guard);
            char path[kPathSize];
            for (const char* suffix : {".json", ".json.bak", ".json.tmp"}) {
                if (!removed) break;
                snprintf(path, sizeof(path), "%s/%s%s", kDirectory, id, suffix);
                if (Storage.exists(path) && !Storage.remove(path)) {
                    LOGE(TAG, "Cannot remove transcript for %s", id);
                    removed = false;
                    break;
                }
            }
            if (removed) removed = g_store.remove(id);
            result = removed ? VoiceNoteFileStatus::Ready : VoiceNoteFileStatus::Error;
        }
    }
    xSemaphoreGive(g_catalog_mutex);
    return result;
}
String voice_notes_path(const char* id) { return g_store.data_path(id); }

#endif // HAS_VOICE_NOTES
