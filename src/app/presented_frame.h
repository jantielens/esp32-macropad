#pragma once

#include <stddef.h>
#include <stdint.h>

inline size_t presented_frame_row_bytes(uint16_t width, uint8_t bitDepth) {
		return (size_t(width) * bitDepth + 7) / 8;
}

inline size_t presented_frame_bytes(uint16_t width, uint16_t height, uint8_t bitDepth) {
		return presented_frame_row_bytes(width, bitDepth) * height;
}

inline bool presented_frame_native_coords(uint16_t nativeWidth, uint16_t nativeHeight,
		uint8_t rotation, uint16_t x, uint16_t y, uint16_t* nativeX, uint16_t* nativeY) {
		if (!nativeX || !nativeY || rotation > 3) return false;
		if ((rotation & 1) ? (x >= nativeHeight || y >= nativeWidth) :
				(x >= nativeWidth || y >= nativeHeight)) return false;
		switch (rotation) {
				case 0:
						*nativeX = x;
						*nativeY = y;
						break;
				case 1:
						*nativeX = nativeWidth - 1 - y;
						*nativeY = x;
						break;
				case 2:
						*nativeX = nativeWidth - 1 - x;
						*nativeY = nativeHeight - 1 - y;
						break;
				case 3:
						*nativeX = y;
						*nativeY = nativeHeight - 1 - x;
						break;
		}
		return *nativeX < nativeWidth && *nativeY < nativeHeight;
}

inline void presented_frame_write_gray(uint8_t* pixels, size_t rowBytes, uint16_t x,
		uint16_t y, uint8_t bitDepth, uint8_t gray) {
		uint8_t* row = pixels + size_t(y) * rowBytes;
		if (bitDepth == 1) {
				const uint8_t mask = uint8_t(0x80 >> (x & 7));
				if (gray) row[x / 8] |= mask;
				else row[x / 8] &= uint8_t(~mask);
		} else if (bitDepth == 4) {
				uint8_t& value = row[x / 2];
				if (x & 1) value = uint8_t((value & 0xF0) | (gray & 0x0F));
				else value = uint8_t((value & 0x0F) | ((gray & 0x0F) << 4));
		}
}
