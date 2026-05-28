// GPIO pin assignments for the 3-LED test rig on ESP32-S3 Super Mini.
// Each pin drives one red LED through a 100Ω series resistor to GND.
#pragma once

#define LED1_PIN 8
#define LED2_PIN 9
#define LED3_PIN 10

// Dedicated LEDC channels for the rig's three PWM LED outputs.
#define LED1_PWM_CHANNEL 0
#define LED2_PWM_CHANNEL 1
#define LED3_PWM_CHANNEL 2

// Compile-time simulation mode. Available modes are defined in led-test-rig.ino:
// LED_TEST_RIG_MODE_BINARY_SIMULTANEOUS, LED_TEST_RIG_MODE_BINARY_TRAVEL,
// LED_TEST_RIG_MODE_VERTICAL_SIM, LED_TEST_RIG_MODE_OM1,
// LED_TEST_RIG_MODE_BAD_CAMERA, and LED_TEST_RIG_MODE_M6.
#ifndef LED_TEST_RIG_MODE
#define LED_TEST_RIG_MODE LED_TEST_RIG_MODE_VERTICAL_SIM
#endif
