#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t epaper_transport_crc32(const uint8_t* data, size_t len);
