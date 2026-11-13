#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

constexpr int HSPI = 1, MSBFIRST = 1, SPI_MODE0 = 0;
constexpr int LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1;
void pinMode(int, int);
void digitalWrite(int, int);
int digitalRead(int);
void delay(unsigned long);
void vTaskDelay(TickType_t);
void sticky_test_spi_begin(int, int, int, int);
void sticky_test_spi_end();
uint8_t sticky_test_spi_byte(uint8_t);
void sticky_test_spi_data(const uint8_t*, size_t);

struct SPISettings {
    uint32_t clock;
    int bitOrder;
    int mode;
    SPISettings(uint32_t clockHz, int order, int spiMode)
        : clock(clockHz), bitOrder(order), mode(spiMode) {}
};
void sticky_test_spi_begin_transaction(const SPISettings&);
void sticky_test_spi_end_transaction();
class SPIClass {
public:
    explicit SPIClass(int) {}
    void begin(int clock, int miso, int mosi, int cs) { sticky_test_spi_begin(clock, miso, mosi, cs); }
    void end() { sticky_test_spi_end(); }
    void beginTransaction(const SPISettings& settings) { sticky_test_spi_begin_transaction(settings); }
    void endTransaction() { sticky_test_spi_end_transaction(); }
    uint8_t transfer(uint8_t value) { return sticky_test_spi_byte(value); }
    void writeBytes(const uint8_t* data, size_t length) { sticky_test_spi_data(data, length); }
};
