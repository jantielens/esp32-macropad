#pragma once

class SPIClass {
public:
    bool begin(int sck, int miso, int mosi, int cs);
    void end();
};

extern SPIClass SPI;

#if TEST_SHARED_SD
SPIClass& test_shared_sd_spi();
bool test_prepare_sd();
void test_sd_mount_failed();
#define BOARD_SD_SPI test_shared_sd_spi()
#define BOARD_SD_PREPARE() test_prepare_sd()
#define BOARD_SD_MOUNT_FAILED() test_sd_mount_failed()
#endif