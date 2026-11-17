#include "web_portal_live_microphone.h"

#if HAS_MICROPHONE_STREAMING

#include "audio_input.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"
#include "web_portal_state.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>
#include <string.h>

namespace {

// AsyncTCP may poll an empty response only twice per second.
constexpr size_t kCaptureFrames = 320;
constexpr size_t kSendBufferBytes = 1460;
constexpr uint8_t kStarting = 0;
constexpr uint8_t kStreaming = 1;
constexpr uint8_t kBusy = 2;
constexpr uint8_t kFailed = 3;

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t* g_buffer = nullptr;
int16_t* g_samples = nullptr;
size_t g_buffer_bytes = 0;
size_t g_head = 0;
size_t g_tail = 0;
size_t g_size = 0;
bool g_session = false;
bool g_disconnected = false;
bool g_worker_done = false;
uint8_t g_status = kStarting;
AudioInputFormat g_format = {};
TaskHandle_t g_worker = nullptr;
RtosTaskPsramAlloc g_worker_allocation = {};

bool should_stop() {
    portENTER_CRITICAL(&g_mux);
    const bool stop = g_disconnected;
    portEXIT_CRITICAL(&g_mux);
    return stop || ota_activity_is_active();
}

void capture_session() {
    const AudioInputFormat format = g_format;
    uint8_t status = kFailed;
    bool started = false;
    if (!should_stop() && format.sample_rate && format.bits_per_sample == 16 &&
        (format.channels == 1 || format.channels == 2)) {
        started = audio_input_start_capture();
        status = started ? kStreaming : kBusy;
    }

    portENTER_CRITICAL(&g_mux);
    g_format = format;
    g_status = status;
    portEXIT_CRITICAL(&g_mux);

    while (started && !should_stop()) {
        const AudioInputReadResult read = audio_input_read_frames(g_samples, kCaptureFrames, 50);
        if (read.status == AudioInputReadStatus::Error || read.status == AudioInputReadStatus::Overrun) {
            LOGW("LiveMic", "Microphone capture failed");
            break;
        }
        const size_t bytes = read.frames * format.channels * sizeof(int16_t);
        const uint8_t* source = reinterpret_cast<const uint8_t*>(g_samples);
        portENTER_CRITICAL(&g_mux);
        const size_t frame_bytes = format.channels * sizeof(int16_t);
        while (g_buffer_bytes - g_size < bytes) {
            g_tail = (g_tail + frame_bytes) % g_buffer_bytes;
            g_size -= frame_bytes;
        }
        const size_t first = bytes < g_buffer_bytes - g_head ? bytes : g_buffer_bytes - g_head;
        memcpy(g_buffer + g_head, source, first);
        memcpy(g_buffer, source + first, bytes - first);
        g_head = (g_head + bytes) % g_buffer_bytes;
        g_size += bytes;
        portEXIT_CRITICAL(&g_mux);
    }

    while (audio_input_capture_owned() && !audio_input_stop_capture()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    uint8_t* free_buffer = nullptr;
    portENTER_CRITICAL(&g_mux);
    g_worker_done = true;
    if (g_disconnected) {
        free_buffer = g_buffer;
        g_buffer = nullptr;
        g_samples = nullptr;
        g_buffer_bytes = 0;
        g_session = false;
    }
    portEXIT_CRITICAL(&g_mux);
    if (free_buffer) heap_caps_free(free_buffer);
}

void capture_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        capture_session();
    }
}

size_t fill_stream(uint8_t* output, size_t max_len, size_t index) {
    portENTER_CRITICAL(&g_mux);
    if (g_status == kStarting) {
        portEXIT_CRITICAL(&g_mux);
        return RESPONSE_TRY_AGAIN;
    }
    if (index == 0) {
        if (max_len < 12) {
            portEXIT_CRITICAL(&g_mux);
            return RESPONSE_TRY_AGAIN;
        }
        const uint32_t rate = g_format.sample_rate;
        const uint8_t header[12] = {
            'L', 'M', 1, g_status,
            static_cast<uint8_t>(rate), static_cast<uint8_t>(rate >> 8),
            static_cast<uint8_t>(rate >> 16), static_cast<uint8_t>(rate >> 24),
            g_format.channels, g_format.bits_per_sample, 0, 0
        };
        memcpy(output, header, sizeof(header));
        portEXIT_CRITICAL(&g_mux);
        return sizeof(header);
    }
    const size_t count = g_size < max_len ? g_size : max_len;
    const size_t first = count < g_buffer_bytes - g_tail ? count : g_buffer_bytes - g_tail;
    memcpy(output, g_buffer + g_tail, first);
    memcpy(output + first, g_buffer, count - first);
    g_tail = (g_tail + count) % g_buffer_bytes;
    g_size -= count;
    const bool done = g_worker_done;
    portEXIT_CRITICAL(&g_mux);
    return count ? count : (done ? 0 : RESPONSE_TRY_AGAIN);
}

class LiveMicrophoneResponse final : public AsyncWebServerResponse {
public:
    explicit LiveMicrophoneResponse(uint8_t* pending) : pending_(pending) {
        setCode(200);
        setContentType("application/octet-stream");
        _sendContentLength = false;
        addHeader("Connection", "close", false);
        addHeader("Cache-Control", "no-store");
    }

    bool _sourceValid() const override { return true; }

    void _respond(AsyncWebServerRequest* request) override {
        request_ = request;
        _assembleHead(headers_, request->version());
        _state = RESPONSE_HEADERS;
        pump();
    }

    size_t _ack(AsyncWebServerRequest*, size_t, uint32_t) override { return pump(); }

private:
    size_t pump() {
        if (!request_ || !request_->client() || !request_->client()->canSend()) return 0;
        AsyncClient* client = request_->client();
        size_t written_total = 0;
        bool done = false;
        while (client->space()) {
            if (pending_offset_ == pending_size_) {
                pending_offset_ = pending_size_ = 0;
                if (header_offset_ < headers_.length()) {
                    pending_size_ = headers_.length() - header_offset_;
                    if (pending_size_ > kSendBufferBytes) pending_size_ = kSendBufferBytes;
                    memcpy(pending_, headers_.c_str() + header_offset_, pending_size_);
                    header_offset_ += pending_size_;
                } else {
                    const size_t count = fill_stream(pending_, kSendBufferBytes, stream_offset_);
                    if (count == RESPONSE_TRY_AGAIN) break;
                    if (!count) {
                        done = true;
                        break;
                    }
                    pending_size_ = count;
                    stream_offset_ += count;
                }
            }
            const size_t remaining = pending_size_ - pending_offset_;
            const size_t count = remaining < client->space() ? remaining : client->space();
            const size_t written = client->add(
                reinterpret_cast<const char*>(pending_ + pending_offset_), count);
            if (!written) break;
            pending_offset_ += written;
            written_total += written;
        }
        if (written_total) client->send();
        if (done) client->close();
        return written_total;
    }

    AsyncWebServerRequest* request_ = nullptr;
    String headers_;
    size_t header_offset_ = 0;
    size_t stream_offset_ = 0;
    uint8_t* pending_;
    size_t pending_size_ = 0;
    size_t pending_offset_ = 0;
};

} // namespace

void handleLiveMicrophoneStream(AsyncWebServerRequest* request) {
    if (web_portal_is_recovery_ap_active()) {
        request->send(404, "application/json", "{\"error\":\"Unavailable in recovery mode\"}");
        return;
    }
    if (!request->hasHeader("X-Live-Microphone")) {
        request->send(403, "application/json", "{\"error\":\"Browser request required\"}");
        return;
    }
    if (ota_activity_is_active()) {
        request->send(409, "application/json", "{\"error\":\"Firmware update in progress\"}");
        return;
    }

    portENTER_CRITICAL(&g_mux);
    const bool available = !g_session;
    if (available) {
        g_session = true;
        g_disconnected = false;
        g_worker_done = false;
        g_status = kStarting;
        g_head = g_tail = g_size = 0;
    }
    portEXIT_CRITICAL(&g_mux);
    if (!available) {
        request->send(409, "application/json", "{\"error\":\"Microphone is busy\"}");
        return;
    }

    const AudioInputFormat format = audio_input_format();
    if (format.sample_rate < kCaptureFrames || format.bits_per_sample != 16 ||
        (format.channels != 1 && format.channels != 2)) {
        portENTER_CRITICAL(&g_mux);
        g_session = false;
        portEXIT_CRITICAL(&g_mux);
        request->send(503, "application/json", "{\"error\":\"Unsupported microphone format\"}");
        return;
    }
    const size_t frame_bytes = format.channels * sizeof(int16_t);
    const size_t scratch_bytes = kCaptureFrames * frame_bytes;
    if (format.sample_rate > (SIZE_MAX - scratch_bytes - kSendBufferBytes) / frame_bytes) {
        portENTER_CRITICAL(&g_mux);
        g_session = false;
        portEXIT_CRITICAL(&g_mux);
        request->send(503, "application/json", "{\"error\":\"Unsupported microphone format\"}");
        return;
    }
    const size_t buffer_bytes = format.sample_rate * frame_bytes;
    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(
        buffer_bytes + scratch_bytes + kSendBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer) {
        LOGW("LiveMic", "PSRAM stream allocation failed");
        portENTER_CRITICAL(&g_mux);
        g_session = false;
        portEXIT_CRITICAL(&g_mux);
        request->send(503, "application/json", "{\"error\":\"Not enough PSRAM for microphone stream\"}");
        return;
    }
    g_buffer = buffer;
    g_buffer_bytes = buffer_bytes;
    g_samples = reinterpret_cast<int16_t*>(buffer + buffer_bytes);
    g_format = format;
    AsyncWebServerResponse* response = new (std::nothrow)
        LiveMicrophoneResponse(buffer + buffer_bytes + scratch_bytes);
    if (!response) {
        heap_caps_free(buffer);
        portENTER_CRITICAL(&g_mux);
        g_buffer = nullptr;
        g_samples = nullptr;
        g_buffer_bytes = 0;
        g_session = false;
        portEXIT_CRITICAL(&g_mux);
        request->send(503, "application/json", "{\"error\":\"Cannot create stream\"}");
        return;
    }
    if (!g_worker && !rtos_create_task_psram_stack(capture_task, "live_mic", 4096,
                                                    nullptr, 1, &g_worker, &g_worker_allocation)) {
        LOGW("LiveMic", "PSRAM capture task allocation failed");
        delete response;
        heap_caps_free(buffer);
        portENTER_CRITICAL(&g_mux);
        g_buffer = nullptr;
        g_samples = nullptr;
        g_buffer_bytes = 0;
        g_session = false;
        portEXIT_CRITICAL(&g_mux);
        request->send(503, "application/json", "{\"error\":\"Not enough PSRAM for microphone task\"}");
        return;
    }
    request->onDisconnect([]() {
        uint8_t* free_buffer = nullptr;
        portENTER_CRITICAL(&g_mux);
        g_disconnected = true;
        if (g_worker_done) {
            free_buffer = g_buffer;
            g_buffer = nullptr;
            g_samples = nullptr;
            g_buffer_bytes = 0;
            g_session = false;
        }
        portEXIT_CRITICAL(&g_mux);
        if (free_buffer) heap_caps_free(free_buffer);
    });
    xTaskNotifyGive(g_worker);
    request->send(response);
}

#endif
