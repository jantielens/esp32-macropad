#include "web_portal_camera.h"

#if HAS_CAMERA

#include "camera.h"
#include "main_loop_bridge.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"

#include <memory>
#include <new>

#define TAG "CameraAPI"
#define CAMERA_CAPTURE_TIMEOUT_MS 2000

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

#endif