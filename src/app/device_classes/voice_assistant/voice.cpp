#include "voice.h"

#if IS_VOICE_ASSISTANT

#include "action_continuation.h"
#include "audio_input.h"
#include "audio.h"
#include "audio_pcm_level.h"
#include "azure_ca.h"
#include "log_manager.h"
#include "voice_auto_stop.h"
#include "voice_config.h"
#include "voice_payload.h"
#include "voice_transcription_request.h"
#include "voice_tts_request.h"
#include "voice_wav.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

namespace {
constexpr char TAG[] = "Voice";
constexpr uint32_t kOutputRate = 16000;
constexpr uint32_t kMaxSeconds = 30;
constexpr size_t kMaxSamples = kOutputRate * kMaxSeconds;
constexpr size_t kMaxWavBytes = VOICE_WAV_HEADER_BYTES + kMaxSamples * sizeof(int16_t);
constexpr uint32_t kReadTimeoutMs = 250;
constexpr uint32_t kRequestTimeoutMs = 30000;
static_assert(kRequestTimeoutMs < ACTION_CONTINUATION_TIMEOUT_MS,
              "Azure request timeout must remain below action continuation timeout");
constexpr uint32_t kWorkerStackBytes = 12288;
constexpr size_t kTtsMaxMp3Bytes = 256 * 1024;
constexpr size_t kTtsInitialMp3Bytes = 16 * 1024;
constexpr size_t kTtsRequestBodyBytes = 2048;
constexpr char kBoundary[] = "----esp32macropadvoice";
constexpr char kApiVersion[] = "2025-03-01-preview";

struct VoiceState {
    VoiceStatus status = VOICE_STATUS_IDLE;
    char text[sizeof(VoiceSnapshot::text)] = {};
    bool stop_requested = false;
    bool manual_stop_requested = false;
    bool cancel_requested = false;
    bool auto_stop = false;
    uint16_t silence_ms = 0;
    uint8_t speech_threshold = 0;
    uint32_t continuation_token = 0;
};

struct VoiceCaptureOptions {
    bool auto_stop;
    uint16_t silence_ms;
    uint8_t speech_threshold;
};

struct VoiceTtsJob {
    uint32_t generation;
    char text[VOICE_TTS_TEXT_MAX_LEN];
    char voice[VOICE_TTS_VOICE_MAX_LEN];
    uint8_t volume;
};

VoiceState g_state = {};
portMUX_TYPE g_state_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_task = nullptr;
QueueHandle_t g_tts_queue = nullptr;
uint32_t g_tts_generation = 0;
bool g_capture_pending = false;
char g_tts_request_body[kTtsRequestBodyBytes] = {};

void set_result(VoiceStatus status, const char* text) {
    portENTER_CRITICAL(&g_state_mux);
    g_state.status = status;
    strlcpy(g_state.text, text ? text : "", sizeof(g_state.text));
    portEXIT_CRITICAL(&g_state_mux);
}

bool stop_requested() {
    portENTER_CRITICAL(&g_state_mux);
    const bool requested = g_state.stop_requested;
    portEXIT_CRITICAL(&g_state_mux);
    return requested;
}

bool cancel_requested() {
    portENTER_CRITICAL(&g_state_mux);
    const bool requested = g_state.cancel_requested;
    portEXIT_CRITICAL(&g_state_mux);
    return requested;
}

bool manual_stop_requested() {
    portENTER_CRITICAL(&g_state_mux);
    const bool requested = g_state.manual_stop_requested;
    portEXIT_CRITICAL(&g_state_mux);
    return requested;
}

VoiceCaptureOptions capture_options() {
    portENTER_CRITICAL(&g_state_mux);
    const VoiceCaptureOptions options = {
        g_state.auto_stop,
        g_state.silence_ms,
        g_state.speech_threshold,
    };
    portEXIT_CRITICAL(&g_state_mux);
    return options;
}

uint32_t take_continuation_token() {
    portENTER_CRITICAL(&g_state_mux);
    const uint32_t token = g_state.continuation_token;
    g_state.continuation_token = 0;
    g_state.stop_requested = false;
    g_state.manual_stop_requested = false;
    g_state.cancel_requested = false;
    g_state.auto_stop = false;
    portEXIT_CRITICAL(&g_state_mux);
    return token;
}

bool tts_generation_active(uint32_t generation) {
    portENTER_CRITICAL(&g_state_mux);
    const bool active = g_tts_generation == generation;
    portEXIT_CRITICAL(&g_state_mux);
    return active;
}

bool tts_generation_current(uint32_t generation) {
    if (!tts_generation_active(generation)) {
        portENTER_CRITICAL(&g_state_mux);
        const uint32_t current_generation = g_tts_generation;
        portEXIT_CRITICAL(&g_state_mux);
        LOGD(TAG, "TTS generation %u superseded by %u", (unsigned)generation,
             (unsigned)current_generation);
        return false;
    }
    return true;
}

bool write_all(WiFiClientSecure& client, const uint8_t* data, size_t length) {
    while (length) {
        const size_t written = client.write(data, length);
        if (!written) return false;
        data += written;
        length -= written;
    }
    return true;
}

bool read_tts_bytes(WiFiClientSecure& client, uint8_t* out, size_t length, uint32_t started,
                    uint32_t generation) {
    size_t received = 0;
    while ((uint32_t)(millis() - started) < kRequestTimeoutMs) {
        if (!tts_generation_active(generation)) return false;
        const size_t available = client.available();
        if (available) {
            const size_t count = available < length - received ? available : length - received;
            const int read = client.read(out + received, count);
            if (read > 0) {
                received += (size_t)read;
                if (received == length) return true;
            }
        }
        if (!client.connected()) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

bool grow_tts_mp3_buffer(uint8_t** mp3, size_t* capacity, size_t required) {
    if (required <= *capacity) return true;
    size_t new_capacity = *capacity;
    while (new_capacity < required && new_capacity < kTtsMaxMp3Bytes) new_capacity *= 2;
    if (new_capacity > kTtsMaxMp3Bytes) new_capacity = kTtsMaxMp3Bytes;
    if (new_capacity < required) return false;
    uint8_t* grown = (uint8_t*)heap_caps_realloc(*mp3, new_capacity,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!grown) return false;
    *mp3 = grown;
    *capacity = new_capacity;
    return true;
}

bool read_tts_chunked_mp3(WiFiClientSecure& client, uint8_t** mp3, size_t* mp3_capacity,
                          size_t* out_size, uint32_t started, uint32_t generation) {
    *out_size = 0;
    for (;;) {
        char line[20] = {};
        size_t line_len = 0;
        for (;;) {
            uint8_t byte = 0;
            if (!read_tts_bytes(client, &byte, 1, started, generation) ||
                line_len + 1 >= sizeof(line)) return false;
            if (byte == '\n') break;
            if (byte != '\r') line[line_len++] = (char)byte;
        }
        char* extension = strchr(line, ';');
        if (extension) *extension = '\0';
        char* end = nullptr;
        const unsigned long chunk_size = strtoul(line, &end, 16);
        if (!line[0] || !end || *end || chunk_size > kTtsMaxMp3Bytes - *out_size) return false;
        if (!chunk_size) return true;
        if (!grow_tts_mp3_buffer(mp3, mp3_capacity, *out_size + chunk_size) ||
            !read_tts_bytes(client, *mp3 + *out_size, chunk_size, started, generation)) return false;
        *out_size += chunk_size;
        uint8_t crlf[2] = {};
        if (!read_tts_bytes(client, crlf, sizeof(crlf), started, generation) ||
            crlf[0] != '\r' || crlf[1] != '\n') {
            return false;
        }
    }
}

const char* find_http_header_value(const char* headers, const char* name) {
    const size_t name_len = strlen(name);
    const char* line = strchr(headers, '\n');
    while (line && *line) {
        ++line;
        const char* line_end = strchr(line, '\n');
        if (!line_end || line == line_end || (line + 1 == line_end && line[0] == '\r')) break;
        if (strncasecmp(line, name, name_len) == 0 && line[name_len] == ':') {
            const char* value = line + name_len + 1;
            while (*value == ' ' || *value == '\t') ++value;
            return value;
        }
        line = line_end;
    }
    return nullptr;
}

bool http_header_value_contains(const char* value, const char* needle) {
    if (!value) return false;
    const size_t needle_len = strlen(needle);
    for (; *value && *value != '\r' && *value != '\n'; ++value) {
        if (strncasecmp(value, needle, needle_len) == 0) return true;
    }
    return false;
}

bool upload_transcription(const uint8_t* wav, size_t wav_size, char* transcript,
                          size_t transcript_size) {
    VoiceConfig config = {};
    voice_config_snapshot(&config);
    if (!config.azure_api_key[0]) {
        strlcpy(transcript, "Azure API key is not configured", transcript_size);
        return false;
    }
    if (!config.azure_host[0] || !config.azure_deployment[0]) {
        strlcpy(transcript, "Azure host or deployment is not configured", transcript_size);
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        strlcpy(transcript, "Wi-Fi is not connected", transcript_size);
        return false;
    }

    char prefix[384] = {};
    const int prefix_len = voice_transcription_build_multipart_prefix(
        prefix, sizeof(prefix), kBoundary, config.azure_language);
    char suffix[sizeof(kBoundary) + 8] = {};
    const int suffix_len = snprintf(suffix, sizeof(suffix), "\r\n--%s--\r\n", kBoundary);
    char path[192] = {};
    const int path_len = snprintf(path, sizeof(path),
        "/openai/deployments/%s/audio/transcriptions?api-version=%s",
        config.azure_deployment, kApiVersion);
    if (prefix_len < 0 || suffix_len < 0 || path_len < 0 || (size_t)prefix_len >= sizeof(prefix) ||
        (size_t)suffix_len >= sizeof(suffix) || (size_t)path_len >= sizeof(path)) {
        strlcpy(transcript, "Azure request is too long", transcript_size);
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(VOICE_AZURE_CA_CERT);
    client.setTimeout(kRequestTimeoutMs);
    if (!client.connect(config.azure_host, 443)) {
        strlcpy(transcript, "Azure connection failed", transcript_size);
        return false;
    }
    const size_t content_length = (size_t)prefix_len + wav_size + (size_t)suffix_len;
    char request[640] = {};
    const int request_len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\nHost: %s\r\napi-key: %s\r\nContent-Type: multipart/form-data; boundary=%s\r\n"
        "Content-Length: %u\r\nConnection: close\r\n\r\n",
        path, config.azure_host, config.azure_api_key, kBoundary, (unsigned)content_length);
    if (request_len < 0 || (size_t)request_len >= sizeof(request) ||
        !write_all(client, (const uint8_t*)request, request_len) ||
        !write_all(client, (const uint8_t*)prefix, prefix_len) ||
        !write_all(client, wav, wav_size) ||
        !write_all(client, (const uint8_t*)suffix, suffix_len)) {
        client.stop();
        strlcpy(transcript, "Azure upload failed", transcript_size);
        return false;
    }

    char response[2048] = {};
    const uint32_t started = millis();
    size_t response_len = 0;
    while ((uint32_t)(millis() - started) < kRequestTimeoutMs) {
        while (client.available()) {
            if (response_len + 1 >= sizeof(response)) {
                client.stop();
                strlcpy(transcript, "Azure response is too large", transcript_size);
                return false;
            }
            response[response_len++] = (char)client.read();
        }
        if (!client.connected() && !client.available()) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    const bool response_timed_out = (uint32_t)(millis() - started) >= kRequestTimeoutMs;
    client.stop();
    response[response_len] = '\0';
    if (response_timed_out) {
        strlcpy(transcript, "Azure request timed out", transcript_size);
        return false;
    }
    const char* body = strstr(response, "\r\n\r\n");
    int status = 0;
    sscanf(response, "HTTP/%*u.%*u %d", &status);
    if (!body || status < 200 || status >= 300) {
        snprintf(transcript, transcript_size, "Azure HTTP error %d", status);
        return false;
    }
    body += 4;
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        strlcpy(transcript, "Azure returned invalid JSON", transcript_size);
        return false;
    }
    const char* text = doc["text"] | "";
    if (!text[0]) {
        strlcpy(transcript, "Azure returned no transcript", transcript_size);
        return false;
    }
    strlcpy(transcript, text, transcript_size);
    return true;
}

bool download_tts_mp3(const VoiceTtsJob& job, uint8_t** out_mp3, size_t* out_mp3_size) {
    *out_mp3 = nullptr;
    *out_mp3_size = 0;
    VoiceConfig config = {};
    voice_config_snapshot(&config);
    if (!config.tts_api_key[0] || !config.tts_host[0] || !config.tts_deployment[0]) {
        LOGW(TAG, "TTS unavailable: host, deployment, or API key is not configured");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        LOGW(TAG, "TTS unavailable: Wi-Fi is not connected");
        return false;
    }
    const char* voice = job.voice[0] ? job.voice : config.tts_voice;
    const int body_len = voice_tts_build_request_json(g_tts_request_body, sizeof(g_tts_request_body), job.text,
                                                      config.tts_deployment, voice,
                                                      config.tts_language,
                                                      config.tts_instructions);
    char path[192] = {};
    const int path_len = snprintf(path, sizeof(path),
        "/openai/deployments/%s/audio/speech?api-version=%s",
        config.tts_deployment, kApiVersion);
    if (body_len < 0 || path_len < 0 || (size_t)path_len >= sizeof(path)) {
        LOGW(TAG, "TTS request is too long or invalid");
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(VOICE_AZURE_CA_CERT);
    client.setTimeout(kRequestTimeoutMs);
    if (!client.connect(config.tts_host, 443)) {
        LOGW(TAG, "TTS Azure connection failed");
        return false;
    }
    char request[640] = {};
    const int request_len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\nHost: %s\r\napi-key: %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %u\r\nConnection: close\r\n\r\n",
        path, config.tts_host, config.tts_api_key, (unsigned)body_len);
    if (request_len < 0 || (size_t)request_len >= sizeof(request) ||
        !write_all(client, (const uint8_t*)request, request_len) ||
        !write_all(client, (const uint8_t*)g_tts_request_body, body_len)) {
        client.stop();
        LOGW(TAG, "TTS Azure upload failed");
        return false;
    }

    char headers[1024] = {};
    size_t header_len = 0;
    const uint32_t started = millis();
    while ((uint32_t)(millis() - started) < kRequestTimeoutMs) {
        if (!tts_generation_active(job.generation)) {
            client.stop();
            return false;
        }
        while (client.available()) {
            if (header_len + 1 >= sizeof(headers)) {
                client.stop();
                LOGW(TAG, "TTS Azure response headers are too large");
                return false;
            }
            headers[header_len++] = (char)client.read();
            headers[header_len] = '\0';
            if (header_len >= 4 && strcmp(headers + header_len - 4, "\r\n\r\n") == 0) goto headers_complete;
        }
        if (!client.connected()) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    client.stop();
    LOGW(TAG, "TTS Azure response headers timed out");
    return false;

headers_complete:
    int status = 0;
    unsigned content_length = 0;
    const char* content_length_header = find_http_header_value(headers, "Content-Length");
    const char* content_type_header = find_http_header_value(headers, "Content-Type");
    const char* transfer_encoding_header = find_http_header_value(headers, "Transfer-Encoding");
    const bool chunked = http_header_value_contains(transfer_encoding_header, "chunked");
    sscanf(headers, "HTTP/%*u.%*u %d", &status);
    if (content_length_header) sscanf(content_length_header, "%u", &content_length);
    if (!content_type_header || !http_header_value_contains(content_type_header, "audio/mpeg") ||
        status < 200 || status >= 300 || (!chunked && (!content_length || content_length > kTtsMaxMp3Bytes))) {
        client.stop();
        LOGW(TAG, "TTS Azure response rejected: HTTP %d, length=%u, chunked=%d", status,
             content_length, chunked);
        return false;
    }
    size_t mp3_capacity = chunked ? kTtsInitialMp3Bytes : content_length;
    uint8_t* mp3 = (uint8_t*)heap_caps_malloc(mp3_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mp3) {
        client.stop();
        LOGW(TAG, "TTS could not allocate %u-byte MP3 buffer", (unsigned)mp3_capacity);
        return false;
    }
    size_t received = 0;
    if (chunked) {
        if (!read_tts_chunked_mp3(client, &mp3, &mp3_capacity, &received, started, job.generation)) {
            heap_caps_free(mp3);
            client.stop();
            LOGW(TAG, "TTS Azure chunked download failed");
            return false;
        }
    } else {
        while (received < content_length && (uint32_t)(millis() - started) < kRequestTimeoutMs) {
            if (!tts_generation_active(job.generation)) break;
            const size_t available = client.available();
            if (available) {
                const size_t remaining = content_length - received;
                const size_t count = available < remaining ? available : remaining;
                const int read = client.read(mp3 + received, count);
                if (read > 0) received += (size_t)read;
            }
            if (received < content_length) vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    client.stop();
    if (!received || (!chunked && received != content_length)) {
        heap_caps_free(mp3);
        LOGW(TAG, "TTS Azure download incomplete: %u/%u bytes", (unsigned)received,
             content_length);
        return false;
    }
    LOGD(TAG, "TTS downloaded %u-byte MP3", (unsigned)received);
    *out_mp3 = mp3;
    *out_mp3_size = received;
    return true;
}

bool capture_wav(const VoiceCaptureOptions& options, uint8_t** out_wav, size_t* out_wav_size,
                 bool* was_cancelled, char* error, size_t error_size) {
    *out_wav = nullptr;
    *out_wav_size = 0;
    *was_cancelled = false;
    const AudioInputFormat format = audio_input_format();
    if (format.bits_per_sample != 16 || format.channels == 0 ||
        format.sample_rate < kOutputRate || format.sample_rate % kOutputRate != 0) {
        strlcpy(error, "Unsupported microphone format", error_size);
        return false;
    }
    const uint32_t decimation = format.sample_rate / kOutputRate;
    uint8_t* wav = (uint8_t*)heap_caps_malloc(kMaxWavBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t raw_frames = 512;
    int16_t* raw = (int16_t*)heap_caps_malloc(raw_frames * format.channels * sizeof(int16_t),
                                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!wav || !raw) {
        if (wav) heap_caps_free(wav);
        if (raw) heap_caps_free(raw);
        strlcpy(error, "Insufficient memory for recording", error_size);
        return false;
    }
    if (!audio_input_start_capture()) {
        heap_caps_free(wav);
        heap_caps_free(raw);
        strlcpy(error, "Microphone is busy", error_size);
        return false;
    }

    int16_t* pcm = (int16_t*)(wav + VOICE_WAV_HEADER_BYTES);
    size_t samples_written = 0;
    uint32_t input_frame = 0;
    VoiceAutoStopDetector auto_stop = {};
    if (options.auto_stop) {
        voice_auto_stop_init(&auto_stop, options.silence_ms, options.speech_threshold);
    }
    while (!stop_requested() && samples_written < kMaxSamples) {
        const size_t frames_read = audio_input_read_frames(raw, raw_frames, kReadTimeoutMs);
        if (!frames_read) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        for (size_t frame = 0; frame < frames_read && samples_written < kMaxSamples; ++frame, ++input_frame) {
            if (input_frame % decimation == 0) {
                pcm[samples_written++] = raw[frame * format.channels + format.channels - 1];
            }
        }
        if (options.auto_stop) {
            const uint32_t elapsed_ms = (uint32_t)((frames_read * 1000U) / format.sample_rate);
            if (voice_auto_stop_update(&auto_stop,
                                       audio_pcm_rms_level(raw, frames_read, format.channels),
                                       elapsed_ms ? elapsed_ms : 1U)) {
                break;
            }
        }
    }
    audio_input_stop_capture();
    heap_caps_free(raw);
    if (cancel_requested()) {
        *was_cancelled = true;
        heap_caps_free(wav);
        return false;
    }
    if (options.auto_stop && !auto_stop.speech_detected && !manual_stop_requested()) {
        heap_caps_free(wav);
        strlcpy(error, "No speech detected", error_size);
        return false;
    }
    if (!samples_written) {
        heap_caps_free(wav);
        strlcpy(error, "No microphone samples received", error_size);
        return false;
    }
    const uint32_t data_bytes = (uint32_t)(samples_written * sizeof(int16_t));
    voice_wav_build_header(wav, data_bytes, kOutputRate, 1, 16);
    *out_wav = wav;
    *out_wav_size = VOICE_WAV_HEADER_BYTES + data_bytes;
    return true;
}

void voice_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&g_state_mux);
        const bool capture_pending = g_capture_pending;
        g_capture_pending = false;
        portEXIT_CRITICAL(&g_state_mux);
        if (capture_pending) {
        const VoiceCaptureOptions options = capture_options();
        uint8_t* wav = nullptr;
        size_t wav_size = 0;
        bool was_cancelled = false;
        char result[sizeof(VoiceSnapshot::text)] = {};
        bool success = capture_wav(options, &wav, &wav_size, &was_cancelled, result, sizeof(result));
        if (was_cancelled) {
            set_result(VOICE_STATUS_IDLE, "");
            const uint32_t token = take_continuation_token();
            if (token) action_continuation_complete(token, false);
        } else if (success) {
            set_result(VOICE_STATUS_TRANSCRIBING, "");
            success = upload_transcription(wav, wav_size, result, sizeof(result));
            heap_caps_free(wav);
        }
        if (!was_cancelled) {
            set_result(success ? VOICE_STATUS_READY : VOICE_STATUS_ERROR, result);
            const uint32_t token = take_continuation_token();
            if (token) action_continuation_complete(token, success);
        }
        }
        VoiceTtsJob job = {};
        while (xQueueReceive(g_tts_queue, &job, 0) == pdTRUE) {
            uint8_t* mp3 = nullptr;
            size_t mp3_size = 0;
            if (!tts_generation_current(job.generation)) continue;
            if (!download_tts_mp3(job, &mp3, &mp3_size)) continue;
            if (!tts_generation_current(job.generation)) {
                heap_caps_free(mp3);
                continue;
            }
            audio_play_mp3_buffer(mp3, mp3_size, job.volume, tts_generation_current, job.generation);
        }
    }
}
} // namespace

const char* voice_status_name(VoiceStatus status) {
    switch (status) {
        case VOICE_STATUS_RECORDING: return "recording";
        case VOICE_STATUS_LISTENING: return "listening";
        case VOICE_STATUS_TRANSCRIBING: return "transcribing";
        case VOICE_STATUS_READY: return "ready";
        case VOICE_STATUS_ERROR: return "error";
        default: return "idle";
    }
}

bool voice_api_key_configured() {
    VoiceConfig config = {};
    voice_config_snapshot(&config);
    return config.azure_api_key[0] != '\0';
}

void voice_init() {
    g_tts_queue = xQueueCreate(1, sizeof(VoiceTtsJob));
    if (!g_tts_queue) {
        set_result(VOICE_STATUS_ERROR, "Voice TTS queue initialization failed");
        return;
    }
    if (xTaskCreatePinnedToCore(voice_task, "Voice", kWorkerStackBytes, nullptr, 3, &g_task, 0) != pdPASS) {
        g_task = nullptr;
        vQueueDelete(g_tts_queue);
        g_tts_queue = nullptr;
        set_result(VOICE_STATUS_ERROR, "Voice task initialization failed");
    }
}

bool start_recording(bool auto_stop, uint16_t silence_ms, uint8_t speech_threshold,
                     uint32_t continuation_token) {
    if (!g_task || (auto_stop && !continuation_token)) return false;
    portENTER_CRITICAL(&g_state_mux);
    const bool available = g_state.status == VOICE_STATUS_IDLE || g_state.status == VOICE_STATUS_READY ||
                           g_state.status == VOICE_STATUS_ERROR;
    if (available) {
        ++g_tts_generation;
        g_state.status = auto_stop ? VOICE_STATUS_LISTENING : VOICE_STATUS_RECORDING;
        g_state.text[0] = '\0';
        g_state.stop_requested = false;
        g_state.manual_stop_requested = false;
        g_state.cancel_requested = false;
        g_state.auto_stop = auto_stop;
        g_state.silence_ms = silence_ms;
        g_state.speech_threshold = speech_threshold;
        g_state.continuation_token = continuation_token;
        g_capture_pending = true;
    }
    portEXIT_CRITICAL(&g_state_mux);
    if (available) xTaskNotifyGive(g_task);
    return available;
}

bool voice_start_recording() {
    return start_recording(false, 0, 0, 0);
}

bool voice_start_until_silence(uint16_t silence_ms, uint8_t speech_threshold,
                               uint32_t continuation_token) {
    return start_recording(true, silence_ms, speech_threshold, continuation_token);
}

VoiceStopResult voice_stop_and_transcribe(uint32_t continuation_token) {
    portENTER_CRITICAL(&g_state_mux);
    if (g_state.status == VOICE_STATUS_LISTENING) {
        g_state.stop_requested = true;
        g_state.manual_stop_requested = true;
        g_state.status = VOICE_STATUS_TRANSCRIBING;
        portEXIT_CRITICAL(&g_state_mux);
        return VOICE_STOP_AUTO_CONTINUATION;
    }
    const bool recording = g_state.status == VOICE_STATUS_RECORDING && continuation_token;
    if (recording) {
        g_state.stop_requested = true;
        g_state.status = VOICE_STATUS_TRANSCRIBING;
        g_state.continuation_token = continuation_token;
    }
    portEXIT_CRITICAL(&g_state_mux);
    return recording ? VOICE_STOP_PENDING : VOICE_STOP_FAILED;
}

bool voice_cancel_recording() {
    portENTER_CRITICAL(&g_state_mux);
    const bool recording = g_state.status == VOICE_STATUS_RECORDING ||
                           g_state.status == VOICE_STATUS_LISTENING;
    if (recording) {
        g_state.stop_requested = true;
        g_state.cancel_requested = true;
    }
    portEXIT_CRITICAL(&g_state_mux);
    return recording;
}

bool voice_speak(const char* text, const char* voice, uint8_t volume) {
    if (!g_task || !g_tts_queue || !text || !text[0]) return false;
    VoiceTtsJob job = {};
    strlcpy(job.text, text, sizeof(job.text));
    strlcpy(job.voice, voice ? voice : "", sizeof(job.voice));
    job.volume = volume <= 100 ? volume : 0;
    portENTER_CRITICAL(&g_state_mux);
    job.generation = ++g_tts_generation;
    portEXIT_CRITICAL(&g_state_mux);
    LOGD(TAG, "TTS generation %u queued", (unsigned)job.generation);
    audio_stop();
    VoiceTtsJob discarded = {};
    while (xQueueReceive(g_tts_queue, &discarded, 0) == pdTRUE) {}
    if (xQueueSend(g_tts_queue, &job, 0) != pdTRUE) return false;
    xTaskNotifyGive(g_task);
    return true;
}

void voice_get_snapshot(VoiceSnapshot* out) {
    if (!out) return;
    portENTER_CRITICAL(&g_state_mux);
    out->status = g_state.status;
    strlcpy(out->text, g_state.text, sizeof(out->text));
    portEXIT_CRITICAL(&g_state_mux);
}

#endif // IS_VOICE_ASSISTANT