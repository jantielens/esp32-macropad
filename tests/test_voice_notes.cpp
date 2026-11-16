#include "voice_notes.h"
#include "voice_notes_file.h"
#include "audio_input.h"
#include "rtos_task_utils.h"
#include "fs_health.h"
#include "storage_browser.h"
#include "fs_indexed_store.h"
#include "voice_wav.h"
#include "voice_notes_metadata.h"
#include "voice_notes_processing.h"
#include "voice_notes_transcription_request.h"
#include "voice_notes_enrichment_request.h"
#include "bounded_psram_json_allocator.h"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>
#include <esp_timer.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

FakeIndexedStorage LittleFS;
#if USE_SD_STORAGE
FakeIndexedStorage& SD_MMC = LittleFS;
#endif

namespace {
std::mutex completion_mutex;
std::condition_variable completion_cv;
std::map<uint32_t, bool> completions;
std::atomic<bool> owned{false};
std::atomic<bool> expired{false};
std::atomic<bool> block_start{false};
std::atomic<bool> start_entered{false};
std::atomic<bool> force_overrun{false};
std::atomic<bool> no_audio{false};
std::atomic<bool> block_read{false};
std::atomic<bool> read_entered{false};
std::atomic<bool> block_publication{false};
std::atomic<bool> publication_entered{false};
std::atomic<unsigned> reads{0};
std::atomic<unsigned> pauses{0};
std::atomic<bool> block_capacity{false};
std::atomic<bool> capacity_entered{false};
std::atomic<bool> block_write{false};
std::atomic<bool> write_entered{false};
std::atomic<bool> fail_stop{false};
std::atomic<bool> block_stop{false};
std::atomic<bool> stop_entered{false};
bool no_psram = false;
bool capture_psram_stack = false;
bool capture_internal_stack = false;
bool fail_transcript_write = false;
std::atomic<bool> block_transcription{false};
std::atomic<bool> transcription_entered{false};
std::atomic<bool> fail_transcription{false};
std::atomic<bool> block_enrichment{false};
std::atomic<bool> enrichment_entered{false};
std::atomic<bool> fail_enrichment{false};
std::atomic<unsigned> enrichment_requests{0};
VoiceNotesRequestOutcome simulated_enrichment_outcome = VoiceNotesRequestOutcome::NoteFailure;
uint64_t simulated_enrichment_cooldown = 0;
std::atomic<bool> wifi_connected{true};
const std::thread::id test_thread = std::this_thread::get_id();
std::atomic<bool> block_processing{false};
std::atomic<bool> processing_blocked{false};
std::mutex request_mutex;
std::vector<std::string> request_ids;
std::string failed_note;
VoiceNotesRequestOutcome simulated_outcome = VoiceNotesRequestOutcome::NoteFailure;
uint64_t simulated_cooldown = 0;
const char* transcription_text = "host transcript";
const char* enrichment_title = "Host title";
const char* enrichment_summary = "Host summary.";

void interrupt_candidate_scan(const char* path) {
    if (!strstr(path, ".json")) return;
    LittleFS.exists_hook = nullptr;
    assert(voice_notes_command(VoiceNoteCommand::Start, 2040));
}

void usage_hook() {
    if (!block_capacity) return;
    capacity_entered = true;
    while (block_capacity) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void write_hook(const char* path, size_t length) {
    if (!block_write || length <= VOICE_WAV_HEADER_BYTES || !strstr(path, ".wav.tmp")) return;
    write_entered = true;
    while (block_write) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void publication_hook(const char* from, const char*) {
    if (!strstr(from, ".wav.tmp")) return;
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
    assert(!ota_activity_try_begin());
    assert(!ota_activity_try_begin_network_transition());
#endif
    publication_entered = true;
    while (block_publication) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void publication_remove_hook(const char* path) {
    if (!strstr(path, ".wav") || strstr(path, ".wav.tmp")) return;
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
    assert(!ota_activity_try_begin());
    assert(!ota_activity_try_begin_network_transition());
#endif
}
}

bool storage_mount() { return true; }
bool storage_write_atomic(const char* path, const uint8_t* bytes, size_t length) {
    if (fail_transcript_write) return false;
    LittleFS.files[path] = std::vector<uint8_t>(bytes, bytes + length);
    return true;
}
void fs_health_get(FSHealthStats* out) { *out = {}; }
bool time_service_ready() { return false; }
HostWiFi WiFi;
int HostWiFi::status() const {
    if (block_processing && std::this_thread::get_id() != test_thread) {
        processing_blocked = true;
        while (block_processing) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return wifi_connected ? WL_CONNECTED : 0;
}
size_t heap_caps_get_free_size(uint32_t) { return 8 * 1024 * 1024; }
#if !(USE_SD_STORAGE && HAS_AUTO_SHUTDOWN)
bool ota_activity_is_busy() { return false; }
uint32_t ota_activity_epoch() { return 0; }
#endif
bool voice_notes_transcription_request(const VoiceNotesServiceConfig&, File& recording, uint32_t, uint64_t,
                                       VoiceNotesRequestResult& result) {
    bool simulate_failure = false;
    {
        std::lock_guard<std::mutex> lock(request_mutex);
        const std::string id(recording.name(), 16);
        request_ids.push_back(id);
        simulate_failure = id == failed_note;
        if (simulate_failure) {
            result.outcome = simulated_outcome;
            result.cooldown_ms = simulated_cooldown;
            result.error = "Simulated failure";
        }
    }
    transcription_entered = true;
    while (block_transcription) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (simulate_failure) return false;
    if (fail_transcription) { result.error = "Request failed"; return false; }
    const size_t size = strlen(transcription_text) + 1;
    result.text = static_cast<char*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    memcpy(result.text, transcription_text, size);
    result.status = 200;
    return true;
}
bool voice_notes_enrichment_request(const VoiceNotesServiceConfig&, const char* transcript, uint32_t, uint64_t,
                                    JsonDocument& content, VoiceNotesRequestResult& result) {
    assert(strcmp(transcript, transcription_text) == 0);
    ++enrichment_requests;
    enrichment_entered = true;
    while (block_enrichment) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (fail_enrichment) {
        result.error = "Simulated enrichment failure";
        result.outcome = simulated_enrichment_outcome;
        result.cooldown_ms = simulated_enrichment_cooldown;
        return false;
    }
    content["title"] = enrichment_title;
    content["summary"] = enrichment_summary;
    result.status = 200;
    result.outcome = VoiceNotesRequestOutcome::Success;
    return true;
}
uint32_t esp_random() { static uint32_t value = 0; return ++value; }
bool action_continuation_complete(uint32_t token, bool success) {
    if (expired) return false;
    std::lock_guard<std::mutex> lock(completion_mutex);
    completions[token] = success;
    completion_cv.notify_all();
    return true;
}
bool rtos_create_task_internal_stack(TaskFunction_t entry, const char* name, uint32_t stack,
                                     void* arg, UBaseType_t priority, TaskHandle_t* task,
                                     RtosTaskInternalAlloc*) {
    if (strcmp(name, "VoiceCapture") == 0) capture_internal_stack = true;
    return xTaskCreatePinnedToCore(entry, name, stack, arg, priority, task, 0) == pdPASS;
}
bool rtos_create_task_psram_stack(TaskFunction_t entry, const char* name, uint32_t stack,
                                  void* arg, UBaseType_t priority, TaskHandle_t* task,
                                  RtosTaskPsramAlloc*) {
    if (no_psram) return false;
    capture_psram_stack = true;
    return xTaskCreatePinnedToCore(entry, name, stack, arg, priority, task, 0) == pdPASS;
}
AudioInputFormat audio_input_format() { return {16000, 1, 16}; }
bool audio_input_capture_owned() { return owned; }
bool audio_input_start_capture() {
    start_entered = true;
    while (block_start) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    owned = true;
    return true;
}
bool audio_input_pause_capture() { ++pauses; return owned; }
bool audio_input_stop_capture() {
    if (block_stop) {
        stop_entered = true;
        while (block_stop) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (fail_stop) return false;
    owned = false;
    return true;
}
AudioInputReadStatus audio_input_capture_status() { return AudioInputReadStatus::Ok; }
AudioInputReadResult audio_input_read_frames(int16_t* samples, size_t count, uint32_t) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (audio_test_stopping) throw AudioTestStop{};
    read_entered = true;
    while (block_read) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (force_overrun) return {0, AudioInputReadStatus::Overrun};
    if (no_audio) return {0, AudioInputReadStatus::Timeout};
    ++reads;
    for (size_t i = 0; i < count; ++i) samples[i] = static_cast<int16_t>(i);
    return {count, AudioInputReadStatus::Ok};
}

static bool wait_completion(uint32_t token) {
    std::unique_lock<std::mutex> lock(completion_mutex);
    assert(completion_cv.wait_for(lock, std::chrono::seconds(3),
                                  [=] { return completions.count(token); }));
    return completions[token];
}

template <typename Predicate>
static void wait_until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(predicate());
}

static VoiceNoteSnapshot snapshot() {
    VoiceNoteSnapshot value{};
    voice_notes_snapshot(&value);
    return value;
}

int main(int argc, char**) {
    no_psram = argc > 1;
    LittleFS.usage_hook = usage_hook;
    LittleFS.write_hook = write_hook;
    assert(voice_notes_init());
    assert(capture_psram_stack == !no_psram && capture_internal_stack == no_psram);
    assert(voice_notes_command(VoiceNoteCommand::Start, 1));
    assert(wait_completion(1));
    assert(strcmp(snapshot().scenario, "general") == 0);
    wait_until([] { return reads.load() > 0; });
    assert(voice_notes_command(VoiceNoteCommand::Pause, 2));
    assert(wait_completion(2));
    assert(snapshot().state == VoiceNoteState::Paused && owned);
    const uint64_t paused_duration = snapshot().duration_ms;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(snapshot().duration_ms == paused_duration);
    assert(voice_notes_command(VoiceNoteCommand::Resume, 3));
    assert(wait_completion(3));
    assert(voice_notes_command(VoiceNoteCommand::Save, 4));
    assert(wait_completion(4));
    VoiceNoteSnapshot saved = snapshot();
    assert(saved.state == VoiceNoteState::Idle && !owned && saved.last_clip[0]);
    assert(!saved.scenario[0]);
    assert(voice_notes_exists(saved.last_clip));
    JsonDocument catalog;
    assert(voice_notes_list(catalog));
    assert(!catalog["entries"][0]["has_transcript"].as<bool>());
    BoundedPsramJsonAllocator exhausted_allocator(0);
    JsonDocument unavailable_catalog(&exhausted_allocator);
    assert(!voice_notes_list(unavailable_catalog));
    assert(unavailable_catalog.isNull());
    assert(voice_notes_exists(saved.last_clip));
    JsonDocument note;
    assert(voice_notes_read_metadata(saved.last_clip, note.to<JsonObject>()));
    assert(strcmp(note["id"], saved.last_clip) == 0);
    assert(!note.containsKey("entries"));
    assert(!voice_notes_read_metadata("missing", note.to<JsonObject>()));
    const String path = voice_notes_path(saved.last_clip);
    const auto& file = LittleFS.files.at(path.c_str());
    VoiceWavInfo info{};
    assert(voice_wav_read_header(file.data(), file.size(), file.size(), &info));
    assert(info.sample_rate == 16000 && info.channels == 1 && info.data_bytes > 0);
    assert(info.data_bytes % 2048 == 0);
    assert(saved.duration_ms == static_cast<uint64_t>(info.data_bytes / 2) * 1000 / 16000);
    for (size_t offset = VOICE_WAV_HEADER_BYTES; offset < file.size(); offset += 2) {
        const unsigned sample = file[offset] | (unsigned(file[offset + 1]) << 8);
        assert(sample == ((offset - VOICE_WAV_HEADER_BYTES) / 2) % 1024);
    }
    File readers[4], other;
    for (auto& reader : readers)
        assert(voice_notes_open(saved.last_clip, &reader) == VoiceNoteFileStatus::Ready);
    assert(voice_notes_open(saved.last_clip, &other) == VoiceNoteFileStatus::Busy);
    assert(voice_notes_remove(saved.last_clip) == VoiceNoteFileStatus::Busy);
    for (auto& reader : readers) voice_notes_close(&reader, saved.last_clip);
    assert(voice_notes_open("missing", &other) == VoiceNoteFileStatus::NotFound);
    const std::vector<uint8_t> original_wav = LittleFS.files.at(path.c_str());
    const char first_transcript[] = R"({"transcript":{"text":"first","model":"test","created_at":0}})";
    const char next_transcript[] = R"({"transcript":{"text":"next","model":"test","created_at":1}})";
    assert(!voice_notes_write_artifact(saved.last_clip, VoiceNoteArtifact::Transcript,
        reinterpret_cast<const uint8_t*>(first_transcript), strlen(first_transcript)));
    assert(voice_notes_open(saved.last_clip, &other) == VoiceNoteFileStatus::Ready);
    assert(voice_notes_write_artifact(saved.last_clip, VoiceNoteArtifact::Transcript,
        reinterpret_cast<const uint8_t*>(first_transcript), strlen(first_transcript)));
    assert(voice_notes_has_artifact(saved.last_clip, VoiceNoteArtifact::Transcript));
    catalog.clear();
    assert(voice_notes_list(catalog));
    assert(catalog["entries"][0]["has_transcript"].as<bool>());
    assert(voice_notes_remove(saved.last_clip) == VoiceNoteFileStatus::Busy);
    const String sidecar = String("/storage/voice-notes/") + saved.last_clip + ".json";
    fail_transcript_write = true;
    assert(!voice_notes_write_artifact(saved.last_clip, VoiceNoteArtifact::Transcript,
        reinterpret_cast<const uint8_t*>(next_transcript), strlen(next_transcript)));
    fail_transcript_write = false;
    assert(std::string(LittleFS.files[sidecar.c_str()].begin(), LittleFS.files[sidecar.c_str()].end()) == first_transcript);
    assert(LittleFS.files.at(path.c_str()) == original_wav);
    const String backup = sidecar + ".bak";
    assert(LittleFS.rename(sidecar.c_str(), backup.c_str()));
    assert(voice_notes_has_artifact(saved.last_clip, VoiceNoteArtifact::Transcript));
    File transcript;
    assert(voice_notes_open_artifact(saved.last_clip, VoiceNoteArtifact::Transcript, &transcript) == VoiceNoteFileStatus::Ready);
    assert(transcript.readString() == first_transcript);
    transcript.close();
    assert(voice_notes_write_artifact(saved.last_clip, VoiceNoteArtifact::Transcript,
        reinterpret_cast<const uint8_t*>(next_transcript), strlen(next_transcript)));
    assert(!LittleFS.exists(backup.c_str()));
    if (!no_psram) {
        VoiceNoteCursor next{};
        assert(voice_notes_next_candidate({}, &next, false, true) == VoiceNoteCandidate::Found);
        assert(strcmp(next.id, saved.last_clip) == 0);
    }
    const char enrichment[] = R"({"enrichment":{"title":"First title","summary":"First summary","model":"test","created_at":0}})";
    assert(voice_notes_write_artifact(saved.last_clip, VoiceNoteArtifact::Enrichment,
        reinterpret_cast<const uint8_t*>(enrichment), strlen(enrichment)));
    assert(voice_notes_has_artifact(saved.last_clip, VoiceNoteArtifact::Enrichment));
    if (!no_psram) {
        VoiceNoteCursor next{};
        assert(voice_notes_next_candidate({}, &next, false, true) == VoiceNoteCandidate::None);
        catalog.clear();
        assert(voice_notes_list(catalog));
        assert(catalog["entries"][0]["has_enrichment"].as<bool>());
        assert(strcmp(catalog["entries"][0]["title"], "First title") == 0);
        assert(strcmp(catalog["entries"][0]["enrichment_error"], "") == 0);
    }
    const String enrichment_path = String("/storage/voice-notes/") + saved.last_clip + ".enrichment.json";
    const String enrichment_backup = enrichment_path + ".bak";
    assert(LittleFS.rename(enrichment_path.c_str(), enrichment_backup.c_str()));
    File enrichment_file;
    assert(voice_notes_open_artifact(saved.last_clip, VoiceNoteArtifact::Enrichment, &enrichment_file)
        == VoiceNoteFileStatus::Ready);
    assert(enrichment_file.readString() == enrichment);
    enrichment_file.close();
    fail_transcript_write = true;
    assert(!voice_notes_write_artifact(saved.last_clip, VoiceNoteArtifact::Enrichment,
        reinterpret_cast<const uint8_t*>(enrichment), strlen(enrichment)));
    fail_transcript_write = false;
    assert(LittleFS.files.at(enrichment_path.c_str()) ==
        std::vector<uint8_t>(enrichment, enrichment + strlen(enrichment)));
    if (!no_psram) {
        LittleFS.files[enrichment_path.c_str()] = {'{'};
        catalog.clear();
        assert(voice_notes_list(catalog));
        assert(catalog["entries"][0]["has_enrichment"].as<bool>());
        assert(catalog["entries"][0]["title"].isNull());
        assert(strlen(catalog["entries"][0]["enrichment_error"]) > 0);
        LittleFS.files[enrichment_path.c_str()] =
            std::vector<uint8_t>(enrichment, enrichment + strlen(enrichment));
    }
    voice_notes_close(&other, saved.last_clip);
    assert(LittleFS.files.at(path.c_str()) == original_wav);
    if (!no_psram) {
        VoiceNotesConfig config{};
        assert(voice_notes_normalize_endpoint("https://example.test", config.transcription.endpoint, sizeof(config.transcription.endpoint)));
        assert(strcmp(config.transcription.endpoint, "https://example.test/") == 0);
        char endpoint[512];
        assert(!voice_notes_normalize_endpoint("http://example.test", endpoint, sizeof(endpoint)));
        assert(!voice_notes_normalize_endpoint("https://example.test/path", endpoint, sizeof(endpoint)));
        assert(!voice_notes_normalize_endpoint("https://user@example.test/", endpoint, sizeof(endpoint)));
        assert(!voice_notes_normalize_endpoint("https://:443/", endpoint, sizeof(endpoint)));
        assert(!voice_notes_normalize_endpoint("https://example.test:abc/", endpoint, sizeof(endpoint)));
        assert(!voice_notes_normalize_endpoint("https://example.test:65536/", endpoint, sizeof(endpoint)));
        strlcpy(config.transcription.api_key, "key", sizeof(config.transcription.api_key));
        strlcpy(config.transcription.deployment, "original", sizeof(config.transcription.deployment));
        assert(voice_notes_processing_configure(config));
        assert(voice_notes_processing_init());
        const auto before_early_interruption = LittleFS.files.at(sidecar.c_str());
        for (bool recording : {false, true}) {
            processing_blocked = false;
            block_processing = true;
            assert(voice_notes_processing_start(saved.last_clip, VoiceNotesProcessingMode::Retranscribe) ==
                   VoiceNotesProcessingStart::Accepted);
            wait_until([] { return processing_blocked.load(); });
            if (recording) {
                assert(voice_notes_command(VoiceNoteCommand::Start, 2070));
                assert(wait_completion(2070));
            } else wifi_connected = false;
            block_processing = false;
            wait_until([] {
                VoiceNotesProcessingSnapshot state{};
                voice_notes_processing_snapshot(&state);
                return state.state == VoiceNotesProcessingState::Idle;
            });
            VoiceNotesTranscriptionSnapshot interrupted{};
            voice_notes_transcription_snapshot(&interrupted);
            assert(interrupted.state == VoiceNotesTranscriptionState::Error);
            assert(strcmp(interrupted.note_id, saved.last_clip) == 0 && interrupted.error[0]);
            assert(!transcription_entered);
            assert(LittleFS.files.at(sidecar.c_str()) == before_early_interruption);
            if (recording) {
                assert(voice_notes_command(VoiceNoteCommand::Discard, 2071));
                assert(wait_completion(2071));
            } else wifi_connected = true;
        }
        block_transcription = true;
        assert(voice_notes_processing_start(saved.last_clip, VoiceNotesProcessingMode::Retranscribe) == VoiceNotesProcessingStart::Accepted);
        wait_until([] { return transcription_entered.load(); });
        assert(voice_notes_processing_start(saved.last_clip, VoiceNotesProcessingMode::Retranscribe) == VoiceNotesProcessingStart::Busy);
        assert(voice_notes_remove(saved.last_clip) == VoiceNoteFileStatus::Busy);
        strlcpy(config.transcription.deployment, "updated", sizeof(config.transcription.deployment));
        assert(voice_notes_processing_configure(config));
        block_transcription = false;
        VoiceNotesTranscriptionSnapshot processing{};
        wait_until([&] {
            voice_notes_transcription_snapshot(&processing);
            return processing.state == VoiceNotesTranscriptionState::Ready;
        });
        JsonDocument persisted;
        File result_file;
        assert(voice_notes_open_artifact(saved.last_clip, VoiceNoteArtifact::Transcript, &result_file) == VoiceNoteFileStatus::Ready);
        assert(!deserializeJson(persisted, result_file));
        result_file.close();
        assert(strcmp(persisted["transcript"]["text"], "host transcript") == 0);
        assert(strcmp(persisted["transcript"]["model"], "original") == 0);
        const auto previous = LittleFS.files.at(sidecar.c_str());
        fail_transcription = true;
        bool accepted = false;
        wait_until([&] {
            if (!accepted)
                accepted = voice_notes_processing_start(saved.last_clip, VoiceNotesProcessingMode::Retranscribe) == VoiceNotesProcessingStart::Accepted;
            return accepted;
        });
        wait_until([&] {
            voice_notes_transcription_snapshot(&processing);
            return processing.state == VoiceNotesTranscriptionState::Error;
        });
        assert(LittleFS.files.at(sidecar.c_str()) == previous);
        assert(LittleFS.files.at(path.c_str()) == original_wav);
        fail_transcription = false;
        fail_transcript_write = true;
        accepted = false;
        wait_until([&] {
            if (!accepted)
                accepted = voice_notes_processing_start(saved.last_clip, VoiceNotesProcessingMode::Retranscribe) == VoiceNotesProcessingStart::Accepted;
            return accepted;
        });
        wait_until([&] {
            voice_notes_transcription_snapshot(&processing);
            return processing.state == VoiceNotesTranscriptionState::Error;
        });
        fail_transcript_write = false;
        assert(LittleFS.files.at(sidecar.c_str()) == previous);
        transcription_entered = false;
        block_transcription = true;
        accepted = false;
        wait_until([&] {
            if (!accepted)
                accepted = voice_notes_processing_start(saved.last_clip, VoiceNotesProcessingMode::Retranscribe) == VoiceNotesProcessingStart::Accepted;
            return accepted;
        });
        wait_until([] { return transcription_entered.load(); });
        assert(voice_notes_command(VoiceNoteCommand::Start, 1001));
        assert(wait_completion(1001));
        assert(voice_notes_command(VoiceNoteCommand::Discard, 1002));
        assert(wait_completion(1002));
        block_transcription = false;
        wait_until([&] {
            voice_notes_transcription_snapshot(&processing);
            return processing.state == VoiceNotesTranscriptionState::Error;
        });
        assert(LittleFS.files.at(sidecar.c_str()) == previous);
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
        transcription_entered = false;
        block_transcription = true;
        accepted = false;
        wait_until([&] {
            if (!accepted)
                accepted = voice_notes_processing_start(saved.last_clip, VoiceNotesProcessingMode::Retranscribe) == VoiceNotesProcessingStart::Accepted;
            return accepted;
        });
        wait_until([] { return transcription_entered.load(); });
        assert(ota_activity_try_begin());
        ota_activity_finish();
        block_transcription = false;
        wait_until([&] {
            voice_notes_transcription_snapshot(&processing);
            return processing.state == VoiceNotesTranscriptionState::Error;
        });
        assert(LittleFS.files.at(sidecar.c_str()) == previous);
#endif
    }
    assert(LittleFS.remove("/storage/voice-notes/_index.json"));
    const char* const fields[] = {"size_bytes", "duration_ms", "sample_rate", "channels"};
    FsIndexedStore rebuilt("/storage/voice-notes", fields, 4, nullptr, 0, ".wav",
                           voice_notes_metadata_read_metadata);
    assert(rebuilt.begin() && rebuilt.exists(saved.last_clip));
    assert(voice_notes_has_artifact(saved.last_clip, VoiceNoteArtifact::Transcript));
    assert(!storage_browser_path_is_safe("/storage/./voice-notes/file.wav.tmp"));
    assert(!storage_browser_path_is_safe("/./storage/voice-notes"));
    assert(!storage_browser_path_is_safe("/storage/."));
    assert(storage_browser_path_is_safe("/storage/other"));
    const char* storage_error = nullptr;
    JsonDocument listing;
    assert(storage_browser_list("/storage/voice-notes", listing.to<JsonObject>(), storage_error));
    bool found_clip = false;
    for (JsonObject item : listing["entries"].as<JsonArray>())
        found_clip |= item["path"].as<String>() == path.c_str();
    assert(found_clip);
    const String extra_path = "/storage/voice-notes/extra.wav";
    LittleFS.files[extra_path.c_str()] = {1, 2, 3};
    assert(storage_browser_remove(extra_path, storage_error));
    assert(!LittleFS.exists(extra_path.c_str()));
    assert(voice_notes_exists(saved.last_clip));

    block_start = true;
    start_entered = false;
    assert(voice_notes_command(VoiceNoteCommand::Start, 5));
    wait_until([] { return start_entered.load(); });
    assert(strcmp(snapshot().last_clip, saved.last_clip) == 0);
    assert(voice_notes_cancel() == VoiceNoteCancelStatus::Pending);
    assert(!voice_notes_command(VoiceNoteCommand::Pause, 6));
    block_start = false;
    assert(!wait_completion(5));
    wait_until([] { return voice_notes_cancel() == VoiceNoteCancelStatus::Ready; });
    assert(snapshot().state == VoiceNoteState::Idle && !owned);
    assert(voice_notes_exists(saved.last_clip));
    for (const auto& entry : LittleFS.files)
        assert(entry.first.find(".wav.tmp") == std::string::npos);

    expired = true;
    assert(voice_notes_command(VoiceNoteCommand::Start, 7));
    wait_until([] { return snapshot().state == VoiceNoteState::Error && !snapshot().session_active; });
    expired = false;
    assert(voice_notes_command(VoiceNoteCommand::Start, 8));
    assert(wait_completion(8));
    force_overrun = true;
    wait_until([] { return snapshot().state == VoiceNoteState::Error && !snapshot().session_active; });
    force_overrun = false;
    assert(voice_notes_exists(saved.last_clip));

    no_audio = true;
    assert(voice_notes_command(VoiceNoteCommand::Start, 9));
    assert(wait_completion(9));
    assert(voice_notes_command(VoiceNoteCommand::Save, 10));
    assert(!wait_completion(10));
    assert(snapshot().state == VoiceNoteState::Error && !owned);
    assert(strcmp(snapshot().error, "No audio captured") == 0);
    assert(strcmp(snapshot().last_clip, saved.last_clip) == 0);
    no_audio = false;

    assert(voice_notes_command(VoiceNoteCommand::Start, 11));
    assert(wait_completion(11));
    const unsigned prior_reads = reads;
    wait_until([=] { return reads.load() > prior_reads; });
    assert(voice_notes_command(VoiceNoteCommand::Pause, 12));
    assert(wait_completion(12));
    LittleFS.short_write = true;
    assert(voice_notes_command(VoiceNoteCommand::Save, 13));
    assert(!wait_completion(13));
    LittleFS.short_write = false;
    assert(snapshot().state == VoiceNoteState::Error && !owned);
    assert(strcmp(snapshot().last_clip, saved.last_clip) == 0);
    assert(voice_notes_exists(saved.last_clip));

    assert(voice_notes_command(VoiceNoteCommand::Start, 14));
    assert(wait_completion(14));
    assert(voice_notes_command(VoiceNoteCommand::Discard, 15));
    assert(wait_completion(15));
    assert(snapshot().state == VoiceNoteState::Idle && !owned);
    assert(strcmp(snapshot().last_clip, saved.last_clip) == 0);
    assert(voice_notes_exists(saved.last_clip));

    block_read = true;
    read_entered = false;
    assert(voice_notes_command(VoiceNoteCommand::Start, 16));
    assert(wait_completion(16));
    wait_until([] { return read_entered.load(); });
    assert(voice_notes_command(VoiceNoteCommand::Pause, 17));
    force_overrun = true;
    block_read = false;
    assert(!wait_completion(17));
    force_overrun = false;
    assert(snapshot().state == VoiceNoteState::Error && !owned);

    no_audio = true;
    assert(voice_notes_command(VoiceNoteCommand::Start, 18));
    assert(wait_completion(18));
    wait_until([] { return snapshot().state == VoiceNoteState::Error && !snapshot().session_active; });
    assert(strcmp(snapshot().error, "Microphone capture stalled") == 0);
    no_audio = false;

    LittleFS.capacity = 65536;
    assert(voice_notes_command(VoiceNoteCommand::Start, 19));
    assert(!wait_completion(19));
    assert(strcmp(snapshot().error, "Insufficient recording storage") == 0);
    LittleFS.capacity = 1024 * 1024;
    assert(voice_notes_exists(saved.last_clip));

    assert(voice_notes_command(VoiceNoteCommand::Start, 20));
    assert(wait_completion(20));
    const unsigned reads_before_save = reads;
    wait_until([=] { return reads.load() > reads_before_save; });
    assert(voice_notes_command(VoiceNoteCommand::Pause, 21));
    assert(wait_completion(21));
    block_publication = true;
    publication_entered = false;
    LittleFS.rename_hook = publication_hook;
    LittleFS.remove_hook = publication_remove_hook;
    assert(voice_notes_command(VoiceNoteCommand::Save, 22));
    wait_until([] { return publication_entered.load(); });
    assert(voice_notes_cancel() == VoiceNoteCancelStatus::Pending);
    block_publication = false;
    assert(!wait_completion(22));
    LittleFS.rename_hook = nullptr;
    LittleFS.remove_hook = nullptr;
    assert(voice_notes_cancel() == VoiceNoteCancelStatus::Ready);
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
    assert(ota_activity_try_begin());
    ota_activity_finish();
#endif
    assert(snapshot().state == VoiceNoteState::Idle);
    assert(strcmp(snapshot().last_clip, saved.last_clip) == 0);
    assert(voice_notes_exists(saved.last_clip));
    for (const auto& entry : LittleFS.files)
        assert(entry.first.find(".wav.tmp") == std::string::npos);

    LittleFS.capacity = 10 * 1024 * 1024;
    const unsigned before_buffered = reads;
    assert(voice_notes_command(VoiceNoteCommand::Start, 23));
    assert(wait_completion(23));
    capacity_entered = false;
    block_capacity = true;
    wait_until([] { return capacity_entered.load(); });
    const unsigned while_blocked = reads;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(reads.load() > while_blocked);
    block_capacity = false;
    assert(voice_notes_command(VoiceNoteCommand::Pause, 24));
    assert(wait_completion(24));
    const unsigned buffered_frames = reads - before_buffered;
    assert(voice_notes_command(VoiceNoteCommand::Save, 25));
    assert(wait_completion(25));
    const VoiceNoteSnapshot buffered = snapshot();
    assert(strcmp(buffered.last_clip, saved.last_clip) != 0);
    const String buffered_path = voice_notes_path(buffered.last_clip);
    const auto& buffered_file = LittleFS.files.at(buffered_path.c_str());
    assert(voice_wav_read_header(buffered_file.data(), buffered_file.size(), buffered_file.size(), &info));
    assert(info.data_bytes == buffered_frames * 2048);

    block_write = true;
    write_entered = false;
    assert(voice_notes_command(VoiceNoteCommand::Start, 26));
    assert(wait_completion(26));
    wait_until([] { return write_entered.load(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    block_write = false;
    wait_until([] { return snapshot().state == VoiceNoteState::Error && !snapshot().session_active; });
    assert(strcmp(snapshot().last_clip, buffered.last_clip) == 0);
    assert(strcmp(snapshot().error, "Recording buffer overrun") == 0);

    block_write = true;
    write_entered = false;
    assert(voice_notes_command(VoiceNoteCommand::Start, 27));
    assert(wait_completion(27));
    wait_until([] { return write_entered.load(); });
    assert(voice_notes_cancel() == VoiceNoteCancelStatus::Pending);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(voice_notes_cancel() == VoiceNoteCancelStatus::Pending);
    block_write = false;
    wait_until([] { return voice_notes_cancel() == VoiceNoteCancelStatus::Ready; });
    assert(strcmp(snapshot().last_clip, buffered.last_clip) == 0 && !owned);

    assert(voice_notes_command(VoiceNoteCommand::Start, 28));
    assert(wait_completion(28));
    fail_stop = true;
    assert(voice_notes_command(VoiceNoteCommand::Discard, 29));
    assert(!wait_completion(29));
    assert(snapshot().session_active && owned);
    assert(voice_notes_command(VoiceNoteCommand::Start, 40));
    assert(!wait_completion(40));
    assert(snapshot().session_active && owned);
    fail_stop = false;
    assert(voice_notes_command(VoiceNoteCommand::Start, 41));
    assert(wait_completion(41));
    assert(snapshot().state == VoiceNoteState::Recording && owned);
    assert(voice_notes_command(VoiceNoteCommand::Discard, 42));
    assert(wait_completion(42));
    assert(!snapshot().session_active && !owned);

    assert(voice_notes_command(VoiceNoteCommand::Start, 43));
    assert(wait_completion(43));
    fail_stop = true;
    assert(voice_notes_command(VoiceNoteCommand::Discard, 44));
    assert(!wait_completion(44));
    fail_stop = false;
    stop_entered = false;
    block_stop = true;
    assert(voice_notes_command(VoiceNoteCommand::Start, 45));
    wait_until([] { return stop_entered.load(); });
    assert(voice_notes_cancel() == VoiceNoteCancelStatus::Pending);
    block_stop = false;
    assert(!wait_completion(45));
    wait_until([] { return voice_notes_cancel() == VoiceNoteCancelStatus::Ready; });
    assert(!snapshot().session_active && !owned);

    assert(voice_notes_command(VoiceNoteCommand::Start, 30));
    assert(wait_completion(30));
    const unsigned before_limit = reads;
    wait_until([=] { return reads.load() > before_limit; });
    block_read = true;
    read_entered = false;
    wait_until([] { return read_entered.load(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    LittleFS.capacity = LittleFS.usedBytes() + 65536;
    assert(voice_notes_command(VoiceNoteCommand::Pause, 31));
    block_read = false;
    assert(wait_completion(31));
    assert(snapshot().state == VoiceNoteState::Idle && snapshot().last_clip[0]);
    assert(voice_notes_exists(snapshot().last_clip));

    assert(voice_notes_open(buffered.last_clip, &other) == VoiceNoteFileStatus::Ready);
    assert(voice_notes_write_artifact(buffered.last_clip, VoiceNoteArtifact::Transcript,
        reinterpret_cast<const uint8_t*>(first_transcript), strlen(first_transcript)));
    assert(voice_notes_write_artifact(buffered.last_clip, VoiceNoteArtifact::Enrichment,
        reinterpret_cast<const uint8_t*>(enrichment), strlen(enrichment)));
    voice_notes_close(&other, buffered.last_clip);
    const String buffered_sidecar = String("/storage/voice-notes/") + buffered.last_clip + ".json";
    LittleFS.fail_remove_path = buffered_sidecar.c_str();
    assert(voice_notes_remove(buffered.last_clip) == VoiceNoteFileStatus::Error);
    assert(voice_notes_exists(buffered.last_clip));
    LittleFS.fail_remove_path.clear();
    assert(voice_notes_remove(buffered.last_clip) == VoiceNoteFileStatus::Ready);
    assert(!LittleFS.exists(buffered_sidecar.c_str()));
    assert(!LittleFS.exists((String("/storage/voice-notes/") + buffered.last_clip + ".enrichment.json").c_str()));
    assert(!LittleFS.exists(buffered_path.c_str()));

    if (!no_psram) {
        assert(voice_notes_remove(snapshot().last_clip) == VoiceNoteFileStatus::Ready);
        LittleFS.capacity = 10 * 1024 * 1024;
        std::vector<std::string> pass_ids;
        for (uint32_t token = 2000; token < 2006; token += 2) {
            assert(voice_notes_command(VoiceNoteCommand::Start, token));
            assert(wait_completion(token));
            const unsigned before = reads;
            wait_until([=] { return reads.load() > before; });
            assert(voice_notes_command(VoiceNoteCommand::Save, token + 1));
            assert(wait_completion(token + 1));
            pass_ids.emplace_back(snapshot().last_clip);
        }
        VoiceNoteCursor candidate = {};
        assert(voice_notes_next_candidate({}, &candidate) == VoiceNoteCandidate::Found);
        assert(voice_notes_command(VoiceNoteCommand::Start, 2038));
        assert(wait_completion(2038));
        assert(voice_notes_next_candidate({}, &candidate) == VoiceNoteCandidate::Interrupted);
        assert(voice_notes_command(VoiceNoteCommand::Discard, 2039));
        assert(wait_completion(2039));
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
        assert(ota_activity_try_begin());
        assert(voice_notes_next_candidate({}, &candidate) == VoiceNoteCandidate::Interrupted);
        ota_activity_finish();
#endif
        LittleFS.exists_hook = interrupt_candidate_scan;
        assert(voice_notes_next_candidate({}, &candidate) == VoiceNoteCandidate::Interrupted);
        assert(wait_completion(2040));
        assert(voice_notes_command(VoiceNoteCommand::Discard, 2041));
        assert(wait_completion(2041));
        assert(voice_notes_next_candidate({}, &candidate) == VoiceNoteCandidate::Found);
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            request_ids.clear();
            failed_note = pass_ids[1];
            simulated_outcome = VoiceNotesRequestOutcome::SharedFailure;
        }
        wifi_connected = false;
        assert(voice_notes_processing_start() == VoiceNotesProcessingStart::Offline);
        wifi_connected = true;
        assert(voice_notes_processing_start() == VoiceNotesProcessingStart::Accepted);
        wait_until([&] {
            std::lock_guard<std::mutex> lock(request_mutex);
            return request_ids.size() == 2;
        });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            assert((request_ids == std::vector<std::string>{pass_ids[0], pass_ids[1]}));
        }
        assert(voice_notes_has_artifact(pass_ids[0].c_str(), VoiceNoteArtifact::Transcript));
        assert(!voice_notes_has_artifact(pass_ids[1].c_str(), VoiceNoteArtifact::Transcript));
        assert(!voice_notes_has_artifact(pass_ids[2].c_str(), VoiceNoteArtifact::Transcript));
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            request_ids.clear();
            simulated_outcome = VoiceNotesRequestOutcome::NoteFailure;
        }
        assert(voice_notes_processing_start() == VoiceNotesProcessingStart::Accepted);
        wait_until([&] {
            std::lock_guard<std::mutex> lock(request_mutex);
            return request_ids.size() == 2;
        });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            assert((request_ids == std::vector<std::string>{pass_ids[1], pass_ids[2]}));
        }
        assert(!voice_notes_has_artifact(pass_ids[1].c_str(), VoiceNoteArtifact::Transcript));
        assert(voice_notes_has_artifact(pass_ids[2].c_str(), VoiceNoteArtifact::Transcript));
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            failed_note.clear();
            request_ids.clear();
        }
        assert(voice_notes_processing_start() == VoiceNotesProcessingStart::Accepted);
        wait_until([&] { return voice_notes_has_artifact(pass_ids[1].c_str(), VoiceNoteArtifact::Transcript); });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            assert(request_ids.size() == 1 && request_ids[0] == pass_ids[1]);
        }
        for (const auto& id : pass_ids) assert(voice_notes_remove(id.c_str()) == VoiceNoteFileStatus::Ready);

        assert(voice_notes_command(VoiceNoteCommand::Start, 2050));
        assert(wait_completion(2050));
        const unsigned before_enrichment_note = reads;
        wait_until([=] { return reads.load() > before_enrichment_note; });
        assert(voice_notes_command(VoiceNoteCommand::Save, 2051));
        assert(wait_completion(2051));
        const std::string enrichment_id = snapshot().last_clip;
        VoiceNotesConfig both{};
        strlcpy(both.transcription.endpoint, "https://example.test/", sizeof(both.transcription.endpoint));
        strlcpy(both.transcription.api_key, "key", sizeof(both.transcription.api_key));
        strlcpy(both.transcription.deployment, "transcription", sizeof(both.transcription.deployment));
        strlcpy(both.llm.endpoint, "https://example.test/", sizeof(both.llm.endpoint));
        strlcpy(both.llm.api_key, "key", sizeof(both.llm.api_key));
        strlcpy(both.llm.deployment, "llm", sizeof(both.llm.deployment));
        assert(voice_notes_processing_configure(both));
        block_enrichment = true;
        fail_enrichment = true;
        enrichment_entered = false;
        const auto processing_started = std::chrono::steady_clock::now();
        assert(voice_notes_processing_start(enrichment_id.c_str(), VoiceNotesProcessingMode::Missing) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([&] {
            VoiceNotesTranscriptionSnapshot transcription{};
            voice_notes_transcription_snapshot(&transcription);
            VoiceNotesProcessingSnapshot processing{};
            voice_notes_processing_snapshot(&processing);
            return strcmp(transcription.note_id, enrichment_id.c_str()) == 0 &&
                   transcription.state == VoiceNotesTranscriptionState::Ready &&
                   processing.state == VoiceNotesProcessingState::Running &&
                   processing.service == VoiceNotesProcessingService::None;
        });
        assert(!enrichment_entered);
        File gap_readers[4];
        for (auto& reader : gap_readers)
            assert(voice_notes_open(enrichment_id.c_str(), &reader) == VoiceNoteFileStatus::Ready);
        for (auto& reader : gap_readers) voice_notes_close(&reader, enrichment_id.c_str());
        assert(voice_notes_processing_start() == VoiceNotesProcessingStart::Busy);
        wait_until([] { return enrichment_entered.load(); });
        assert(std::chrono::steady_clock::now() - processing_started >= std::chrono::seconds(2));
        assert(voice_notes_has_artifact(enrichment_id.c_str(), VoiceNoteArtifact::Transcript));
        assert(!voice_notes_has_artifact(enrichment_id.c_str(), VoiceNoteArtifact::Enrichment));
        assert(voice_notes_remove(enrichment_id.c_str()) == VoiceNoteFileStatus::Busy);
        block_enrichment = false;
        wait_until([] {
            VoiceNotesEnrichmentSnapshot state{};
            voice_notes_enrichment_snapshot(&state);
            return state.state == VoiceNotesEnrichmentState::Error;
        });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        unsigned transcription_count;
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            transcription_count = request_ids.size();
        }
        fail_enrichment = false;
        both.transcription = {};
        assert(voice_notes_processing_configure(both));
        assert(voice_notes_processing_start(enrichment_id.c_str(), VoiceNotesProcessingMode::Missing) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([&] { return voice_notes_has_artifact(enrichment_id.c_str(), VoiceNoteArtifact::Enrichment); });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            assert(request_ids.size() == transcription_count);
        }
        const String generated_path = String("/storage/voice-notes/") + enrichment_id.c_str() + ".enrichment.json";
        const auto generated = LittleFS.files.at(generated_path.c_str());
        const unsigned before_early_enrichment = enrichment_requests;
        processing_blocked = false;
        block_processing = true;
        assert(voice_notes_processing_start(enrichment_id.c_str(), VoiceNotesProcessingMode::Regenerate) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([] { return processing_blocked.load(); });
        wifi_connected = false;
        block_processing = false;
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        VoiceNotesEnrichmentSnapshot interrupted{};
        voice_notes_enrichment_snapshot(&interrupted);
        assert(interrupted.state == VoiceNotesEnrichmentState::Error && interrupted.error[0]);
        assert(strcmp(interrupted.note_id, enrichment_id.c_str()) == 0);
        assert(enrichment_requests == before_early_enrichment);
        assert(LittleFS.files.at(generated_path.c_str()) == generated);
        wifi_connected = true;
        simulated_enrichment_outcome = VoiceNotesRequestOutcome::RateLimited;
        simulated_enrichment_cooldown = 60000;
        fail_enrichment = true;
        assert(voice_notes_processing_start(enrichment_id.c_str(), VoiceNotesProcessingMode::Regenerate) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        assert(voice_notes_processing_start(enrichment_id.c_str(), VoiceNotesProcessingMode::Regenerate) ==
               VoiceNotesProcessingStart::Cooldown);
        assert(voice_notes_command(VoiceNoteCommand::Start, 2054));
        assert(wait_completion(2054));
        const unsigned before_independent_note = reads;
        wait_until([=] { return reads.load() > before_independent_note; });
        assert(voice_notes_command(VoiceNoteCommand::Save, 2055));
        assert(wait_completion(2055));
        const std::string independent_id = snapshot().last_clip;
        strlcpy(both.transcription.endpoint, "https://example.test/", sizeof(both.transcription.endpoint));
        strlcpy(both.transcription.api_key, "key", sizeof(both.transcription.api_key));
        strlcpy(both.transcription.deployment, "transcription", sizeof(both.transcription.deployment));
        assert(voice_notes_processing_configure(both));
        assert(voice_notes_processing_start(independent_id.c_str(), VoiceNotesProcessingMode::Missing) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([&] { return voice_notes_has_artifact(independent_id.c_str(), VoiceNoteArtifact::Transcript); });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        assert(!voice_notes_has_artifact(independent_id.c_str(), VoiceNoteArtifact::Enrichment));
        assert(voice_notes_processing_start(enrichment_id.c_str(), VoiceNotesProcessingMode::Regenerate) ==
               VoiceNotesProcessingStart::Cooldown);
        assert(voice_notes_remove(independent_id.c_str()) == VoiceNoteFileStatus::Ready);
        audio_test_time_offset_us += 61000000;
        simulated_enrichment_outcome = VoiceNotesRequestOutcome::NoteFailure;
        simulated_enrichment_cooldown = 0;
        fail_enrichment = true;
        assert(voice_notes_processing_start(enrichment_id.c_str(), VoiceNotesProcessingMode::Regenerate) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([] {
            VoiceNotesEnrichmentSnapshot state{};
            voice_notes_enrichment_snapshot(&state);
            return state.state == VoiceNotesEnrichmentState::Error;
        });
        assert(LittleFS.files.at(generated_path.c_str()) == generated);
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        fail_enrichment = false;
        block_enrichment = true;
        enrichment_entered = false;
        assert(voice_notes_processing_start(enrichment_id.c_str(), VoiceNotesProcessingMode::Regenerate) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([] { return enrichment_entered.load(); });
        assert(voice_notes_command(VoiceNoteCommand::Start, 2052));
        assert(wait_completion(2052));
        assert(voice_notes_command(VoiceNoteCommand::Discard, 2053));
        assert(wait_completion(2053));
        block_enrichment = false;
        wait_until([] {
            VoiceNotesEnrichmentSnapshot state{};
            voice_notes_enrichment_snapshot(&state);
            return state.state == VoiceNotesEnrichmentState::Error;
        });
        assert(LittleFS.files.at(generated_path.c_str()) == generated);
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        const unsigned prior_enrichment_requests = enrichment_requests;
        assert(voice_notes_processing_start(enrichment_id.c_str(), VoiceNotesProcessingMode::Regenerate) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([] {
            VoiceNotesEnrichmentSnapshot state{};
            voice_notes_enrichment_snapshot(&state);
            return state.state == VoiceNotesEnrichmentState::Ready;
        });
        assert(enrichment_requests == prior_enrichment_requests + 1);
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        assert(voice_notes_remove(enrichment_id.c_str()) == VoiceNoteFileStatus::Ready);

        assert(voice_notes_command(VoiceNoteCommand::Start, 2056));
        assert(wait_completion(2056));
        const unsigned before_disabled_stage = reads;
        wait_until([=] { return reads.load() > before_disabled_stage; });
        assert(voice_notes_command(VoiceNoteCommand::Save, 2057));
        assert(wait_completion(2057));
        const std::string disabled_stage_id = snapshot().last_clip;
        const unsigned before_disabled_enrichment = enrichment_requests;
        transcription_entered = false;
        block_transcription = true;
        both.auto_process = true;
        assert(voice_notes_processing_configure(both));
        wait_until([] { return transcription_entered.load(); });
        both.auto_process = false;
        assert(voice_notes_processing_configure(both));
        block_transcription = false;
        wait_until([&] { return voice_notes_has_artifact(disabled_stage_id.c_str(), VoiceNoteArtifact::Transcript); });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        assert(!voice_notes_has_artifact(disabled_stage_id.c_str(), VoiceNoteArtifact::Enrichment));
        assert(enrichment_requests == before_disabled_enrichment);
        assert(voice_notes_processing_start(disabled_stage_id.c_str(), VoiceNotesProcessingMode::Missing) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([&] { return voice_notes_has_artifact(disabled_stage_id.c_str(), VoiceNoteArtifact::Enrichment); });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        assert(voice_notes_remove(disabled_stage_id.c_str()) == VoiceNoteFileStatus::Ready);

        assert(voice_notes_command(VoiceNoteCommand::Start, 2072));
        assert(wait_completion(2072));
        const unsigned before_silent_note = reads;
        wait_until([=] { return reads.load() > before_silent_note; });
        assert(voice_notes_command(VoiceNoteCommand::Save, 2073));
        assert(wait_completion(2073));
        const std::string silent_id = snapshot().last_clip;
        transcription_text = "No speech detected";
        enrichment_title = "No speech detected";
        enrichment_summary = "";
        const unsigned before_silent_enrichment = enrichment_requests;
        both.auto_process = true;
        assert(voice_notes_processing_configure(both));
        wait_until([&] { return voice_notes_has_artifact(silent_id.c_str(), VoiceNoteArtifact::Enrichment); });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        assert(enrichment_requests == before_silent_enrichment + 1);
        JsonDocument silent_result;
        File silent_file;
        assert(voice_notes_open_artifact(silent_id.c_str(), VoiceNoteArtifact::Transcript, &silent_file) ==
               VoiceNoteFileStatus::Ready);
        assert(!deserializeJson(silent_result, silent_file));
        silent_file.close();
        assert(strcmp(silent_result["transcript"]["text"], "No speech detected") == 0);
        silent_result.clear();
        assert(voice_notes_open_artifact(silent_id.c_str(), VoiceNoteArtifact::Enrichment, &silent_file) ==
               VoiceNoteFileStatus::Ready);
        assert(!deserializeJson(silent_result, silent_file));
        silent_file.close();
        assert(strcmp(silent_result["enrichment"]["title"], "No speech detected") == 0);
        assert(strcmp(silent_result["enrichment"]["summary"], "") == 0);
        VoiceNoteCursor silent_candidate{};
        assert(voice_notes_next_candidate({}, &silent_candidate, true, true) == VoiceNoteCandidate::None);
        assert(voice_notes_processing_start(silent_id.c_str(), VoiceNotesProcessingMode::Missing) ==
               VoiceNotesProcessingStart::Unchanged);
        both.auto_process = false;
        assert(voice_notes_processing_configure(both));
        transcription_text = "host transcript";
        enrichment_title = "Host title";
        enrichment_summary = "Host summary.";
        assert(voice_notes_remove(silent_id.c_str()) == VoiceNoteFileStatus::Ready);

        assert(voice_notes_command(VoiceNoteCommand::Start, 2010));
        assert(wait_completion(2010));
        const unsigned before = reads;
        wait_until([=] { return reads.load() > before; });
        assert(voice_notes_command(VoiceNoteCommand::Save, 2011));
        assert(wait_completion(2011));
        const std::string automatic_id = snapshot().last_clip;
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            request_ids.clear();
            failed_note = automatic_id;
            simulated_outcome = VoiceNotesRequestOutcome::SetupFailure;
        }
        wifi_connected = false;
        VoiceNotesConfig automatic{};
        strlcpy(automatic.transcription.endpoint, "https://example.test/", sizeof(automatic.transcription.endpoint));
        strlcpy(automatic.transcription.api_key, "key", sizeof(automatic.transcription.api_key));
        strlcpy(automatic.transcription.deployment, "updated", sizeof(automatic.transcription.deployment));
        automatic.auto_process = true;
        assert(voice_notes_processing_configure(automatic));
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Waiting &&
                   state.reason == VoiceNotesProcessingReason::Offline;
        });
        wifi_connected = true;
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Blocked;
        });
        automatic.auto_process = false;
        assert(voice_notes_processing_configure(automatic));
        automatic.auto_process = true;
        assert(voice_notes_processing_configure(automatic));
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Blocked;
        });
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            simulated_outcome = VoiceNotesRequestOutcome::RateLimited;
            simulated_cooldown = 60000;
        }
        strlcpy(automatic.transcription.deployment, "changed", sizeof(automatic.transcription.deployment));
        assert(voice_notes_processing_configure(automatic));
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Waiting &&
                   state.reason == VoiceNotesProcessingReason::Retry;
        });
        assert(voice_notes_processing_start() == VoiceNotesProcessingStart::Cooldown);
        automatic.auto_process = false;
        assert(voice_notes_processing_configure(automatic));
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        assert(voice_notes_processing_start() == VoiceNotesProcessingStart::Cooldown);
        audio_test_time_offset_us += 61000000;
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            failed_note.clear();
        }
        assert(voice_notes_processing_start() == VoiceNotesProcessingStart::Accepted);
        wait_until([&] { return voice_notes_has_artifact(automatic_id.c_str(), VoiceNoteArtifact::Transcript); });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        assert(voice_notes_remove(automatic_id.c_str()) == VoiceNoteFileStatus::Ready);

        std::vector<std::string> disabled_ids;
        for (uint32_t token = 2020; token < 2024; token += 2) {
            assert(voice_notes_command(VoiceNoteCommand::Start, token));
            assert(wait_completion(token));
            const unsigned captured = reads;
            wait_until([=] { return reads.load() > captured; });
            assert(voice_notes_command(VoiceNoteCommand::Save, token + 1));
            assert(wait_completion(token + 1));
            disabled_ids.emplace_back(snapshot().last_clip);
        }
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            request_ids.clear();
        }
        transcription_entered = false;
        block_transcription = true;
        automatic.auto_process = true;
        assert(voice_notes_processing_configure(automatic));
        wait_until([] { return transcription_entered.load(); });
        automatic.auto_process = false;
        assert(voice_notes_processing_configure(automatic));
        block_transcription = false;
        wait_until([&] { return voice_notes_has_artifact(disabled_ids[0].c_str(), VoiceNoteArtifact::Transcript); });
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        assert(!voice_notes_has_artifact(disabled_ids[1].c_str(), VoiceNoteArtifact::Transcript));
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            assert(request_ids.size() == 1 && request_ids[0] == disabled_ids[0]);
        }
        for (const auto& id : disabled_ids) assert(voice_notes_remove(id.c_str()) == VoiceNoteFileStatus::Ready);

        assert(voice_notes_command(VoiceNoteCommand::Start, 2030));
        assert(wait_completion(2030));
        const unsigned retry_capture = reads;
        wait_until([=] { return reads.load() > retry_capture; });
        assert(voice_notes_command(VoiceNoteCommand::Save, 2031));
        assert(wait_completion(2031));
        const std::string retry_id = snapshot().last_clip;
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            failed_note = retry_id;
            simulated_outcome = VoiceNotesRequestOutcome::NoteFailure;
            request_ids.clear();
        }
        automatic.auto_process = true;
        assert(voice_notes_processing_configure(automatic));
        auto requests = [] {
            std::lock_guard<std::mutex> lock(request_mutex);
            return request_ids.size();
        };
        auto waiting_for_retry = [] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Waiting &&
                   state.reason == VoiceNotesProcessingReason::Retry;
        };
        auto transcription_retry_status = [] {
            VoiceNotesTranscriptionSnapshot state{};
            voice_notes_transcription_snapshot(&state);
            return std::string(state.retry_status);
        };
        wait_until([&] { return requests() == 1 && waiting_for_retry(); });
        assert(transcription_retry_status() == "Retry 1 of 3");
        audio_test_time_offset_us += 27000000;
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        assert(requests() == 1);
        audio_test_time_offset_us += 4000000;
        wait_until([&] { return requests() == 2 && waiting_for_retry(); });
        assert(transcription_retry_status() == "Retry 2 of 3");
        audio_test_time_offset_us += 57000000;
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        assert(requests() == 2);
        audio_test_time_offset_us += 4000000;
        wait_until([&] { return requests() == 3 && waiting_for_retry(); });
        assert(transcription_retry_status() == "Retry 3 of 3");
        audio_test_time_offset_us += 297000000;
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        assert(requests() == 3);
        audio_test_time_offset_us += 4000000;
        auto retries_halted = [] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Blocked &&
                   state.reason == VoiceNotesProcessingReason::RetriesExhausted;
        };
        wait_until([&] { return requests() == 4 && retries_halted(); });
        assert(transcription_retry_status() == "Retries halted");
        audio_test_time_offset_us += 3600000000ULL;
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        assert(requests() == 4);
        automatic.auto_process = false;
        assert(voice_notes_processing_configure(automatic));
        automatic.auto_process = true;
        assert(voice_notes_processing_configure(automatic));
        wait_until(retries_halted);
        assert(requests() == 4);

        assert(voice_notes_command(VoiceNoteCommand::Start, 2080));
        assert(wait_completion(2080));
        const unsigned new_capture = reads;
        wait_until([=] { return reads.load() > new_capture; });
        assert(voice_notes_command(VoiceNoteCommand::Save, 2081));
        assert(wait_completion(2081));
        const std::string resumed_id = snapshot().last_clip;
        wait_until([&] { return requests() == 6 && waiting_for_retry(); });
        assert(voice_notes_has_artifact(resumed_id.c_str(), VoiceNoteArtifact::Transcript));
        assert(transcription_retry_status() == "Retry 1 of 3");
        assert(voice_notes_processing_start(retry_id.c_str(), VoiceNotesProcessingMode::Missing) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([&] { return requests() == 7 && waiting_for_retry(); });
        assert(transcription_retry_status() == "Retry 1 of 3");
        strlcpy(automatic.transcription.deployment, "retry-reset", sizeof(automatic.transcription.deployment));
        assert(voice_notes_processing_configure(automatic));
        wait_until([&] { return requests() == 8 && waiting_for_retry(); });
        assert(transcription_retry_status() == "Retry 1 of 3");
        automatic.auto_process = false;
        assert(voice_notes_processing_configure(automatic));
        wait_until([] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return state.state == VoiceNotesProcessingState::Idle;
        });
        {
            std::lock_guard<std::mutex> lock(request_mutex);
            failed_note.clear();
        }
        assert(voice_notes_processing_start(retry_id.c_str(), VoiceNotesProcessingMode::Missing) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until([&] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return voice_notes_has_artifact(retry_id.c_str(), VoiceNoteArtifact::Transcript) &&
                   state.state == VoiceNotesProcessingState::Idle;
        });
        assert(transcription_retry_status().empty());
        both.auto_process = true;
        fail_enrichment = true;
        simulated_enrichment_outcome = VoiceNotesRequestOutcome::NoteFailure;
        const unsigned enrichment_before_retry = enrichment_requests;
        assert(voice_notes_processing_configure(both));
        auto enrichment_retry_status = [] {
            VoiceNotesEnrichmentSnapshot state{};
            voice_notes_enrichment_snapshot(&state);
            return std::string(state.retry_status);
        };
        wait_until([&] {
            return enrichment_requests == enrichment_before_retry + 2 && waiting_for_retry();
        });
        assert(enrichment_retry_status() == "Retry 1 of 3");
        for (uint64_t delay : {31000000ULL, 61000000ULL, 301000000ULL}) {
            const unsigned expected = enrichment_requests + 2;
            audio_test_time_offset_us += delay;
            wait_until([&] {
                return enrichment_requests == expected && (waiting_for_retry() || retries_halted());
            });
        }
        assert(enrichment_retry_status() == "Retries halted");
        const unsigned halted_enrichment_requests = enrichment_requests;
        const String resumed_transcript = String("/storage/voice-notes/") + resumed_id.c_str() + ".json";
        assert(LittleFS.remove(resumed_transcript.c_str()));
        audio_test_time_offset_us += 6000000;
        wait_until([&] {
            return voice_notes_has_artifact(resumed_id.c_str(), VoiceNoteArtifact::Transcript) &&
                   retries_halted();
        });
        assert(enrichment_requests == halted_enrichment_requests);
        assert(voice_notes_processing_start(retry_id.c_str(), VoiceNotesProcessingMode::Retranscribe) ==
               VoiceNotesProcessingStart::Accepted);
        wait_until(retries_halted);
        assert(enrichment_requests == halted_enrichment_requests);
        assert(enrichment_retry_status() == "Retries halted");
        both.auto_process = false;
        assert(voice_notes_processing_configure(both));
        fail_enrichment = false;
        assert(voice_notes_processing_start() == VoiceNotesProcessingStart::Accepted);
        wait_until([&] {
            VoiceNotesProcessingSnapshot state{};
            voice_notes_processing_snapshot(&state);
            return voice_notes_has_artifact(retry_id.c_str(), VoiceNoteArtifact::Enrichment) &&
                   voice_notes_has_artifact(resumed_id.c_str(), VoiceNoteArtifact::Enrichment) &&
                   state.state == VoiceNotesProcessingState::Idle;
        });
        assert(enrichment_retry_status().empty());
        assert(voice_notes_remove(resumed_id.c_str()) == VoiceNoteFileStatus::Ready);
        assert(voice_notes_remove(retry_id.c_str()) == VoiceNoteFileStatus::Ready);
    }

    audio_test_stopping = true;
    xTaskNotifyGive(&audio_test_worker_handle);
    xTaskNotifyGive(&audio_test_worker2_handle);
    if (audio_test_worker3.joinable()) xTaskNotifyGive(&audio_test_worker3_handle);
    audio_test_worker.join();
    audio_test_worker2.join();
    if (audio_test_worker3.joinable()) audio_test_worker3.join();
}
