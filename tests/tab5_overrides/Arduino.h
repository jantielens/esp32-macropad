#pragma once

#include <cstddef>
#include <cstdint>

constexpr int OUTPUT = 1;
constexpr int INPUT = 0;
constexpr int HIGH = 1;
constexpr int LOW = 0;

void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
void delay(unsigned long duration);