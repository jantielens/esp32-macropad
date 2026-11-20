#include "component_registry.h"
#include "voice_notes.h"
#include "voice_notes_file.h"
#include "voice_note_range.h"
#include "voice_notes_processing.h"
#include "bounded_psram_json_allocator.h"
#include "fs_health.h"
#include "web_portal_json.h"
#include "web_portal_utils.h"
#include "log_manager.h"
#include <ArduinoJson.h>
#include <memory>
#include <new>
#include <ctype.h>
#include <esp_heap_caps.h>

namespace {
bool voice_note_valid_string(JsonVariantConst value, bool nonblank) {
    if (!value.is<const char*>()) return false;
    const JsonString text = value.as<JsonString>();
    if (!text.c_str() || strlen(text.c_str()) != text.size()) return false;
    if (!nonblank) return true;
    for (size_t i = 0; i < text.size(); ++i)
        if (!isspace(static_cast<unsigned char>(text.c_str()[i]))) return true;
    return false;
}

bool voice_note_id_valid(const String& id) {
    if (id.length() != 16) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool voice_note_file_error(AsyncWebServerRequest* request, VoiceNoteFileStatus status) {
    if (status == VoiceNoteFileStatus::Ready) return false;
    const int code = status == VoiceNoteFileStatus::NotFound ? 404 :
                     status == VoiceNoteFileStatus::Busy ? 409 : 503;
    const char* message = status == VoiceNoteFileStatus::NotFound ? "Note not found" :
                          status == VoiceNoteFileStatus::Busy ? "Note is busy" :
                          "Note storage unavailable";
    web_portal_send_json_error(request, code, message);
    return true;
}

struct VoiceNotesResponse {
    BoundedPsramJsonAllocator allocator;
    JsonDocument document;
    VoiceNotesResponse() : allocator(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                           document(&allocator) {}
};

void voice_notes_send_json(AsyncWebServerRequest* request,
                          const std::shared_ptr<VoiceNotesResponse>& body) {
    if (body->document.overflowed()) {
        web_portal_send_json_error(request, 503, "Voice Notes response memory unavailable");
        return;
    }
    const size_t length = measureJson(body->document);
    AsyncWebServerResponse* response = request->beginResponse("application/json", length,
        [body, length](uint8_t* buffer, size_t max_len, size_t index) -> size_t {
            if (index >= length) return 0;
            const size_t count = min(max_len, length - index);
            ChunkPrint output(buffer, index, count);
            serializeJson(body->document, output);
            return count;
        });
    if (!response) {
        web_portal_send_json_error(request, 503, "Cannot start note response");
        return;
    }
    request->send(response);
}

void voice_notes_list_get(AsyncWebServerRequest* request) {
    auto list = std::make_shared<VoiceNotesResponse>();
    if (!voice_notes_list(list->document)) {
        web_portal_send_json_error(request, 503, "Note catalog unavailable");
        return;
    }
    voice_notes_send_json(request, list);
}

void voice_notes_status_get(AsyncWebServerRequest* request) {
    auto doc = make_psram_json_doc(1024);
    if (!doc) {
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }
    VoiceNoteSnapshot snapshot{};
    voice_notes_snapshot(&snapshot);
    JsonObject recording = (*doc)["recording"].to<JsonObject>();
    recording["state"] = voice_notes_state_name(snapshot.state);
    recording["session_active"] = snapshot.session_active;
    recording["error"] = snapshot.error;
    recording["duration_ms"] = snapshot.duration_ms;
    if (snapshot.session_active) recording["scenario"] = snapshot.scenario;
    else recording["scenario"] = nullptr;
    (*doc)["last_saved"]["id"] = snapshot.last_clip;
    VoiceNotesTranscriptionSnapshot transcription{};
    voice_notes_transcription_snapshot(&transcription);
    JsonObject processing = (*doc)["transcription"].to<JsonObject>();
    processing["state"] = voice_notes_transcription_state_name(transcription.state);
    processing["note_id"] = transcription.note_id;
    processing["error"] = transcription.error;
    processing["retry_status"] = transcription.retry_status;
    (*doc)["transcription_configured"] = voice_notes_transcription_configured();
    VoiceNotesEnrichmentSnapshot enrichment{};
    voice_notes_enrichment_snapshot(&enrichment);
    (*doc)["enrichment"]["state"] = voice_notes_enrichment_state_name(enrichment.state);
    (*doc)["enrichment"]["note_id"] = enrichment.note_id;
    (*doc)["enrichment"]["error"] = enrichment.error;
    (*doc)["enrichment"]["retry_status"] = enrichment.retry_status;
    (*doc)["enrichment_configured"] = voice_notes_enrichment_configured();
    VoiceNotesProcessingSnapshot coordinator{};
    voice_notes_processing_snapshot(&coordinator);
    (*doc)["processing"]["state"] = voice_notes_processing_state_name(coordinator.state);
    (*doc)["processing"]["reason"] = voice_notes_processing_reason_name(coordinator.reason);
    (*doc)["processing"]["service"] = voice_notes_processing_service_name(coordinator.service);
#if USE_SD_STORAGE && defined(SD_STORAGE_SPI) && SD_STORAGE_SPI
    (*doc)["backend"] = "sd_spi";
#elif USE_SD_STORAGE
    (*doc)["backend"] = "sdmmc";
#else
    (*doc)["backend"] = "littlefs";
#endif
    FSHealthStats storage{};
    fs_health_get(&storage);
    const uint64_t total = storage.storage_total_bytes;
    const uint64_t used = storage.storage_used_bytes;
    (*doc)["total_bytes"] = total;
    (*doc)["free_bytes"] = total >= used ? total - used : 0;
    web_portal_send_json_sized(request, doc);
}

void voice_notes_detail_get(AsyncWebServerRequest* request) {
    const String id = request->hasParam("id") ? request->getParam("id")->value() : "";
    if (!voice_note_id_valid(id)) {
        web_portal_send_json_error(request, 400, "Invalid note ID");
        return;
    }
    File recording;
    if (voice_note_file_error(request, voice_notes_open(id.c_str(), &recording))) return;
    auto detail = std::make_shared<VoiceNotesResponse>();
    const bool found = voice_notes_read_metadata(id.c_str(), detail->document["note"].to<JsonObject>());
    if (!found || detail->document.overflowed()) {
        voice_notes_close(&recording, id.c_str());
        web_portal_send_json_error(request, 503, "Note metadata unavailable");
        return;
    }
    File transcript;
    const VoiceNoteFileStatus status = voice_notes_open_artifact(id.c_str(), VoiceNoteArtifact::Transcript, &transcript);
    if (status == VoiceNoteFileStatus::Ready) {
        JsonDocument saved(&detail->allocator);
        const DeserializationError error = deserializeJson(saved, transcript);
        transcript.close();
        JsonObjectConst value = saved["transcript"].as<JsonObjectConst>();
        if (error || saved.overflowed() || value.isNull() ||
            !value["text"].is<const char*>() || !value["model"].is<const char*>() ||
            !value["created_at"].is<uint32_t>()) {
            voice_notes_close(&recording, id.c_str());
            web_portal_send_json_error(request, 500, "Saved transcript is invalid");
            return;
        }
        detail->document["transcript"].set(value);
    } else if (status == VoiceNoteFileStatus::NotFound) {
        detail->document["transcript"] = nullptr;
    } else {
        voice_notes_close(&recording, id.c_str());
        voice_note_file_error(request, status);
        return;
    }
    detail->document["enrichment"] = nullptr;
    detail->document["enrichment_error"] = "";
    File enrichment;
    const VoiceNoteFileStatus enrichment_status =
        voice_notes_open_artifact(id.c_str(), VoiceNoteArtifact::Enrichment, &enrichment);
    if (enrichment_status == VoiceNoteFileStatus::Ready) {
        JsonDocument saved(&detail->allocator);
        const DeserializationError error = deserializeJson(saved, enrichment);
        enrichment.close();
        JsonObjectConst value = saved["enrichment"].as<JsonObjectConst>();
        if (error == DeserializationError::NoMemory || saved.overflowed()) {
            voice_notes_close(&recording, id.c_str());
            web_portal_send_json_error(request, 503, "Voice Notes response memory unavailable");
            return;
        }
        if (error || value.isNull() || !voice_note_valid_string(value["title"], true) ||
            !voice_note_valid_string(value["summary"], false) || !value["model"].is<const char*>() ||
            !value["created_at"].is<uint32_t>()) {
            detail->document["enrichment_error"] = "Saved title and summary are invalid";
        } else {
            detail->document["enrichment"].set(value);
        }
    } else if (enrichment_status != VoiceNoteFileStatus::NotFound) {
        detail->document["enrichment_error"] = "Saved title and summary are unavailable";
    }
    voice_notes_close(&recording, id.c_str());
    voice_notes_send_json(request, detail);
}

void voice_notes_process_post(AsyncWebServerRequest* request, VoiceNotesProcessingMode mode) {
    const String id = request->hasParam("id") ? request->getParam("id")->value() : "";
    const VoiceNotesProcessingStart result = voice_notes_processing_start(id.c_str(), mode);
    if (result != VoiceNotesProcessingStart::Accepted && result != VoiceNotesProcessingStart::Unchanged) {
        switch (result) {
            case VoiceNotesProcessingStart::InvalidId: web_portal_send_json_error(request, 400, "Invalid note ID"); return;
            case VoiceNotesProcessingStart::NotFound: web_portal_send_json_error(request, 404, "Note not found"); return;
            case VoiceNotesProcessingStart::NotConfigured: web_portal_send_json_error(request, 400, "Processing service is not configured"); return;
            case VoiceNotesProcessingStart::MissingResult: web_portal_send_json_error(request, 409, "Required saved result is missing"); return;
            case VoiceNotesProcessingStart::Busy: web_portal_send_json_error(request, 409, "Device is busy"); return;
            case VoiceNotesProcessingStart::Offline: web_portal_send_json_error(request, 503, "Wi-Fi disconnected"); return;
            case VoiceNotesProcessingStart::Cooldown: web_portal_send_json_error(request, 429, "Rate limited; retry later"); return;
            default: web_portal_send_json_error(request, 503, "Processing unavailable"); return;
        }
    }
    const bool unchanged = result == VoiceNotesProcessingStart::Unchanged;
    request->send(unchanged ? 200 : 202, "application/json",
        String("{\"success\":true,\"note_id\":\"") + id +
        "\",\"state\":\"" + (unchanged ? "unchanged" : "accepted") + "\"}");
}
void voice_notes_process(AsyncWebServerRequest* request) { voice_notes_process_post(request, VoiceNotesProcessingMode::Missing); }
void voice_notes_retranscribe(AsyncWebServerRequest* request) { voice_notes_process_post(request, VoiceNotesProcessingMode::Retranscribe); }
void voice_notes_regenerate(AsyncWebServerRequest* request) { voice_notes_process_post(request, VoiceNotesProcessingMode::Regenerate); }

struct VoiceNoteStream {
    File file;
    char id[32] = {};
    ~VoiceNoteStream() { if (file) voice_notes_close(&file, id); }
};

void voice_notes_file_get(AsyncWebServerRequest* request) {
    const String id = request->hasParam("id") ? request->getParam("id")->value() : "";
    if (!voice_note_id_valid(id)) {
        web_portal_send_json_error(request, 400, "Invalid note ID");
        return;
    }
    auto stream = std::make_shared<VoiceNoteStream>();
    strlcpy(stream->id, id.c_str(), sizeof(stream->id));
    if (voice_note_file_error(request, voice_notes_open(id.c_str(), &stream->file))) return;
    const uint64_t size = stream->file.size();
    uint64_t start = 0, end = size ? size - 1 : 0;
    const bool partial = request->hasHeader("Range");
    if (partial && !voice_note_parse_range(request->getHeader("Range")->value().c_str(),
                                            size, &start, &end)) {
        AsyncWebServerResponse* response = request->beginResponse(416, "text/plain", "");
        char range[48];
        snprintf(range, sizeof(range), "bytes */%llu", static_cast<unsigned long long>(size));
        response->addHeader("Content-Range", range);
        response->addHeader("Accept-Ranges", "bytes");
        request->send(response);
        return;
    }
    if (!size || !stream->file.seek(static_cast<size_t>(start))) {
        web_portal_send_json_error(request, 500, "Recording seek failed");
        return;
    }
    const size_t length = static_cast<size_t>(end - start + 1);
    AsyncWebServerResponse* response = request->beginResponse("audio/wav", length,
        [stream, length](uint8_t* buffer, size_t max_len, size_t index) -> size_t {
            if (index >= length) return 0;
            size_t count = length - index;
            if (count > max_len) count = max_len;
            if (count > HTTP_STREAM_CHUNK_SIZE) count = HTTP_STREAM_CHUNK_SIZE;
            const size_t actual = stream->file.read(buffer, count);
            if (actual != count) LOGE("VoiceNotes", "Recording stream read failed");
            return actual;
        });
    if (!response) {
        web_portal_send_json_error(request, 503, "Cannot start recording stream");
        return;
    }
    response->addHeader("Accept-Ranges", "bytes");
    response->addHeader("X-Content-Type-Options", "nosniff");
    if (partial) {
        char range[80];
        snprintf(range, sizeof(range), "bytes %llu-%llu/%llu",
                 static_cast<unsigned long long>(start), static_cast<unsigned long long>(end),
                 static_cast<unsigned long long>(size));
        response->setCode(206);
        response->addHeader("Content-Range", range);
    }
    if (request->hasParam("download"))
        response->addHeader("Content-Disposition", String("attachment; filename=\"") + id + ".wav\"");
    request->send(response);
}

void voice_notes_delete(AsyncWebServerRequest* request) {
    const String id = request->hasParam("id") ? request->getParam("id")->value() : "";
    if (!voice_note_id_valid(id)) {
        web_portal_send_json_error(request, 400, "Invalid note ID");
        return;
    }
    if (voice_note_file_error(request, voice_notes_remove(id.c_str()))) return;
    request->send(200, "application/json", "{\"success\":true}");
}

const ComponentAction voice_notes_actions[] = {
    {"list", HTTP_GET, voice_notes_list_get, nullptr},
    {"status", HTTP_GET, voice_notes_status_get, nullptr},
    {"detail", HTTP_GET, voice_notes_detail_get, nullptr},
    {"process", HTTP_POST, voice_notes_process, nullptr},
    {"retranscribe", HTTP_POST, voice_notes_retranscribe, nullptr},
    {"regenerate", HTTP_POST, voice_notes_regenerate, nullptr},
    {"file", HTTP_GET, voice_notes_file_get, nullptr},
    {"delete", HTTP_DELETE, voice_notes_delete, nullptr},
};
}

static ComponentDef voice_notes_component = {
    .id = "voice-notes",
    .category = "voice-notes",
    .display_name = "Notes",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = voice_notes_actions,
    .num_custom_actions = sizeof(voice_notes_actions) / sizeof(voice_notes_actions[0]),
    .fragment_id = "voice-notes",
    .portal_script = "/portal-voice-notes.js",
};
REGISTER_COMPONENT(voice_notes)

static ComponentDef voice_notes_settings_component = {
    .id = "voice-notes-settings",
    .category = "voice-notes",
    .display_name = "Settings",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "voice-notes-settings",
    .portal_script = "/portal-voice-notes.js",
};
REGISTER_COMPONENT(voice_notes_settings)
