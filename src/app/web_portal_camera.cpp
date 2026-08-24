#include "web_portal_camera.h"

#if HAS_CAMERA

#include "camera.h"
#include "camera_feed.h"
#include "log_manager.h"
#include "main_loop_bridge.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"

#include <memory>
#include <new>
#include <esp_heap_caps.h>

#define TAG "CameraAPI"
#define CAMERA_CAPTURE_TIMEOUT_MS 2000

namespace {

constexpr char kMjpegBoundary[] = "esp32-macropad";
portMUX_TYPE s_mjpeg_stream_mux = portMUX_INITIALIZER_UNLOCKED;
bool s_mjpeg_stream_active = false;

bool camera_mjpeg_stream_reserve() {
    portENTER_CRITICAL(&s_mjpeg_stream_mux);
    const bool available = !s_mjpeg_stream_active;
    if (available) s_mjpeg_stream_active = true;
    portEXIT_CRITICAL(&s_mjpeg_stream_mux);
    return available;
}

void camera_mjpeg_stream_release() {
    portENTER_CRITICAL(&s_mjpeg_stream_mux);
    s_mjpeg_stream_active = false;
    portEXIT_CRITICAL(&s_mjpeg_stream_mux);
}

enum CameraMjpegFrameResult : uint8_t {
    CAMERA_MJPEG_FRAME_WAIT,
    CAMERA_MJPEG_FRAME_READY,
    CAMERA_MJPEG_FRAME_FAILED,
};

class CameraMjpegResponse final : public AsyncWebServerResponse {
public:
    CameraMjpegResponse() {
        setCode(200);
        String content_type = "multipart/x-mixed-replace; boundary=";
        content_type += kMjpegBoundary;
        setContentType(content_type);
        _sendContentLength = false;
        addHeader("Cache-Control", "no-store");
        camera_feed_acquire_demand(CAMERA_FEED_OUTPUT_JPEG);
    }

    ~CameraMjpegResponse() override {
        if (jpeg_copy_) free(jpeg_copy_);
        camera_feed_release_demand(CAMERA_FEED_OUTPUT_JPEG);
        camera_mjpeg_stream_release();
        LOGI(TAG, "MJPEG stream closed");
    }

    bool _sourceValid() const override {
        return true;
    }

    void _respond(AsyncWebServerRequest* request) override {
        request_ = request;
        addHeader("Connection", "close", false);
        _assembleHead(headers_, request->version());
        _state = RESPONSE_HEADERS;
        pump();
    }

    size_t _ack(AsyncWebServerRequest*, size_t, uint32_t) override {
        return pump();
    }

    void poll() {
        pump();
    }

private:
    enum Part : uint8_t { PART_NEED_FRAME, PART_HEADER, PART_JPEG, PART_TRAILER };
    static constexpr size_t kSendBufferSize = 1460;

    size_t pump() {
        if (!request_ || !request_->client() || !request_->client()->canSend()) return 0;

        size_t queued = 0;
        while (request_->client()->space()) {
            if (pending_offset_ == pending_size_ && !prepare_pending()) break;

            const size_t written = request_->client()->add(
                reinterpret_cast<const char*>(pending_ + pending_offset_), pending_size_ - pending_offset_);
            if (!written) break;
            pending_offset_ += written;
            queued += written;
            if (pending_offset_ != pending_size_) break;
        }

        if (queued) request_->client()->send();
        return queued;
    }

    bool prepare_pending() {
        pending_size_ = 0;
        pending_offset_ = 0;
        if (_state == RESPONSE_HEADERS) {
            if (headers_offset_ < headers_.length()) {
                const size_t remaining = headers_.length() - headers_offset_;
                pending_size_ = std::min(remaining, sizeof(pending_));
                memcpy(pending_, headers_.c_str() + headers_offset_, pending_size_);
                headers_offset_ += pending_size_;
                return true;
            }
            headers_ = String();
            _state = RESPONSE_CONTENT;
        }

        if (part_ == PART_NEED_FRAME && load_latest_frame() != CAMERA_MJPEG_FRAME_READY) return false;

        const uint8_t* source = nullptr;
        size_t source_size = 0;
        if (part_ == PART_HEADER) {
            source = reinterpret_cast<const uint8_t*>(part_header_);
            source_size = part_header_size_;
        } else if (part_ == PART_JPEG) {
            source = jpeg_copy_;
            source_size = jpeg_size_;
        } else {
            source = reinterpret_cast<const uint8_t*>("\r\n");
            source_size = 2;
        }

        pending_size_ = std::min(source_size - part_offset_, sizeof(pending_));
        memcpy(pending_, source + part_offset_, pending_size_);
        part_offset_ += pending_size_;
        if (part_offset_ == source_size) advance_part();
        return true;
    }

    void advance_part() {
        part_offset_ = 0;
        if (part_ == PART_HEADER) {
            part_ = PART_JPEG;
        } else if (part_ == PART_JPEG) {
            part_ = PART_TRAILER;
            ++sent_frames_;
        } else {
            part_ = PART_NEED_FRAME;
        }
    }

    CameraMjpegFrameResult load_latest_frame() {
        CameraFeedFrame frame = {};
        if (!camera_feed_acquire_frame(&frame, CAMERA_FEED_OUTPUT_JPEG)) {
            return CAMERA_MJPEG_FRAME_WAIT;
        }

        const CameraJpegFrame* jpeg = frame.jpeg;
        if (!jpeg || !jpeg->data || !jpeg->size || frame.generation == generation_) {
            camera_feed_release_frame(&frame);
            return CAMERA_MJPEG_FRAME_WAIT;
        }

        if (jpeg->size > jpeg_capacity_) {
            uint8_t* grown = static_cast<uint8_t*>(
                heap_caps_realloc(jpeg_copy_, jpeg->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!grown) {
                camera_feed_release_frame(&frame);
                LOGE(TAG, "MJPEG frame copy allocation failed: %u bytes",
                     static_cast<unsigned>(jpeg->size));
                return CAMERA_MJPEG_FRAME_FAILED;
            }
            jpeg_copy_ = grown;
            jpeg_capacity_ = jpeg->size;
        }

        memcpy(jpeg_copy_, jpeg->data, jpeg->size);
        jpeg_size_ = jpeg->size;
        generation_ = frame.generation;
        camera_feed_release_frame(&frame);

        const int header_size = snprintf(part_header_, sizeof(part_header_),
            "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
            kMjpegBoundary, static_cast<unsigned>(jpeg_size_));
        if (header_size < 0 || static_cast<size_t>(header_size) >= sizeof(part_header_)) {
            return CAMERA_MJPEG_FRAME_FAILED;
        }
        part_header_size_ = static_cast<size_t>(header_size);
        part_ = PART_HEADER;
        return CAMERA_MJPEG_FRAME_READY;
    }

    uint8_t* jpeg_copy_ = nullptr;
    size_t jpeg_capacity_ = 0;
    size_t jpeg_size_ = 0;
    uint32_t generation_ = 0;
    char part_header_[96] = {};
    size_t part_header_size_ = 0;
    size_t part_offset_ = 0;
    uint16_t sent_frames_ = 0;
    AsyncWebServerRequest* request_ = nullptr;
    String headers_;
    size_t headers_offset_ = 0;
    uint8_t pending_[kSendBufferSize] = {};
    size_t pending_size_ = 0;
    size_t pending_offset_ = 0;
    Part part_ = PART_NEED_FRAME;
    bool failed_ = false;
};

} // namespace

struct CameraSnapshotRequest {
    CameraRawFrame* frame;
};

struct CameraSnapshotResponse {
    CameraRawFrame frame = {};

    ~CameraSnapshotResponse() {
        camera_release_raw(&frame);
    }
};

struct CameraJpegSnapshotRequest {
    CameraJpegFrame* frame;
};

struct CameraJpegSnapshotResponse {
    CameraJpegFrame frame = {};

    ~CameraJpegSnapshotResponse() {
        camera_release_jpeg(&frame);
    }
};

static void capture_camera_snapshot(const void* opaque, bool* ok, char* message, size_t message_len) {
    const CameraSnapshotRequest* request = static_cast<const CameraSnapshotRequest*>(opaque);
    if (!request || !request->frame) {
        snprintf(message, message_len, "invalid camera capture request");
        return;
    }

    if (!camera_capture_raw(request->frame)) {
        snprintf(message, message_len, "camera capture failed");
        return;
    }
    *ok = true;
}

static void cleanup_abandoned_camera_snapshot(const void* opaque) {
    const CameraSnapshotRequest* request = static_cast<const CameraSnapshotRequest*>(opaque);
    if (!request || !request->frame) return;
    camera_release_raw(request->frame);
    delete request->frame;
}

static void capture_camera_jpeg_snapshot(const void* opaque, bool* ok, char* message, size_t message_len) {
    const CameraJpegSnapshotRequest* request = static_cast<const CameraJpegSnapshotRequest*>(opaque);
    if (!request || !request->frame) {
        snprintf(message, message_len, "invalid camera JPEG capture request");
        return;
    }

    if (!camera_capture_jpeg(request->frame)) {
        snprintf(message, message_len, "camera JPEG capture failed");
        return;
    }
    *ok = true;
}

static void cleanup_abandoned_camera_jpeg_snapshot(const void* opaque) {
    const CameraJpegSnapshotRequest* request = static_cast<const CameraJpegSnapshotRequest*>(opaque);
    if (!request || !request->frame) return;
    camera_release_jpeg(request->frame);
    delete request->frame;
}

void handleGetCameraRawSnapshot(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!camera_is_detected()) {
        web_portal_send_json_error(request, 503, "Camera unavailable");
        return;
    }

    CameraRawFrame* frame = new (std::nothrow) CameraRawFrame{};
    if (!frame) {
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }

    const CameraSnapshotRequest capture_request = { .frame = frame };
    bool captured = false;
    char message[96] = {};
    const LoopBridgeResult result = loop_bridge_dispatch(
        capture_camera_snapshot, &capture_request, sizeof(capture_request),
        CAMERA_CAPTURE_TIMEOUT_MS, &captured, message, sizeof(message),
        cleanup_abandoned_camera_snapshot);
    if (result != LOOP_BRIDGE_OK || !captured) {
        if (result != LOOP_BRIDGE_TIMEOUT) {
            camera_release_raw(frame);
            delete frame;
        }
        web_portal_send_json_error(request, result == LOOP_BRIDGE_BUSY ? 429 : 503,
                                  message[0] ? message : "Camera capture unavailable");
        return;
    }

    auto response_context = std::make_shared<CameraSnapshotResponse>();
    response_context->frame = *frame;
    *frame = {};
    delete frame;

    AsyncWebServerResponse* response = request->beginChunkedResponse(
        "application/octet-stream",
        [response_context](uint8_t* buffer, size_t max_len, size_t index) -> size_t {
            if (index >= response_context->frame.size) return 0;
            const size_t remaining = response_context->frame.size - index;
            const size_t count = remaining < max_len ? remaining : max_len;
            memcpy(buffer, response_context->frame.data + index, count);
            return count;
        });
    response->addHeader("Content-Disposition", "attachment; filename=camera.raw10");
    response->addHeader("X-Camera-Width", String(response_context->frame.width));
    response->addHeader("X-Camera-Height", String(response_context->frame.height));
    response->addHeader("X-Camera-Pixel-Format", "RAW10");
    web_portal_add_cors_headers(response);
    request->send(response);
}

void handleGetCameraJpegSnapshot(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!camera_is_detected()) {
        web_portal_send_json_error(request, 503, "Camera unavailable");
        return;
    }

    CameraJpegFrame* frame = new (std::nothrow) CameraJpegFrame{};
    if (!frame) {
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }

    const CameraJpegSnapshotRequest capture_request = { .frame = frame };
    bool captured = false;
    char message[96] = {};
    const LoopBridgeResult result = loop_bridge_dispatch(
        capture_camera_jpeg_snapshot, &capture_request, sizeof(capture_request),
        CAMERA_CAPTURE_TIMEOUT_MS, &captured, message, sizeof(message),
        cleanup_abandoned_camera_jpeg_snapshot);
    if (result != LOOP_BRIDGE_OK || !captured) {
        if (result != LOOP_BRIDGE_TIMEOUT) {
            camera_release_jpeg(frame);
            delete frame;
        }
        web_portal_send_json_error(request, result == LOOP_BRIDGE_BUSY ? 429 : 503,
                                  message[0] ? message : "Camera JPEG capture unavailable");
        return;
    }

    auto response_context = std::make_shared<CameraJpegSnapshotResponse>();
    response_context->frame = *frame;
    *frame = {};
    delete frame;

    AsyncWebServerResponse* response = request->beginChunkedResponse(
        "image/jpeg",
        [response_context](uint8_t* buffer, size_t max_len, size_t index) -> size_t {
            if (index >= response_context->frame.size) return 0;
            const size_t remaining = response_context->frame.size - index;
            const size_t count = remaining < max_len ? remaining : max_len;
            memcpy(buffer, response_context->frame.data + index, count);
            return count;
        });
    response->addHeader("Content-Disposition", "inline; filename=camera.jpg");
    response->addHeader("X-Camera-Width", String(response_context->frame.width));
    response->addHeader("X-Camera-Height", String(response_context->frame.height));
    response->addHeader("X-Camera-Color-Mode", "color");
    web_portal_add_cors_headers(response);
    request->send(response);
}

void handleGetCameraMjpegStream(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!camera_is_detected()) {
        web_portal_send_json_error(request, 503, "Camera unavailable");
        return;
    }
    if (!camera_mjpeg_stream_reserve()) {
        web_portal_send_json_error(request, 429, "Camera stream already in use");
        return;
    }

    CameraMjpegResponse* response = new (std::nothrow) CameraMjpegResponse();
    if (!response) {
        camera_mjpeg_stream_release();
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }

    LOGI(TAG, "MJPEG stream opened");
    web_portal_add_cors_headers(response);
    request->send(response);
    request->client()->onPoll(
        [](void* context, AsyncClient*) {
            static_cast<CameraMjpegResponse*>(context)->poll();
        },
        response);
}

#endif