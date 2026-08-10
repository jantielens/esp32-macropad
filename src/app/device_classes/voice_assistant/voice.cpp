#include "voice.h"

#if IS_VOICE_ASSISTANT

#include "action_continuation.h"
#include "audio_input.h"
#include "audio_pcm_level.h"
#include "azure_ca.h"
#include "log_manager.h"
#include "voice_auto_stop.h"
#include "voice_config.h"
#include "voice_transcription_request.h"
#include "voice_wav.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace {
constexpr char TAG[] = "Voice";
constexpr uint32_t kOutputRate = 16000;
constexpr uint32_t kMaxSeconds = 30;
constexpr size_t kMaxSamples = kOutputRate * kMaxSeconds;
constexpr size_t kMaxWavBytes = VOICE_WAV_HEADER_BYTES + kMaxSamples * sizeof(int16_t);
constexpr uint32_t kReadTimeoutMs = 250;
constexpr uint32_t kRequestTimeoutMs = 30000;
constexpr uint32_t kWorkerStackBytes = 12288;
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

VoiceState g_state = {};
portMUX_TYPE g_state_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_task = nullptr;

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

bool write_all(WiFiClientSecure& client, const uint8_t* data, size_t length) {
    while (length) {
        const size_t written = client.write(data, length);
        if (!written) return false;
        data += written;
        length -= written;
    }
    return true;
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
    client.stop();
    response[response_len] = '\0';
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
            continue;
        }
        if (success) {
            set_result(VOICE_STATUS_TRANSCRIBING, "");
            success = upload_transcription(wav, wav_size, result, sizeof(result));
            heap_caps_free(wav);
        }
        set_result(success ? VOICE_STATUS_READY : VOICE_STATUS_ERROR, result);
        const uint32_t token = take_continuation_token();
        if (token) action_continuation_complete(token, success);
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
    if (xTaskCreatePinnedToCore(voice_task, "Voice", kWorkerStackBytes, nullptr, 3, &g_task, 0) != pdPASS) {
        g_task = nullptr;
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
        g_state.status = auto_stop ? VOICE_STATUS_LISTENING : VOICE_STATUS_RECORDING;
        g_state.text[0] = '\0';
        g_state.stop_requested = false;
        g_state.manual_stop_requested = false;
        g_state.cancel_requested = false;
        g_state.auto_stop = auto_stop;
        g_state.silence_ms = silence_ms;
        g_state.speech_threshold = speech_threshold;
        g_state.continuation_token = continuation_token;
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

void voice_get_snapshot(VoiceSnapshot* out) {
    if (!out) return;
    portENTER_CRITICAL(&g_state_mux);
    out->status = g_state.status;
    strlcpy(out->text, g_state.text, sizeof(out->text));
    portEXIT_CRITICAL(&g_state_mux);
}

#endif // IS_VOICE_ASSISTANT