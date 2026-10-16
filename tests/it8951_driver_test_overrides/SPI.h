#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

constexpr int HSPI = 1, MSBFIRST = 1, SPI_MODE0 = 0;
constexpr int LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2;
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
void delay(unsigned long milliseconds);
void vTaskDelay(TickType_t ticks);
void yield();
void it8951_test_spi_begin(int clock, int miso, int mosi, int cs);
void it8951_test_spi_transaction(uint32_t frequency);
uint8_t it8951_test_spi_byte(uint8_t value);
void it8951_test_spi_data(const uint8_t* data, size_t length);

struct SPISettings {
    uint32_t frequency;
    SPISettings(uint32_t hz, int, int) : frequency(hz) {}
};
class SPIClass {
public:
    explicit SPIClass(int) {}
    void begin(int clock, int miso, int mosi, int cs) { it8951_test_spi_begin(clock, miso, mosi, cs); }
    void beginTransaction(const SPISettings& settings) { it8951_test_spi_transaction(settings.frequency); }
    void endTransaction() {}
    uint8_t transfer(uint8_t value) { return it8951_test_spi_byte(value); }
    uint16_t transfer16(uint16_t value) {
        const uint16_t high = transfer(value >> 8);
        return (high << 8) | transfer(value & 0xff);
    }
    void transfer(uint8_t* data, size_t length) { it8951_test_spi_data(data, length); }
    void writeBytes(const uint8_t* data, size_t length) { it8951_test_spi_data(data, length); }
};
