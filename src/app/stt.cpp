#include "stt.h"

#if HAS_STT

#include "audio.h"
#include "binding_template.h"
#include "log_manager.h"
#include "stt_wav.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#if HAS_MQTT
#include "mqtt_manager.h"
#include <freertos/queue.h>
#endif

#if __has_include("stt_credentials.h")
#include "stt_credentials.h"
#else
#include "stt_credentials.h.example"
#endif

namespace {

constexpr char TAG[] = "STT";
constexpr uint32_t kCodecSampleRate = AUDIO_SAMPLE_RATE;
constexpr uint32_t kWavSampleRate = 16000;
constexpr uint32_t kMaxRecordingSeconds = 30;
constexpr uint32_t kMaxSamples = kWavSampleRate * kMaxRecordingSeconds;
constexpr size_t kMaxWavBytes = STT_WAV_HEADER_BYTES + kMaxSamples * sizeof(int16_t);
constexpr uint32_t kRequestTimeoutMs = 30000;
constexpr uint32_t kReadTimeoutMs = 250;
constexpr uint32_t kWorkerStackBytes = 12288;
constexpr size_t kMqttTopicBytes = 128;
constexpr char kBoundary[] = "----esp32macropadstt";
constexpr char kAzureApiVersion[] = "2025-03-01-preview";

static_assert(kCodecSampleRate % kWavSampleRate == 0,
              "STT sample rate must divide the codec sample rate");

struct SttState {
    SttStatus status = STT_STATUS_IDLE;
    char text[sizeof(SttSnapshot::text)] = {};
};

SttState g_state;
portMUX_TYPE g_state_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool g_stop_requested = false;
TaskHandle_t g_task = nullptr;

#if HAS_MQTT
struct SttMqttPublish {
    char topic[kMqttTopicBytes];
    char payload[sizeof(SttSnapshot::text)];
};

StaticQueue_t g_publish_queue_storage;
uint8_t g_publish_queue_buffer[sizeof(SttMqttPublish)];
QueueHandle_t g_publish_queue = nullptr;
char g_completion_mqtt_topic[kMqttTopicBytes] = {};

void queue_transcript_publish(const char* transcript) {
    char topic[sizeof(g_completion_mqtt_topic)];
    portENTER_CRITICAL(&g_state_mux);
    strlcpy(topic, g_completion_mqtt_topic, sizeof(topic));
    g_completion_mqtt_topic[0] = '\0';
    portEXIT_CRITICAL(&g_state_mux);
    if (!topic[0] || !g_publish_queue) return;

    SttMqttPublish message = {};
    strlcpy(message.topic, topic, sizeof(message.topic));
    strlcpy(message.payload, transcript ? transcript : "", sizeof(message.payload));
    if (xQueueOverwrite(g_publish_queue, &message) != pdPASS) {
        LOGW(TAG, "Transcript MQTT publish queue unavailable");
    }
}
#endif

const char* status_name(SttStatus status) {
    switch (status) {
        case STT_STATUS_IDLE: return "idle";
        case STT_STATUS_RECORDING: return "recording";
        case STT_STATUS_TRANSCRIBING: return "transcribing";
        case STT_STATUS_READY: return "ready";
        case STT_STATUS_ERROR: return "error";
    }
    return "error";
}

void set_state(SttStatus status, const char* text = "") {
    portENTER_CRITICAL(&g_state_mux);
    g_state.status = status;
    strlcpy(g_state.text, text ? text : "", sizeof(g_state.text));
    portEXIT_CRITICAL(&g_state_mux);
}

bool is_api_key_configured() {
    return STT_AZURE_API_KEY[0] &&
           strcmp(STT_AZURE_API_KEY, "REPLACE_WITH_YOUR_AZURE_FOUNDRY_API_KEY") != 0;
}

bool write_all(WiFiClientSecure& client, const uint8_t* data, size_t length) {
    while (length > 0) {
        size_t written = client.write(data, length);
        if (written == 0) return false;
        data += written;
        length -= written;
    }
    return true;
}

bool upload_transcription(const uint8_t* wav, size_t wav_size, char* transcript,
                          size_t transcript_size) {
    if (!is_api_key_configured()) {
        LOGE(TAG, "Upload blocked api_key=missing");
        strlcpy(transcript, "STT API key is not configured", transcript_size);
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        LOGE(TAG, "Upload blocked wifi=disconnected");
        strlcpy(transcript, "Wi-Fi is not connected", transcript_size);
        return false;
    }
    LOGI(TAG, "Upload start wav_bytes=%u", (unsigned)wav_size);

    char prefix[256];
    int prefix_len = snprintf(prefix, sizeof(prefix),
                              "--%s\r\n"
                              "Content-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\n"
                              "Content-Type: audio/wav\r\n\r\n",
                              kBoundary);
    const char suffix[] = "\r\n--" "----esp32macropadstt" "--\r\n";
    if (prefix_len <= 0 || (size_t)prefix_len >= sizeof(prefix)) {
        LOGE(TAG, "Upload request construction failed");
        strlcpy(transcript, "STT request construction failed", transcript_size);
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(kRequestTimeoutMs);
    if (!client.connect(STT_AZURE_HOST, 443)) {
        LOGE(TAG, "Azure connect failed");
        strlcpy(transcript, "Azure connection failed", transcript_size);
        return false;
    }

    const size_t content_length = (size_t)prefix_len + wav_size + strlen(suffix);
    char request_path[256];
    int request_path_len = snprintf(request_path, sizeof(request_path),
                                    "/openai/deployments/%s/audio/transcriptions?api-version=%s",
                                    STT_AZURE_MODEL, kAzureApiVersion);
    if (request_path_len <= 0 || (size_t)request_path_len >= sizeof(request_path)) {
        client.stop();
        LOGE(TAG, "Upload path construction failed");
        strlcpy(transcript, "STT request construction failed", transcript_size);
        return false;
    }
    char headers[512];
    int header_len = snprintf(headers, sizeof(headers),
                              "POST %s HTTP/1.1\r\n"
                              "Host: %s\r\n"
                              "api-key: %s\r\n"
                              "Content-Type: multipart/form-data; boundary=%s\r\n"
                              "Content-Length: %u\r\n"
                              "Connection: close\r\n\r\n",
                              request_path, STT_AZURE_HOST, STT_AZURE_API_KEY,
                              kBoundary, (unsigned)content_length);
    if (header_len <= 0 || (size_t)header_len >= sizeof(headers) ||
        !write_all(client, (const uint8_t*)headers, (size_t)header_len) ||
        !write_all(client, (const uint8_t*)prefix, (size_t)prefix_len) ||
        !write_all(client, wav, wav_size) ||
        !write_all(client, (const uint8_t*)suffix, strlen(suffix))) {
        client.stop();
        LOGE(TAG, "Azure upload write failed");
        strlcpy(transcript, "Azure upload failed", transcript_size);
        return false;
    }

    char response[2048] = {};
    char status_line[64] = {};
    size_t response_len = 0;
    size_t status_line_len = 0;
    bool body_started = false;
    bool status_line_complete = false;
    uint8_t delimiter_match = 0;
    const uint32_t deadline = millis() + kRequestTimeoutMs;
    while ((client.connected() || client.available()) && (int32_t)(deadline - millis()) > 0) {
        while (client.available()) {
            int value = client.read();
            if (value < 0) break;
            char c = (char)value;
            if (!body_started) {
                if (!status_line_complete) {
                    if (c == '\r') {
                        status_line_complete = true;
                    } else if (status_line_len < sizeof(status_line) - 1) {
                        status_line[status_line_len++] = c;
                    }
                }
                static constexpr char delimiter[] = "\r\n\r\n";
                if (c == delimiter[delimiter_match]) {
                    delimiter_match++;
                    if (delimiter_match == sizeof(delimiter) - 1) body_started = true;
                } else {
                    delimiter_match = (c == delimiter[0]) ? 1 : 0;
                }
            } else if (response_len < sizeof(response) - 1) {
                response[response_len++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    client.stop();
    response[response_len] = '\0';
    int http_status = 0;
    if (status_line[0]) sscanf(status_line, "HTTP/%*u.%*u %d", &http_status);
    LOGI(TAG, "Azure response status=%d body_bytes=%u", http_status, (unsigned)response_len);
    if (!body_started) {
        LOGE(TAG, "Azure response headers incomplete");
        strlcpy(transcript, "Azure response timed out", transcript_size);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);
    if (http_status < 200 || http_status >= 300) {
        const char* error_code = !err ? doc["error"]["code"] | "unknown" : "unparsed";
        LOGE(TAG, "Azure rejected request status=%d code=%s", http_status, error_code);
        snprintf(transcript, transcript_size, "Azure HTTP error %d", http_status);
        return false;
    }

    if (err) {
        LOGE(TAG, "Azure response JSON invalid err=%s", err.c_str());
        strlcpy(transcript, "Azure returned invalid JSON", transcript_size);
        return false;
    }
    const char* text = !err ? doc["text"].as<const char*>() : nullptr;
    if (!text || !text[0]) {
        LOGE(TAG, "Azure response missing transcript status=%d", http_status);
        strlcpy(transcript, "Azure returned no transcript", transcript_size);
        return false;
    }
    LOGI(TAG, "Azure transcription complete chars=%u", (unsigned)strlen(text));
    strlcpy(transcript, text, transcript_size);
    return true;
}

bool capture_wav(uint8_t** out_wav, size_t* out_wav_size, char* error, size_t error_size) {
    *out_wav = nullptr;
    *out_wav_size = 0;
    uint8_t* wav = (uint8_t*)heap_caps_malloc(kMaxWavBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t* raw = (int16_t*)heap_caps_malloc(2048, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!wav || !raw) {
        if (wav) heap_caps_free(wav);
        if (raw) heap_caps_free(raw);
        LOGE(TAG, "Recording allocation failed");
        strlcpy(error, "Insufficient memory for recording", error_size);
        return false;
    }

    int16_t* pcm = (int16_t*)(wav + STT_WAV_HEADER_BYTES);
    size_t samples_written = 0;
    uint8_t phase = 0;
    uint16_t left_peak = 0;
    uint16_t right_peak = 0;
    while (!g_stop_requested && samples_written < kMaxSamples) {
        size_t samples_read = 0;
        if (!audio_read_input(raw, 1024, &samples_read, kReadTimeoutMs)) continue;
        for (size_t i = 0; i + 1 < samples_read && samples_written < kMaxSamples; i += 2) {
            const int32_t left = raw[i];
            const int32_t right = raw[i + 1];
            const uint16_t left_magnitude = (uint16_t)(left < 0 ? -left : left);
            const uint16_t right_magnitude = (uint16_t)(right < 0 ? -right : right);
            if (left_magnitude > left_peak) left_peak = left_magnitude;
            if (right_magnitude > right_peak) right_peak = right_magnitude;
            // Preserve the second I2S channel for the mono STT stream.
            if (phase == 0) pcm[samples_written++] = raw[i + 1];
            phase = (phase + 1) % (kCodecSampleRate / kWavSampleRate);
        }
    }
    heap_caps_free(raw);
    if (samples_written == 0) {
        heap_caps_free(wav);
        LOGE(TAG, "Recording failed samples=0");
        strlcpy(error, "No microphone samples received", error_size);
        return false;
    }

    const uint32_t data_bytes = (uint32_t)(samples_written * sizeof(int16_t));
    stt_wav_build_header(wav, data_bytes, kWavSampleRate, 1, 16);
    *out_wav = wav;
    *out_wav_size = STT_WAV_HEADER_BYTES + data_bytes;
        LOGI(TAG, "Recording complete wav_bytes=%u duration_ms=%u left_peak=%u right_peak=%u",
            (unsigned)*out_wav_size, (unsigned)(samples_written * 1000 / kWavSampleRate),
            (unsigned)left_peak, (unsigned)right_peak);
    return true;
}

void stt_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint8_t* wav = nullptr;
        size_t wav_size = 0;
        char result[sizeof(SttSnapshot::text)] = {};
        LOGI(TAG, "Recording started");
        if (!capture_wav(&wav, &wav_size, result, sizeof(result))) {
            set_state(STT_STATUS_ERROR, result);
            continue;
        }

        set_state(STT_STATUS_TRANSCRIBING);
        LOGI(TAG, "Transcription started");
        bool success = upload_transcription(wav, wav_size, result, sizeof(result));
        heap_caps_free(wav);
        set_state(success ? STT_STATUS_READY : STT_STATUS_ERROR, result);
    #if HAS_MQTT
        if (success) queue_transcript_publish(result);
    #endif
        LOGI(TAG, "Transcription finished success=%d", success);
    }
}

bool stt_binding_resolve(const char* params, char* out, size_t out_len) {
    SttSnapshot snapshot;
    stt_get_snapshot(&snapshot);
    if (strcmp(params, "status") == 0) {
        strlcpy(out, status_name(snapshot.status), out_len);
        return true;
    }
    if (strcmp(params, "text") == 0) {
        strlcpy(out, snapshot.text, out_len);
        return snapshot.text[0] != '\0';
    }
    return false;
}

void stt_binding_collect(const char*, void*) {}

const char* stt_binding_validate(const char* params) {
    return strcmp(params, "status") == 0 || strcmp(params, "text") == 0
        ? nullptr : "stt key must be status or text";
}

#if HAS_MCP
void stt_binding_describe(void* out_json) {
    JsonObject& out = *static_cast<JsonObject*>(out_json);
    out["syntax"] = "[stt:status|text]";
    out["example"] = "[stt:text]";
    out["keys"] = "status, text";
    out["read_only"] = true;
}
#endif

} // namespace

void stt_init() {
    if (!binding_template_register("stt", stt_binding_resolve, stt_binding_collect)) {
        LOGE(TAG, "Failed to register STT binding scheme");
    } else {
        LOGI(TAG, "Binding scheme registered");
    }
#if HAS_MCP
    binding_template_set_scheme_describe("stt", stt_binding_describe);
    binding_template_set_scheme_validate("stt", stt_binding_validate);
#endif
#if HAS_MQTT
    g_publish_queue = xQueueCreateStatic(1, sizeof(SttMqttPublish), g_publish_queue_buffer,
                                         &g_publish_queue_storage);
    if (!g_publish_queue) {
        LOGE(TAG, "Failed to create transcript MQTT publish queue");
    }
#endif
    if (xTaskCreatePinnedToCore(stt_task, "stt", kWorkerStackBytes, nullptr,
                                3, &g_task, 0) != pdPASS) {
        LOGE(TAG, "Failed to create STT task");
        set_state(STT_STATUS_ERROR, "STT task initialization failed");
    } else {
        LOGI(TAG, "Worker task ready");
    }
}

bool stt_start_recording() {
    if (!g_task) return false;
    portENTER_CRITICAL(&g_state_mux);
    bool available = g_state.status == STT_STATUS_IDLE || g_state.status == STT_STATUS_READY ||
                     g_state.status == STT_STATUS_ERROR;
    if (available) {
        g_state.status = STT_STATUS_RECORDING;
        g_state.text[0] = '\0';
        g_stop_requested = false;
    }
    portEXIT_CRITICAL(&g_state_mux);
    if (available) xTaskNotifyGive(g_task);
    return available;
}

bool stt_stop_and_transcribe(const char* mqtt_topic) {
    portENTER_CRITICAL(&g_state_mux);
    bool recording = g_state.status == STT_STATUS_RECORDING;
    if (recording) {
        g_stop_requested = true;
#if HAS_MQTT
        strlcpy(g_completion_mqtt_topic, mqtt_topic ? mqtt_topic : "",
                sizeof(g_completion_mqtt_topic));
#endif
    }
    portEXIT_CRITICAL(&g_state_mux);
    return recording;
}

void stt_get_snapshot(SttSnapshot* out) {
    if (!out) return;
    portENTER_CRITICAL(&g_state_mux);
    out->status = g_state.status;
    strlcpy(out->text, g_state.text, sizeof(out->text));
    portEXIT_CRITICAL(&g_state_mux);
}

void stt_loop() {
#if HAS_MQTT
    if (!g_publish_queue) return;
    SttMqttPublish message = {};
    if (xQueueReceive(g_publish_queue, &message, 0) != pdPASS) return;
    bool published = mqtt_manager.publish(message.topic, message.payload, false);
    LOGI(TAG, "Transcript MQTT publish topic=%s success=%d", message.topic, published);
#endif
}

#endif // HAS_STT