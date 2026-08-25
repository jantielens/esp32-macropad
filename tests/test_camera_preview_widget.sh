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
CAMERA_DRIVER="src/app/drivers/ov02c10_p4_driver.cpp"
CAMERA_COMPONENT="src/app/components/camera_component.cpp"
CAMERA_PORTAL="src/app/web/portal_camera.js"
grep -q 'class CameraMjpegResponse final : public AsyncWebServerResponse' "$CAMERA_API"
grep -q 'CAMERA_FEED_OUTPUT_JPEG' "$CAMERA_API"
grep -q 'camera_feed_release_frame(&frame)' "$CAMERA_API"
grep -q 'void abort()' "$CAMERA_API"
grep -q 'request->client()->onPoll' "$CAMERA_API"
grep -q 'CAMERA_MJPEG_MAX_CLIENTS' "$CAMERA_API"
grep -q 'Camera stream client limit reached' "$CAMERA_API"
grep -q 'camera_stop_csi_capture();' "$CAMERA_DRIVER"
grep -q 's_rgb565_raw_staging' "$CAMERA_DRIVER"
grep -q '#define HAS_CAMERA true' src/boards/jc1060p470c/board_overrides.h
grep -q '#define CAMERA_DRIVER CAMERA_DRIVER_OV02C10_P4' src/boards/jc1060p470c/board_overrides.h
grep -q '../jc1060p470c/board_overrides.h' src/boards/jc1060p470c-sd/board_overrides.h
grep -q 'exposure_line_time_us' "$CAMERA_COMPONENT"
grep -q 'feed_target_fps' "$CAMERA_COMPONENT"
grep -q 'CAMERA_FEED_TARGET_FPS_DEFAULT' src/app/camera.h
grep -q 'camera-feed-target-fps' src/app/web/camera.fragment.html
grep -q 'camera-exposure-time-value' src/app/web/camera.fragment.html
grep -q 'camera-wb-red" type="range"' src/app/web/camera.fragment.html
grep -q 'camera-wb-blue" type="range"' src/app/web/camera.fragment.html
grep -q 'updateExposureValue' "$CAMERA_PORTAL"
grep -q 'updateWhiteBalanceValue' "$CAMERA_PORTAL"
grep -q 'feedTargetFps' "$CAMERA_PORTAL"
grep -q 'handleGetCameraMjpegStream' src/app/web_portal_routes.cpp
grep -q '#define CAMERA_MJPEG_MAX_CLIENTS 3' src/boards/jc4880p433/board_overrides.h

echo "PASS: camera preview widget wiring"