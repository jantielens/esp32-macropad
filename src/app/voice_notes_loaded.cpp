#include "voice_notes_loaded.h"

#if HAS_VOICE_NOTES && HAS_DISPLAY && HAS_PSRAM
#include "binding_value_json.h"
#include "loaded_data.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "voice_notes.h"
#include "voice_notes_file.h"
#include "voice_notes_transcription_request.h"

#include <ctype.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>
#include <new>
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t kPsramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
LoadedDataSlot slot;
char requested_id[17] = {};
uint64_t requested_generation = 0;

struct Job {
    char id[17];
    uint64_t request;
    uint64_t recording_generation;
    uint32_t ota_epoch;
    unsigned step = 0;
    File recording;
    bool leased = false;
    LoadedDataSnapshot* snapshot = nullptr;
    BoundedPsramJsonAllocator allocator{SIZE_MAX};
    JsonDocument filter{&allocator};
    uint8_t buffer[512];
};
Job* job = nullptr;

bool current(uint64_t request) {
    LoadedDataLock lock;
    return slot.current(request);
}

bool checkpoint(const Job& work) {
    return current(work.request) &&
        voice_notes_transcription_checkpoint(work.ota_epoch, work.recording_generation);
}

void finish_job() {
    if (!job) return;
    if (job->leased) voice_notes_close(&job->recording, job->id);
    if (job->snapshot) job->snapshot->release();
    job->~Job();
    heap_caps_free(job);
    job = nullptr;
}

void fail_job(const char* message) {
    bool published;
    {
        LoadedDataLock lock;
        published = slot.fail(job->request, message);
    }
    if (published) {
        LOGE("VoiceLoad", "%s", message);
        display_manager_request_binding_refresh();
    }
    finish_job();
}

class ArtifactReader {
public:
    ArtifactReader(File& file, Job& work) : file_(file), work_(work), size_(file.size()) {}
    int read() {
        if (position_ == used_ && !fill()) return -1;
        return work_.buffer[position_++];
    }
    size_t readBytes(char* out, size_t count) {
        size_t used = 0;
        while (used < count) {
            const int byte = read();
            if (byte < 0) break;
            out[used++] = static_cast<char>(byte);
        }
        return used;
    }
    bool interrupted() const { return interrupted_; }
    bool failed() const { return failed_; }
private:
    bool fill() {
        if (!checkpoint(work_)) {
            interrupted_ = true;
            return false;
        }
        if (file_.position() >= size_) return false;
        const size_t remaining = size_ - file_.position();
        const size_t amount = remaining < sizeof(work_.buffer) ? remaining : sizeof(work_.buffer);
        used_ = file_.read(work_.buffer, amount);
        position_ = 0;
        if (!used_) {
            failed_ = true;
            return false;
        }
        vTaskDelay(1);
        return true;
    }
    File& file_;
    Job& work_;
    size_t size_;
    size_t position_ = 0;
    size_t used_ = 0;
    bool interrupted_ = false;
    bool failed_ = false;
};

enum class StepResult { Complete, Deferred, Error };

bool valid_string(JsonVariantConst value, bool nonblank) {
    if (!value.is<const char*>()) return false;
    const JsonString text = value.as<JsonString>();
    if (!text.c_str() || memchr(text.c_str(), '\0', text.size())) return false;
    if (!nonblank) return true;
    for (size_t i = 0; i < text.size(); ++i)
        if (!isspace(static_cast<unsigned char>(text.c_str()[i]))) return true;
    return false;
}

StepResult read_artifact(Job& work, VoiceNoteArtifact artifact, const char** error) {
    File file;
    const VoiceNoteFileStatus status = voice_notes_open_artifact(work.id, artifact, &file);
    if (!checkpoint(work)) {
        if (status == VoiceNoteFileStatus::Ready) file.close();
        return StepResult::Deferred;
    }
    JsonDocument& doc = work.snapshot->document();
    const bool transcript = artifact == VoiceNoteArtifact::Transcript;
    const char* field = transcript ? "transcript" : "enrichment";
    const char* present = transcript ? "has_transcript" : "has_enrichment";
    if (status == VoiceNoteFileStatus::NotFound) {
        doc[present] = false;
        if (doc.overflowed()) { *error = "snapshot allocation failed"; return StepResult::Error; }
        return StepResult::Complete;
    }
    if (status != VoiceNoteFileStatus::Ready) {
        *error = "saved artifact unavailable";
        return StepResult::Error;
    }
    work.filter.clear();
    if (transcript) {
        work.filter["transcript"]["text"] = true;
        work.filter["transcript"]["model"] = true;
        work.filter["transcript"]["created_at"] = true;
    } else {
        work.filter["enrichment"]["title"] = true;
        work.filter["enrichment"]["summary"] = true;
        work.filter["enrichment"]["model"] = true;
        work.filter["enrichment"]["created_at"] = true;
    }
    if (work.filter.overflowed()) {
        file.close();
        *error = "filter allocation failed";
        return StepResult::Error;
    }
    ArtifactReader reader(file, work);
    doc[field].to<JsonObject>();
    JsonVariant destination = doc[field];
    const DeserializationError result = deserializeJson(destination, reader,
                                                         DeserializationOption::Filter(work.filter));
    file.close();
    if (reader.interrupted() || !checkpoint(work)) return StepResult::Deferred;
    if (result || reader.failed() || doc.overflowed()) {
        *error = result == DeserializationError::NoMemory || doc.overflowed()
            ? "snapshot allocation failed" : "saved artifact invalid or unreadable";
        return StepResult::Error;
    }
    JsonObjectConst saved = doc[field][field].as<JsonObjectConst>();
    if (saved.isNull() || !saved["model"].is<const char*>() ||
        !saved["created_at"].is<uint32_t>() ||
        (transcript ? !saved["text"].is<const char*>() :
         (!valid_string(saved["title"], true) || !valid_string(saved["summary"], false)))) {
        *error = "saved artifact invalid";
        return StepResult::Error;
    }
    doc[present] = true;
    if (doc.overflowed()) { *error = "snapshot allocation failed"; return StepResult::Error; }
    return StepResult::Complete;
}

const char* state_name(LoadedDataState state) {
    switch (state) {
        case LoadedDataState::Idle: return "idle";
        case LoadedDataState::Loading: return "loading";
        case LoadedDataState::Ready: return "ready";
        case LoadedDataState::Error: return "error";
    }
    return "error";
}
}

bool voice_notes_load_request(const char* id) {
    if (!voice_notes_id_valid(id)) return false;
    {
        LoadedDataLock lock;
        memcpy(requested_id, id, sizeof(requested_id));
        requested_generation = slot.begin();
    }
    display_manager_request_binding_refresh();
    return true;
}

void voice_notes_loaded_clear_if_matches(const char* id) {
    if (!id || !id[0]) return;
    bool cleared = false;
    {
        LoadedDataLock lock;
        if (strcmp(requested_id, id) == 0) {
            slot.clear();
            requested_id[0] = '\0';
            requested_generation = 0;
            cleared = true;
        }
    }
    if (cleared) display_manager_request_binding_refresh();
}

void voice_notes_loaded_loop() {
    for (;;) {
        LoadedDataSnapshot* unused;
        {
            LoadedDataLock lock;
            unused = slot.take_unused();
        }
        if (!unused) break;
        unused->release();
    }
    if (job && !checkpoint(*job)) finish_job();
    if (!job) {
        VoiceNoteSnapshot recorder = {};
        voice_notes_snapshot(&recorder);
        if (recorder.session_active || ota_activity_is_busy()) return;
        char id[sizeof(requested_id)];
        uint64_t request;
        {
            LoadedDataLock lock;
            if (slot.state() != LoadedDataState::Loading) return;
            memcpy(id, requested_id, sizeof(id));
            request = requested_generation;
        }
        void* memory = heap_caps_malloc(sizeof(Job), kPsramCaps);
        if (!memory) {
            bool failed;
            {
                LoadedDataLock lock;
                failed = slot.fail(request, "job allocation failed");
            }
            if (failed) {
                LOGE("VoiceLoad", "Job allocation failed");
                display_manager_request_binding_refresh();
            }
            return;
        }
        job = new (memory) Job();
        memcpy(job->id, id, sizeof(id));
        job->request = request;
        job->recording_generation = recorder.recording_generation;
        job->ota_epoch = ota_activity_epoch();
        job->snapshot = LoadedDataSnapshot::create();
        if (!job->snapshot) { fail_job("snapshot allocation failed"); return; }
        if (!checkpoint(*job)) { finish_job(); return; }
    }
    const char* error = nullptr;
    if (job->step == 0) {
        const VoiceNoteFileStatus status = voice_notes_open(job->id, &job->recording);
        job->leased = status == VoiceNoteFileStatus::Ready;
        if (!checkpoint(*job)) { finish_job(); return; }
        if (status != VoiceNoteFileStatus::Ready) {
            fail_job(status == VoiceNoteFileStatus::NotFound ? "note not found" : "note unavailable");
            return;
        }
        JsonDocument& doc = job->snapshot->document();
        JsonObject metadata = doc["note"].to<JsonObject>();
        if (!voice_notes_read_metadata(job->id, metadata) || doc.overflowed()) {
            if (!checkpoint(*job)) { finish_job(); return; }
            fail_job("note metadata unavailable");
            return;
        }
    } else if (job->step <= 2) {
        const StepResult result = read_artifact(*job,
            job->step == 1 ? VoiceNoteArtifact::Transcript : VoiceNoteArtifact::Enrichment, &error);
        if (result == StepResult::Deferred) { finish_job(); return; }
        if (result == StepResult::Error) { fail_job(error); return; }
    } else {
        if (!checkpoint(*job)) { finish_job(); return; }
        bool published;
        {
            LoadedDataLock lock;
            published = slot.publish(job->request, job->snapshot);
            if (published) job->snapshot = nullptr;
        }
        if (published) display_manager_request_binding_refresh();
        finish_job();
        return;
    }
    ++job->step;
}

BindingResolverStatus voice_notes_loaded_resolve(const char* key, char* out, size_t size) {
    if (!key || !out || !size) return BINDING_RESOLVER_UNAVAILABLE;
    LoadedDataSnapshot* snapshot;
    {
        LoadedDataLock lock;
        if (!strcmp(key, "status")) {
            return strlcpy(out, state_name(slot.state()), size) < size
                ? BINDING_RESOLVER_RESOLVED : BINDING_RESOLVER_UNAVAILABLE;
        }
        if (!strcmp(key, "error")) {
            const char* error = slot.error();
            if (!error) return BINDING_RESOLVER_UNAVAILABLE;
            return strlcpy(out, error, size) < size ? BINDING_RESOLVER_RESOLVED : BINDING_RESOLVER_UNAVAILABLE;
        }
        snapshot = slot.acquire();
    }
    if (!snapshot) return BINDING_RESOLVER_UNAVAILABLE;
    JsonVariantConst data = snapshot->data();
    JsonVariantConst value;
    if (!strcmp(key, "id")) value = data["note"]["id"];
    else if (!strcmp(key, "created_at")) {
        if (data["note"]["created_at"].as<uint32_t>()) value = data["note"]["created_at"];
    } else if (!strcmp(key, "duration_ms")) value = data["note"]["duration_ms"];
    else if (!strcmp(key, "has_transcript")) value = data["has_transcript"];
    else if (!strcmp(key, "has_enrichment")) value = data["has_enrichment"];
    else if (!strcmp(key, "transcript")) value = data["transcript"]["transcript"]["text"];
    else if (!strcmp(key, "title")) value = data["enrichment"]["enrichment"]["title"];
    else if (!strcmp(key, "summary")) value = data["enrichment"]["enrichment"]["summary"];
    else { snapshot->release(); return BINDING_RESOLVER_UNKNOWN; }
    bool resolved;
    if (value.is<const char*>() && binding_template_checked_resolution_active()) {
        const JsonString text = value.as<JsonString>();
        resolved = text.size() < size && !memchr(text.c_str(), '\0', text.size());
        if (resolved) {
            memcpy(out, text.c_str(), text.size());
            out[text.size()] = '\0';
        }
    } else {
        resolved = binding_value_scalar_json_preview(value, out, size);
    }
    snapshot->release();
    return resolved ? BINDING_RESOLVER_RESOLVED : BINDING_RESOLVER_UNAVAILABLE;
}

LoadedDataSnapshot* voice_notes_loaded_acquire() {
    LoadedDataLock lock;
    return slot.acquire();
}

BindingResolverStatus voice_notes_loaded_acquire_text(const char* key, BindingTextView* out) {
    if (strcmp(key, "transcript") && strcmp(key, "summary") && strcmp(key, "title"))
        return BINDING_RESOLVER_UNKNOWN;
    LoadedDataSnapshot* snapshot = voice_notes_loaded_acquire();
    if (!snapshot) return BINDING_RESOLVER_UNAVAILABLE;
    JsonVariantConst data = snapshot->data();
    JsonVariantConst value = !strcmp(key, "transcript")
        ? data["transcript"]["transcript"]["text"]
        : data["enrichment"]["enrichment"][!strcmp(key, "title") ? "title" : "summary"];
    if (!value.is<const char*>()) {
        snapshot->release();
        return BINDING_RESOLVER_UNAVAILABLE;
    }
    out->text = value.as<const char*>();
    out->owner = snapshot;
    out->release = [](void* owner) { static_cast<LoadedDataSnapshot*>(owner)->release(); };
    return BINDING_RESOLVER_RESOLVED;
}
#endif
