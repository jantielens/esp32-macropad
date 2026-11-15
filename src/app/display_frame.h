#ifndef DISPLAY_FRAME_H
#define DISPLAY_FRAME_H

#include <stdint.h>

enum class DisplayIntent : uint8_t { Background, Interactive };

struct DisplayFrame {
    uint32_t sequence = 0;
    uint32_t request_sequence = 0;
    DisplayIntent intent = DisplayIntent::Background;
    uint64_t requested_us = 0;
    uint64_t ready_us = 0;
};

#endif
