#!/bin/bash
# Camera preview widget wiring guard.
set -e
cd "$(dirname "$0")/.."

WIDGET="src/app/widgets/camera_preview_widget.cpp"

grep -q '#if HAS_DISPLAY && HAS_CAMERA' "$WIDGET"
grep -q 'REGISTER_WIDGET_SCHEMA(camera_preview, nullptr, false)' "$WIDGET"
grep -q 'CAMERA_FEED_OUTPUT_RGB565' "$WIDGET"
grep -q 'CAMERA_PREVIEW_SCALE_LETTERBOX' "$WIDGET"
grep -q 'CAMERA_PREVIEW_SCALE_CENTER_CROP' "$WIDGET"
grep -q 'LV_IMAGE_ALIGN_CONTAIN' "$WIDGET"
grep -q 'widget_camera_scale' "$WIDGET"
grep -q 'screen_saver_manager_is_asleep()' "$WIDGET"
grep -A2 '#if HAS_CAMERA' src/app/widgets.cpp | grep -q 'camera_preview_widget.cpp'
grep -q 'value="camera_preview"' src/app/web/pad-editor.fragment.html
grep -q 'WIDGET_CAMERA_PREVIEW' src/app/web/pad-editor.fragment.html
grep -q 'widget_camera_scale' src/app/web/portal_pad_dialog.js
grep -q 'has_camera === true' src/app/web/portal_pad_editor.js

CAMERA_API="src/app/web_portal_camera.cpp"
grep -q 'class CameraMjpegResponse final : public AsyncWebServerResponse' "$CAMERA_API"
grep -q 'CAMERA_FEED_OUTPUT_JPEG' "$CAMERA_API"
grep -q 'camera_feed_release_frame(&frame)' "$CAMERA_API"
grep -q 'request->client()->onPoll' "$CAMERA_API"
grep -q 'CAMERA_MJPEG_MAX_CLIENTS' "$CAMERA_API"
grep -q 'Camera stream client limit reached' "$CAMERA_API"
grep -q 'handleGetCameraMjpegStream' src/app/web_portal_routes.cpp
grep -q '#define CAMERA_MJPEG_MAX_CLIENTS 3' src/boards/jc4880p433/board_overrides.h

echo "PASS: camera preview widget wiring"