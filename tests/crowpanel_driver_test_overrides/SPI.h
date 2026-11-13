#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

constexpr int FSPI = 0, MSBFIRST = 1, SPI_MODE0 = 0;
constexpr int LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1;
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
void delay(unsigned long milliseconds);
void vTaskDelay(TickType_t ticks);
void crowpanel_test_spi_begin(int clock, int miso, int mosi, int cs);
void crowpanel_test_spi_byte(uint8_t value);
void crowpanel_test_spi_data(const uint8_t* data, size_t length);
void crowpanel_test_spi_end();
void crowpanel_test_spi_begin_transaction();
void crowpanel_test_spi_end_transaction();

struct SPISettings {
    SPISettings(uint32_t, int, int) {}
};
class SPIClass {
public:
    explicit SPIClass(int) {}
    void begin(int clock, int miso, int mosi, int cs) { crowpanel_test_spi_begin(clock, miso, mosi, cs); }
    void end() { crowpanel_test_spi_end(); }
    void beginTransaction(const SPISettings&) { crowpanel_test_spi_begin_transaction(); }
    void endTransaction() { crowpanel_test_spi_end_transaction(); }
    uint8_t transfer(uint8_t value) { crowpanel_test_spi_byte(value); return 0; }
    void writeBytes(const uint8_t* data, size_t length) { crowpanel_test_spi_data(data, length); }
};