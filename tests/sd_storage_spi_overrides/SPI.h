#pragma once

class SPIClass {
public:
    bool begin(int sck, int miso, int mosi, int cs);
    void end();
};

extern SPIClass SPI;