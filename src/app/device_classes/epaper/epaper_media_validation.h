#pragma once

#ifndef EPAPER_MEDIA_VALIDATION_H
#define EPAPER_MEDIA_VALIDATION_H

#include <stddef.h>
#include <stdint.h>

enum class EpaperMediaValidation : uint8_t {
		Valid,
		Malformed,
		Unsupported,
		WrongGeometry,
		PayloadCrcMismatch,
		TrailingBytes,
};

EpaperMediaValidation epaper_validate_g16p(const uint8_t* data, size_t len,
		uint16_t panel_width, uint16_t panel_height);
EpaperMediaValidation epaper_validate_jpeg(const uint8_t* data, size_t len,
		uint16_t panel_width, uint16_t panel_height);
EpaperMediaValidation epaper_validate_g16z_completion(int status, int done_status,
		size_t consumed_input, size_t input_length,
		size_t produced_output, size_t expected_output);

#endif // EPAPER_MEDIA_VALIDATION_H
