#include "list_source_voice_notes.h"

#if HAS_DISPLAY && HAS_PSRAM && HAS_VOICE_NOTES

#include "list_source.h"
#include "log_manager.h"
#include "voice_notes.h"

#include <esp_heap_caps.h>
#include <new>
#include <string.h>

namespace {
constexpr size_t recent_notes_limit = 20;
struct RecentNotesJob {
    BoundedPsramJsonAllocator allocator{LIST_SOURCE_DOCUMENT_BYTES};
    JsonDocument entries{&allocator};
    size_t index = 0;
};

ListSourceLoadStep load_recent_notes(void** opaque, ListSourceSnapshot* snapshot, const char** error) {
    VoiceNoteSnapshot recorder = {};
    voice_notes_snapshot(&recorder);
    if (recorder.session_active) return ListSourceLoadStep::Deferred;
    auto* job = static_cast<RecentNotesJob*>(*opaque);
    if (!job) {
        void* memory = heap_caps_malloc(sizeof(RecentNotesJob), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) {
            *error = "recent note allocation failed";
            return ListSourceLoadStep::Failed;
        }
        job = new (memory) RecentNotesJob();
        *opaque = job;
        const VoiceNoteRecentResult result = voice_notes_select_recent(job->entries, recent_notes_limit);
        if (result == VoiceNoteRecentResult::Deferred) return ListSourceLoadStep::Deferred;
        if (result != VoiceNoteRecentResult::Ready || job->entries.overflowed()) {
            *error = "cannot select recent notes";
            return ListSourceLoadStep::Failed;
        }
        return job->entries.size() ? ListSourceLoadStep::Pending : ListSourceLoadStep::Complete;
    }

    JsonObject entry = job->entries[job->index];
    const VoiceNoteRecentResult result = voice_notes_enrich_recent(entry);
    if (result == VoiceNoteRecentResult::Deferred) return ListSourceLoadStep::Deferred;
    if (result != VoiceNoteRecentResult::Ready || job->entries.overflowed()) {
        *error = "cannot read recent note metadata";
        return ListSourceLoadStep::Failed;
    }
    JsonDocument row(&job->allocator);
    row["id"] = entry["id"];
    if (entry["created_at"].is<uint32_t>() && entry["created_at"].as<uint32_t>() != 0)
        row["created_at"] = entry["created_at"];
    row["duration_ms"] = entry["duration_ms"];
    row["has_transcript"] = entry["has_transcript"];
    row["has_enrichment"] = entry["has_enrichment"];
    JsonString title = entry["title"].as<JsonString>();
    if (title.c_str()) {
        size_t length;
        if (list_source_preview_length(title.c_str(), title.size(), LIST_SOURCE_MAX_TEXT_BYTES, &length))
            row["title"] = JsonString(title.c_str(), length, JsonString::Copied);
        else
            LOGW("ListSource", "Recent note has an invalid title");
    }
    if (row.overflowed()) {
        *error = "recent note allocation failed";
        return ListSourceLoadStep::Failed;
    }
    if (const char* append_error = snapshot->append(row.as<JsonObjectConst>())) {
        *error = append_error;
        return ListSourceLoadStep::Failed;
    }
    ++job->index;
    return job->index == job->entries.size() ? ListSourceLoadStep::Complete : ListSourceLoadStep::Pending;
}

void finish_recent_notes(void* opaque) {
    auto* job = static_cast<RecentNotesJob*>(opaque);
    if (!job) return;
    job->~RecentNotesJob();
    heap_caps_free(job);
}

const ListSourceField fields[] = {
    {"id", "Stable note ID", ListSourceFieldType::String, false},
    {"title", "Saved title when available", ListSourceFieldType::String, true},
    {"created_at", "Creation timestamp when available", ListSourceFieldType::Number, true},
    {"duration_ms", "Recording duration in milliseconds", ListSourceFieldType::Number, false},
    {"has_transcript", "Whether a transcript exists", ListSourceFieldType::Boolean, false},
    {"has_enrichment", "Whether title and summary data exists", ListSourceFieldType::Boolean, false},
};
const ListSourceDescriptor source{"[voice_note:recent_notes]", "Recent voice notes", fields,
    sizeof(fields) / sizeof(fields[0]), voice_notes_catalog_revision, load_recent_notes, finish_recent_notes};
}

void list_source_voice_notes_init() {
    if (!list_source_register(&source)) LOGE("ListSource", "Cannot register recent voice notes");
}

#endif
