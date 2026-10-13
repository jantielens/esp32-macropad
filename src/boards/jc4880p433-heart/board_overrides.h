#ifndef BOARD_OVERRIDES_JC4880P433_HEART_H
#define BOARD_OVERRIDES_JC4880P433_HEART_H

#include "../jc4880p433-sd/board_overrides.h"

#define IS_HEART_MONITOR true
// MAX30101 secondary-bus data pin, separate from touch and audio.
#define HEART_I2C_SDA 52
// MAX30101 secondary-bus clock pin.
#define HEART_I2C_SCL 51
// MAX30101 secondary-bus clock frequency in Hz.
#define HEART_I2C_FREQUENCY 400000

#endif