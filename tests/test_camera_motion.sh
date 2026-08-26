#!/bin/bash
# Camera motion sensing source-wiring guard.
set -e
cd "$(dirname "$0")/.."

MOTION="src/app/camera_motion.cpp"
FEED="src/app/camera_feed.cpp"
SENSOR="src/app/sensors/camera_presence_sensor.cpp"
COMPONENT="src/app/components/camera_component.cpp"

grep -q '#if HAS_CAMERA' "$MOTION"
grep -q 'CAMERA_MOTION_FPS_DEFAULT' src/app/camera_motion.h
grep -q 'CAMERA_PRESENCE_HOLD_SECONDS_DEFAULT' src/app/camera_motion.h
grep -q 'MALLOC_CAP_SPIRAM' "$MOTION"
grep -q 'ota_activity_is_active()' "$MOTION"
grep -q 'camera_capture_raw_reuse(&s_raw_frame)' "$MOTION"
! grep -q 'camera_capture_rgb565' "$MOTION"
! grep -q 'camera_capture_jpeg' "$MOTION"
grep -q 'camera_motion_loop();' "$FEED"
grep -q 'camera_motion_is_enabled()' "$FEED"
grep -q 'camera_motion_release_grid();' "$MOTION"
grep -q 'camera_presence/state' "$SENSOR"
grep -q 'camera_presence_remove_discovery' "$SENSOR"
grep -q '"Camera Presence"' "$SENSOR"
grep -q '"presence"' "$SENSOR"
grep -q 'camera_presence' "$SENSOR"
grep -q 'camera_motion_enabled' src/app/config_manager.h
grep -q 'KEY_CAMERA_MOTION_ENABLED' src/app/config_manager.cpp
grep -q 'motion_enabled' "$COMPONENT"
grep -q 'kCameraConfigMaxBodyBytes = 512' "$COMPONENT"
grep -q 'motion_sensitivity_min' "$COMPONENT"
grep -q 'camera-motion-enabled' src/app/web/camera-motion.fragment.html
grep -q 'camera-motion-sensitivity' src/app/web/camera-motion.fragment.html
grep -q 'camera-presence-hold-seconds' src/app/web/camera-motion.fragment.html
grep -q 'fragment_id = "camera-motion"' "$COMPONENT"
grep -q 'camera-motion-enabled' src/app/web/camera-motion.fragment.html
grep -q 'motion_enabled: enabled.checked' src/app/web/portal_camera_motion.js
grep -q 'Sensor Data fragment' src/app/web/camera-motion.fragment.html
! grep -q 'camera-motion-status' src/app/web/camera-motion.fragment.html
! grep -q 'camera-last-motion' src/app/web/camera-motion.fragment.html
grep -q 'portal_camera_motion.js' src/app/web/portal_camera.js.bundle
grep -q 'Motion sample: tiles=' "$MOTION"
grep -q 'Motion sample: RAW10 capture=' "$MOTION"
grep -q '#include "sensors/camera_presence_sensor.cpp"' src/app/sensors.cpp

echo "PASS: camera motion sensing wiring"