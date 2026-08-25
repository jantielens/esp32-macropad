#pragma once

#include "board_config.h"

#include <stddef.h>
#include <stdint.h>

#define CAMERA_JPEG_QUALITY_DEFAULT 60
#define CAMERA_OUTPUT_WIDTH_DEFAULT 640
#define CAMERA_OUTPUT_HEIGHT_DEFAULT 360
#define CAMERA_EXPOSURE_LINES_DEFAULT 512
#define CAMERA_WHITE_BALANCE_Q8_DEFAULT 256
#define CAMERA_FEED_TARGET_FPS_DEFAULT 4
#define CAMERA_FEED_TARGET_FPS_MIN 1
#define CAMERA_FEED_TARGET_FPS_MAX 5

enum CameraRotation : uint16_t {
	CAMERA_ROTATION_0 = 0,
	CAMERA_ROTATION_90 = 90,
	CAMERA_ROTATION_180 = 180,
	CAMERA_ROTATION_270 = 270,
};

#define CAMERA_ROTATION_DEFAULT CAMERA_ROTATION_0

struct CameraRawFrame {
	uint8_t* data;
	size_t size;
	uint16_t width;
	uint16_t height;
};

struct CameraJpegFrame {
	uint8_t* data;
	size_t size;
	uint16_t width;
	uint16_t height;
};

struct CameraRgb565Frame {
	uint16_t* data;
	size_t size;
	uint16_t width;
	uint16_t height;
};

struct CameraCaptureTiming {
	uint32_t raw_capture_us;
	uint32_t rgb565_convert_us;
	uint32_t jpeg_encode_us;
	uint32_t total_us;
};

enum CameraCaptureSaveTo : uint8_t {
	CAMERA_CAPTURE_SAVE_LATEST,
	CAMERA_CAPTURE_SAVE_ROLL,
	CAMERA_CAPTURE_SAVE_BOTH,
};

struct CameraOutputDimensions {
	uint16_t width;
	uint16_t height;
};

struct CameraCaptureSettings {
	uint8_t jpeg_quality;
	uint8_t feed_target_fps;
	CameraRotation rotation;
	uint16_t output_width;
	uint16_t output_height;
	uint16_t exposure_lines;
	uint16_t white_balance_red_q8;
	uint16_t white_balance_blue_q8;
};

struct CameraCapabilities {
	const char* raw_pixel_format;
	uint16_t raw_width;
	uint16_t raw_height;
	uint8_t jpeg_quality_min;
	uint8_t jpeg_quality_max;
	uint8_t feed_target_fps_min;
	uint8_t feed_target_fps_max;
	uint16_t exposure_lines_min;
	uint16_t exposure_lines_max;
	float exposure_line_time_us;
	uint16_t white_balance_q8_min;
	uint16_t white_balance_q8_max;
	const CameraOutputDimensions* output_dimensions;
	size_t output_dimensions_count;
};

// Initializes the board camera probe after the shared I2C bus is ready.
void camera_init();

// Returns whether the configured camera sensor acknowledged its SCCB address.
bool camera_is_detected();

// Returns the verified raw sensor mode and supported encoded JPEG settings.
const CameraCapabilities* camera_get_capabilities();

// Returns the active JPEG output settings.
CameraCaptureSettings camera_get_capture_settings();

// Updates JPEG encoding settings when they are within the advertised bounds.
bool camera_set_capture_settings(const CameraCaptureSettings& settings);

// Captures one RAW10 frame into caller-owned PSRAM. Must run on the main loop.
bool camera_capture_raw(CameraRawFrame* frame);

// Frees a frame returned by camera_capture_raw().
void camera_release_raw(CameraRawFrame* frame);

// Captures one color JPEG frame into caller-owned PSRAM. Must run on the main loop.
bool camera_capture_jpeg(CameraJpegFrame* frame);

// Frees a frame returned by camera_capture_jpeg().
void camera_release_jpeg(CameraJpegFrame* frame);

// Captures RGB565 pixels into caller-owned memory and optionally encodes the
// same pixels as JPEG. rgb565->data must hold output_width * output_height
// pixels. Must run on the main loop.
bool camera_capture_rgb565(CameraRgb565Frame* rgb565, CameraJpegFrame* jpeg = nullptr,
						   CameraCaptureTiming* timing = nullptr);

// Captures one JPEG and stores the requested latest image, camera-roll image,
// or both. Must run on the main loop.
bool camera_capture_save(CameraCaptureSaveTo save_to);