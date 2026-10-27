#pragma once

#include <stdint.h>
#include "SPI.h"

enum { CARD_NONE, CARD_SD, CARD_SDHC };

class SDFS {
public:
    bool begin(int cs, SPIClass& spi, uint32_t frequency, const char* mountpoint);
    void end();
    uint8_t cardType();
    uint64_t cardSize();
    uint64_t usedBytes();
    uint64_t totalBytes();
};

extern SDFS SD;