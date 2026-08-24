#pragma once

#include "camera.h"

// Selected camera driver entry points. These are invoked only by camera_service
// on the Arduino loop task.
void camera_driver_init();
bool camera_driver_is_detected();
bool camera_driver_capture_raw(CameraRawFrame* frame);
void camera_driver_release_raw(CameraRawFrame* frame);
bool camera_driver_capture_jpeg(CameraJpegFrame* frame, const CameraCaptureSettings& settings);
bool camera_driver_capture_rgb565(CameraRgb565Frame* rgb565, CameraJpegFrame* jpeg,
								  CameraCaptureTiming* timing,
								  const CameraCaptureSettings& settings);
void camera_driver_release_jpeg(CameraJpegFrame* frame);
