#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gt911_config {
constexpr uint16_t start_register = 0x8047;
constexpr uint16_t fresh_register = 0x8100;
constexpr size_t data_size = 0x80FF - start_register;
constexpr size_t size = data_size + 1;
constexpr size_t filter_offset = 0x8050 - start_register;
constexpr uint8_t initial_version = 0x41;

inline uint8_t checksum(const uint8_t* config) {
    uint8_t sum = 0;
    for (size_t index = 0; index < data_size; ++index) sum += config[index];
    return static_cast<uint8_t>(0 - sum);
}

inline void set_filter(uint8_t* config, uint8_t filter, bool reset_version = false) {
    if (reset_version) config[0] = 0;
    config[filter_offset] = filter;
    config[data_size] = checksum(config);
}
}