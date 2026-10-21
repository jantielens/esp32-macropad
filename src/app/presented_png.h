#pragma once

#include "presented_frame.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

namespace presented_png {

inline uint32_t crc32(const uint8_t* data, size_t length) {
		uint32_t crc = 0xFFFFFFFF;
		for (size_t index = 0; index < length; ++index) {
				crc ^= data[index];
				for (uint8_t bit = 0; bit < 8; ++bit)
						crc = (crc >> 1) ^ (0xEDB88320 & (0U - (crc & 1)));
		}
		return ~crc;
}

inline bool encoded_size(uint16_t width, uint16_t height, uint8_t bitDepth,
		size_t* rawBytes, size_t* pngBytes) {
		if (!width || !height || !pngBytes || (bitDepth != 1 && bitDepth != 4)) return false;
		const size_t rowBytes = presented_frame_row_bytes(width, bitDepth);
		const size_t filteredRowBytes = rowBytes + 1;
		if (filteredRowBytes > SIZE_MAX / height) return false;
		const size_t filteredBytes = filteredRowBytes * height;
		const size_t blocks = filteredBytes / 65535 + (filteredBytes % 65535 != 0);
		if (filteredBytes > SIZE_MAX - 63) return false;
		if (blocks > (SIZE_MAX - filteredBytes - 63) / 5) return false;
		const size_t idatBytes = filteredBytes + blocks * 5 + 6;
		if (idatBytes > UINT32_MAX) return false;
		if (rawBytes) *rawBytes = rowBytes * height;
		*pngBytes = 63 + filteredBytes + blocks * 5;
		return true;
}

inline uint32_t adler32(const uint8_t* pixels, size_t rowBytes, uint16_t height) {
		uint32_t a = 1;
		uint32_t b = 0;
		size_t run = 0;
		auto addByte = [&](uint8_t value) {
				a += value;
				b += a;
				if (++run == 5552) {
						a %= 65521;
						b %= 65521;
						run = 0;
				}
		};
		for (uint16_t row = 0; row < height; ++row) {
				addByte(0);
				for (size_t column = 0; column < rowBytes; ++column)
						addByte(pixels[size_t(row) * rowBytes + column]);
		}
		a %= 65521;
		b %= 65521;
		return (b << 16) | a;
}

inline void write_uint32(uint8_t* output, uint32_t value) {
		output[0] = uint8_t(value >> 24);
		output[1] = uint8_t(value >> 16);
		output[2] = uint8_t(value >> 8);
		output[3] = uint8_t(value);
}

inline bool encode(uint16_t width, uint16_t height, uint8_t bitDepth,
		const uint8_t* pixels, size_t pixelBytes, uint8_t* output,
		size_t outputCapacity, size_t* outputSize) {
		size_t rawBytes = 0, totalBytes = 0;
		if (!pixels || !output || !outputSize ||
				!encoded_size(width, height, bitDepth, &rawBytes, &totalBytes) ||
				pixelBytes < rawBytes || outputCapacity < totalBytes) return false;

		const size_t rowBytes = presented_frame_row_bytes(width, bitDepth);
		const size_t filteredBytes = (rowBytes + 1) * height;
		const size_t blockCount = (filteredBytes + 65534) / 65535;
		const size_t idatDataBytes = filteredBytes + blockCount * 5 + 6;
		size_t offset = 0;
		static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
		memcpy(output + offset, signature, sizeof(signature));
		offset += sizeof(signature);

		write_uint32(output + offset, 13);
		offset += 4;
		const size_t ihdrTypeOffset = offset;
		memcpy(output + offset, "IHDR", 4);
		offset += 4;
		write_uint32(output + offset, width);
		write_uint32(output + offset + 4, height);
		offset += 8;
		output[offset++] = bitDepth;
		output[offset++] = 0;
		output[offset++] = 0;
		output[offset++] = 0;
		output[offset++] = 0;
		write_uint32(output + offset, crc32(output + ihdrTypeOffset, 17));
		offset += 4;

		write_uint32(output + offset, uint32_t(idatDataBytes));
		offset += 4;
		const size_t idatTypeOffset = offset;
		memcpy(output + offset, "IDAT", 4);
		offset += 4;
		output[offset++] = 0x78;
		output[offset++] = 0x01;

		size_t streamRemaining = filteredBytes;
		size_t row = 0;
		size_t rowOffset = 0;
		bool filterBytePending = true;
		while (streamRemaining) {
				const size_t blockBytes = streamRemaining > 65535 ? 65535 : streamRemaining;
				const bool finalBlock = blockBytes == streamRemaining;
				output[offset++] = finalBlock ? 1 : 0;
				output[offset++] = uint8_t(blockBytes);
				output[offset++] = uint8_t(blockBytes >> 8);
				const uint16_t inverseLength = uint16_t(~uint16_t(blockBytes));
				output[offset++] = uint8_t(inverseLength);
				output[offset++] = uint8_t(inverseLength >> 8);

				size_t blockRemaining = blockBytes;
				while (blockRemaining) {
						if (filterBytePending) {
								output[offset++] = 0;
								filterBytePending = false;
								--blockRemaining;
								continue;
						}
						const size_t copyBytes = rowBytes - rowOffset < blockRemaining ?
								rowBytes - rowOffset : blockRemaining;
						memcpy(output + offset, pixels + row * rowBytes + rowOffset, copyBytes);
						offset += copyBytes;
						rowOffset += copyBytes;
						blockRemaining -= copyBytes;
						if (rowOffset == rowBytes) {
								++row;
								rowOffset = 0;
								filterBytePending = true;
						}
				}
				streamRemaining -= blockBytes;
		}

		write_uint32(output + offset, adler32(pixels, rowBytes, height));
		offset += 4;
		write_uint32(output + offset, crc32(output + idatTypeOffset, 4 + idatDataBytes));
		offset += 4;

		write_uint32(output + offset, 0);
		offset += 4;
		const size_t iendTypeOffset = offset;
		memcpy(output + offset, "IEND", 4);
		offset += 4;
		write_uint32(output + offset, crc32(output + iendTypeOffset, 4));
		offset += 4;

		*outputSize = offset;
		return offset == totalBytes;
}

} // namespace presented_png
