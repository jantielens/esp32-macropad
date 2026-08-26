#!/bin/bash
# Camera motion sensing source-wiring guard.
set -e
cd "$(dirname "$0")/.."

MOTION="src/app/camera_motion.cpp"
FEED="src/app/camera_feed.cpp"
SENSOR="src/app/sensors/camera_presence_sensor.cpp"
COMPONENT="src/app/components/camera_component.cpp"

grep -q '#if HAS_CAMERA' "$MOTION"
grep -q 'CAMERA_MOTION_ANALYZE_EVERY_DEFAULT' src/app/camera_motion.h
grep -q 'CAMERA_PRESENCE_HOLD_SECONDS_DEFAULT' src/app/camera_motion.h
grep -q 'MALLOC_CAP_SPIRAM' "$MOTION"
! grep -q 'camera_capture_rgb565' "$MOTION"
! grep -q 'camera_capture_jpeg' "$MOTION"
grep -q 'camera_motion_loop();' "$FEED"
grep -q 'camera_motion_on_raw_frame(s_raw_frame,' "$FEED"
grep -q 'camera_capture_raw_reuse(&s_raw_frame)' "$FEED"
grep -q 'camera_convert_raw_to_rgb565(s_raw_frame' "$FEED"
grep -q 'camera_prepare_raw_capture()' "$FEED"
grep -q 'camera_motion_is_enabled()' "$FEED"
grep -q 'camera_is_detected()' "$FEED"
grep -A2 'camera_motion_loop();' "$FEED" | grep -q 'ota_activity_is_active()'
grep -q 'analyze_every_nth_frame' "$FEED"
grep -q 'camera_motion_release_grid();' "$MOTION"
grep -q 'camera_raw10_high_byte(raw' "$MOTION"
grep -q 'camera_presence/state' "$SENSOR"
grep -q 'camera_presence_remove_discovery' "$SENSOR"
grep -q '"Camera Presence"' "$SENSOR"
grep -q '"presence"' "$SENSOR"
grep -q 'camera_presence' "$SENSOR"
grep -q 'camera_motion_enabled' src/app/config_manager.h
grep -q 'KEY_CAMERA_MOTION_ENABLED' src/app/config_manager.cpp
grep -q 'KEY_CAMERA_MOTION_FPS_LEGACY' src/app/config_manager.cpp
grep -q 'preferences.isKey(KEY_CAMERA_MOTION_ANALYZE_EVERY)' src/app/config_manager.cpp
grep -q 'motion_enabled' "$COMPONENT"
grep -q 'kCameraConfigMaxBodyBytes = 512' "$COMPONENT"
grep -q 'motion_sensitivity_min' "$COMPONENT"
grep -q 'motion_analyze_every_nth_frame' "$COMPONENT"
grep -q 'camera-motion-enabled' src/app/web/camera-motion.fragment.html
grep -q 'camera-motion-sensitivity' src/app/web/camera-motion.fragment.html
grep -q 'camera-presence-hold-seconds' src/app/web/camera-motion.fragment.html
grep -q 'fragment_id = "camera-motion"' "$COMPONENT"
grep -q 'camera-motion-enabled' src/app/web/camera-motion.fragment.html
grep -q 'motion_enabled: enabled.checked' src/app/web/portal_camera_motion.js
grep -q 'motion_analyze_every_nth_frame' src/app/web/portal_camera_motion.js
grep -q 'Sensor Data fragment' src/app/web/camera-motion.fragment.html
! grep -q 'camera-motion-status' src/app/web/camera-motion.fragment.html
! grep -q 'camera-last-motion' src/app/web/camera-motion.fragment.html
grep -q 'portal_camera_motion.js' src/app/web/portal_camera.js.bundle
! grep -q 'Motion sample: tiles=' "$MOTION"
! grep -q 'Shared RAW10 capture=' "$FEED"
grep -q '#include "sensors/camera_presence_sensor.cpp"' src/app/sensors.cpp

echo "PASS: camera motion sensing wiring"