#include "board_config.h"

#if HAS_VOICE_NOTES
#include "voice_notes_enrichment_request.h"
#include "voice_notes_enrichment.h"
#include "voice_notes_transport.h"
#include "bounded_psram_json_allocator.h"
#include <esp_heap_caps.h>
#include <string.h>

bool voice_notes_enrichment_request(const VoiceNotesServiceConfig& config, const char* transcript,
                                    uint32_t epoch, uint64_t generation,
                                    JsonDocument& content, VoiceNotesRequestResult& result) {
    result = {};
    constexpr char path[] = "openai/v1/chat/completions";
    const size_t endpoint_length = strlen(config.endpoint);
    if (endpoint_length > SIZE_MAX - sizeof(path)) {
        result.error = "Enrichment endpoint is too long";
        result.outcome = VoiceNotesRequestOutcome::ResourceFailure;
        return false;
    }
    auto* url = static_cast<char*>(heap_caps_malloc(endpoint_length + sizeof(path),
                                                    voice_notes_transport::kPsramCaps));
    if (!url) {
        result.error = "Enrichment request memory unavailable";
        result.outcome = VoiceNotesRequestOutcome::ResourceFailure;
        return false;
    }
    memcpy(url, config.endpoint, endpoint_length);
    memcpy(url + endpoint_length, path, sizeof(path));
    WiFiClientSecure client;
    HTTPClient http;
    const bool begun = voice_notes_transport::begin(http, client, url, config.api_key);
    heap_caps_free(url);
    if (!begun) {
        result.error = "Invalid LLM endpoint";
        result.outcome = VoiceNotesRequestOutcome::SetupFailure;
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    {
        BoundedPsramJsonAllocator allocator(heap_caps_get_free_size(voice_notes_transport::kPsramCaps));
        JsonDocument request(&allocator);
        if (!voice_notes_enrichment_build_request(request, config.deployment, transcript)) {
            voice_notes_transport::close(http, client);
            result.error = "Enrichment request memory unavailable";
            result.outcome = VoiceNotesRequestOutcome::ResourceFailure;
            return false;
        }
        const size_t length = measureJson(request);
        if (!length || length == SIZE_MAX) {
            voice_notes_transport::close(http, client);
            result.error = "Enrichment request serialization failed";
            result.outcome = VoiceNotesRequestOutcome::ResourceFailure;
            return false;
        }
        auto* bytes = static_cast<uint8_t*>(heap_caps_malloc(length + 1, voice_notes_transport::kPsramCaps));
        if (!bytes) {
            voice_notes_transport::close(http, client);
            result.error = "Enrichment request memory unavailable";
            result.outcome = VoiceNotesRequestOutcome::ResourceFailure;
            return false;
        }
        const size_t written = serializeJson(request, reinterpret_cast<char*>(bytes), length + 1);
        if (written != length) {
            heap_caps_free(bytes);
            voice_notes_transport::close(http, client);
            result.error = "Enrichment request serialization failed";
            result.outcome = VoiceNotesRequestOutcome::ResourceFailure;
            return false;
        }
        result.status = http.sendRequest("POST", bytes, length);
        heap_caps_free(bytes);
    }
    net_activity_mark(NET_CH_HTTP);
    if (!voice_notes_transcription_checkpoint(epoch, generation)) {
        voice_notes_transport::close(http, client);
        result.error = "Recording or update interrupted enrichment";
        result.outcome = VoiceNotesRequestOutcome::Interrupted;
        return false;
    }
    if (result.status == 429) result.cooldown_ms = voice_notes_transport::retry_after_ms(http);
    if (result.status != 200 && result.status != 400) {
        voice_notes_transport::close(http, client);
        if (result.status == 401 || result.status == 403 || result.status == 404) {
            result.outcome = VoiceNotesRequestOutcome::SetupFailure;
            result.error = "Check LLM endpoint, deployment, and API key";
        } else if (result.status == 429) {
            result.outcome = VoiceNotesRequestOutcome::RateLimited;
            result.error = "LLM service rate limited; retry later";
        } else if (result.status == 413 || result.status == 415 || result.status == 422) {
            result.outcome = VoiceNotesRequestOutcome::NoteFailure;
            result.error = "LLM service rejected this transcript";
        } else {
            result.outcome = VoiceNotesRequestOutcome::SharedFailure;
            result.error = "LLM service request failed";
        }
        return false;
    }
    if (http.getSize() < 0 && !http.header("Transfer-Encoding").equalsIgnoreCase("chunked")) {
        voice_notes_transport::close(http, client);
        result.error = "Unsupported LLM response framing";
        return false;
    }
    voice_notes_transport::ResponseStream body(epoch, generation);
    const int received = http.writeToStream(&body);
    voice_notes_transport::close(http, client);
    if (!voice_notes_transcription_checkpoint(epoch, generation)) {
        result.error = "Recording or update interrupted enrichment";
        result.outcome = VoiceNotesRequestOutcome::Interrupted;
        return false;
    }
    if (received < 0 || body.failed() || !body.data()) {
        if (body.out_of_memory()) result.outcome = VoiceNotesRequestOutcome::ResourceFailure;
        result.error = "LLM response could not be read";
        return false;
    }
    BoundedPsramJsonAllocator allocator(heap_caps_get_free_size(voice_notes_transport::kPsramCaps));
    JsonDocument envelope(&allocator);
    if (result.status == 400) {
        const DeserializationError error = deserializeJson(envelope, body.data(), body.size());
        JsonString code = envelope["error"]["code"].as<JsonString>();
        if (error || envelope.overflowed() || !envelope["error"]["code"].is<const char*>() ||
            !code.c_str() || strlen(code.c_str()) != code.size()) {
            result.outcome = error == DeserializationError::NoMemory || envelope.overflowed()
                ? VoiceNotesRequestOutcome::ResourceFailure : VoiceNotesRequestOutcome::SharedFailure;
            result.error = "LLM error response could not be read";
        } else {
            const bool note_error = strcmp(code.c_str(), "context_length_exceeded") == 0 ||
                                    strcmp(code.c_str(), "content_filter") == 0;
            result.outcome = note_error ? VoiceNotesRequestOutcome::NoteFailure
                                        : VoiceNotesRequestOutcome::SetupFailure;
            result.error = note_error ? "LLM service rejected this transcript"
                                      : "Check LLM service settings";
        }
        return false;
    }
    const VoiceNotesEnrichmentParse parsed =
        voice_notes_enrichment_parse_response(body.data(), body.size(), envelope, content);
    if (parsed != VoiceNotesEnrichmentParse::Success) {
        result.outcome = parsed == VoiceNotesEnrichmentParse::NoteFailure ? VoiceNotesRequestOutcome::NoteFailure :
                         parsed == VoiceNotesEnrichmentParse::ResourceFailure ? VoiceNotesRequestOutcome::ResourceFailure :
                         VoiceNotesRequestOutcome::SharedFailure;
        result.error = "Invalid or incomplete title and summary response";
        return false;
    }
    result.outcome = VoiceNotesRequestOutcome::Success;
    return true;
}
#endif
