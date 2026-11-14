#include <assert.h>
#include <string.h>

#include "SD.h"
#include "fs_health.h"
#include "sd_storage.h"

SPIClass SPI;
SDFS SD;

static bool spi_ok = false;
static bool sd_ok = false;
static uint8_t card_type = CARD_NONE;
static int spi_begin_count = 0;
static int spi_end_count = 0;
static int sd_begin_count = 0;
static int sd_end_count = 0;
#if TEST_SHARED_SD
static SPIClass shared_spi;
static bool prepare_ok = false;
static int prepare_count = 0;
static int failed_count = 0;
SPIClass& test_shared_sd_spi() { return shared_spi; }
bool test_prepare_sd() { ++prepare_count; return prepare_ok; }
void test_sd_mount_failed() { ++failed_count; }
#endif

bool SPIClass::begin(int sck, int miso, int mosi, int cs) {
    assert(sck == 43 && miso == 39 && mosi == 44 && cs == 42);
    ++spi_begin_count;
    return spi_ok;
}
void SPIClass::end() { ++spi_end_count; }
bool SDFS::begin(int cs, SPIClass& spi, uint32_t frequency, const char* mountpoint) {
    assert(cs == 42 && frequency == 25000000);
#if TEST_SHARED_SD
    assert(&spi == &shared_spi);
#else
    assert(&spi == &SPI);
#endif
    assert(strcmp(mountpoint, "/sdcard") == 0);
    ++sd_begin_count;
    return sd_ok;
}
void SDFS::end() { ++sd_end_count; }
uint8_t SDFS::cardType() { return card_type; }
uint64_t SDFS::cardSize() { return 1024 * 1024; }
uint64_t SDFS::usedBytes() { return 1024; }
uint64_t SDFS::totalBytes() { return 1024 * 1024; }

int main() {
#if TEST_SHARED_SD
    assert(!sd_storage_mount());
    assert(prepare_count == 1 && failed_count == 1 && sd_begin_count == 0);
    prepare_ok = true;
    assert(!sd_storage_mount());
    assert(prepare_count == 2 && failed_count == 2 && spi_end_count == 0);
    sd_ok = true;
    assert(!sd_storage_mount());
    assert(sd_end_count == 1 && failed_count == 3 && spi_end_count == 0);
    card_type = CARD_SDHC;
    assert(sd_storage_mount());
    assert(sd_storage_is_mounted());
    assert(sd_storage_mount());
    assert(prepare_count == 4 && sd_begin_count == 3 && spi_begin_count == 0 && spi_end_count == 0);
#else
    assert(!sd_storage_mount());
    assert(sd_begin_count == 0 && spi_end_count == 1);
    spi_ok = true;
    assert(!sd_storage_mount());
    assert(sd_begin_count == 1 && spi_end_count == 2);
    sd_ok = true;
    assert(!sd_storage_mount());
    assert(sd_end_count == 1 && spi_end_count == 3);
    card_type = CARD_SDHC;
    assert(sd_storage_mount());
    assert(sd_storage_is_mounted());
    assert(sd_storage_mount());
    assert(spi_begin_count == 4 && sd_begin_count == 3);
#endif
    FSHealthStats stats{};
    fs_health_get(&stats);
    assert(stats.backend == FS_BACKEND_SD_SPI && stats.card_type == FS_CARD_TYPE_SDHC);
    return 0;
}