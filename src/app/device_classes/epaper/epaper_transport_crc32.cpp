#include "epaper_transport_crc32.h"

uint32_t epaper_transport_crc32(const uint8_t* data, size_t len) {
		uint32_t crc = 0xFFFFFFFFU;
		for (size_t index = 0; index < len; ++index) {
				crc ^= data[index];
				for (uint8_t bit = 0; bit < 8; ++bit) {
						crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
				}
		}
		return crc ^ 0xFFFFFFFFU;
}
