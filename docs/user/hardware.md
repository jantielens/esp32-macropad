---
title: Find your hardware and firmware
description: Choose the exact board target and find its connections, recovery procedure, and firmware-specific limits.
---

Choose your physical board first, then the firmware variant for your task.
Each board reference covers its connections, recovery, and exceptions to
shared user instructions. A variant links directly to its shared hardware
reference and documents only its differences.

Use the exact target in the
[firmware installer](https://jantielens.github.io/esp32-macropad/).
Similar names, screen sizes, or connectors do not guarantee compatible
firmware. Pin tables record configured connections, not spare GPIOs or a
complete circuit schematic. Verify the board revision before wiring accessories.

## Touchscreen Macropads

| Board or firmware variant                 | Target and hardware reference                                         |
| ----------------------------------------- | --------------------------------------------------------------------- |
| Cheap Yellow Display, one-USB v2          | [`cyd-v2`](../../src/boards/cyd-v2/README.md)                         |
| ESP32-4848S040, square RGB display        | [`esp32-4848S040`](../../src/boards/esp32-4848S040/README.md)         |
| Guition JC3248W535                        | [`jc3248w535`](../../src/boards/jc3248w535/README.md)                 |
| Guition JC3636W518, round display         | [`jc3636w518`](../../src/boards/jc3636w518/README.md)                 |
| JC3636W518 with primary SD storage        | [`jc3636w518-sd`](../../src/boards/jc3636w518-sd/README.md)           |
| Guition JC4827W543C, capacitive C model   | [`jc4827w543c`](../../src/boards/jc4827w543c/README.md)               |
| Guition JC4880P433                        | [`jc4880p433`](../../src/boards/jc4880p433/README.md)                 |
| JC4880P433 with primary SD storage        | [`jc4880p433-sd`](../../src/boards/jc4880p433-sd/README.md)           |
| Guition JC1060P470C                       | [`jc1060p470c`](../../src/boards/jc1060p470c/README.md)               |
| JC1060P470C with primary SD storage       | [`jc1060p470c-sd`](../../src/boards/jc1060p470c-sd/README.md)         |
| Waveshare ESP32-P4-WIFI6-Touch-LCD-4B     | [`esp32-p4-lcd4b`](../../src/boards/esp32-p4-lcd4b/README.md)         |
| M5Stack StopWatch, round AMOLED           | [`m5stack-stopwatch`](../../src/boards/m5stack-stopwatch/README.md)   |

## E-paper

**Frame** firmware is sleep-first: it wakes, fetches a prepared image, updates,
then sleeps. **Interactive** firmware runs pads controlled by touch or physical
buttons. Physical touch hardware is not necessarily enabled in Frame firmware.

| Hardware and use                  | Target and hardware reference                                                               |
| --------------------------------- | ------------------------------------------------------------------------------------------- |
| Inkplate 5 V2, Frame              | [`inkplate5v2-frame`](../../src/boards/inkplate5v2-frame/README.md)                         |
| Inkplate 6FLICK, Frame            | [`inkplate6flick-frame`](../../src/boards/inkplate6flick-frame/README.md)                   |
| Inkplate 6FLICK, Interactive      | [`inkplate6flick-interactive`](../../src/boards/inkplate6flick-interactive/README.md)       |
| reTerminal E1003, Frame           | [`reterminal-e1003-frame`](../../src/boards/reterminal-e1003-frame/README.md)               |
| reTerminal E1003, Interactive     | [`reterminal-e1003-interactive`](../../src/boards/reterminal-e1003-interactive/README.md)   |
| CrowPanel 4.2-inch, Interactive   | [`crowpanel-4.2-interactive`](../../src/boards/crowpanel-4.2-interactive/README.md)         |

## Specialized devices

These targets need the named sensor or service integration. Their board
references explain the required additions and transport/storage differences.

| Task                                | Target and hardware reference                                               |
| ----------------------------------- | --------------------------------------------------------------------------- |
| Coffee Scale with NAU7802 ADC       | [`jc4880p433-nau7802`](../../src/boards/jc4880p433-nau7802/README.md)       |
| Coffee Scale with HX711 amplifier   | [`jc4880p433-hx711`](../../src/boards/jc4880p433-hx711/README.md)           |
| Darkroom Timer and metering         | [`jc4880p433-darkroom`](../../src/boards/jc4880p433-darkroom/README.md)     |
| Shutter Tester photodiode rig       | [`jc4880p433-shutter`](../../src/boards/jc4880p433-shutter/README.md)       |
| LCD4B Azure Voice Assistant         | [`esp32-p4-lcd4b-voice`](../../src/boards/esp32-p4-lcd4b-voice/README.md)   |

## Headless sensor nodes

| Hardware and sensors                                                | Target and hardware reference                                                         |
| ------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| ESP32-C3 SuperMini, synthetic sensor by default                     | [`esp32c3-withsensors`](../../src/boards/esp32c3-withsensors/README.md)               |
| FireBeetle 2 ESP32-C6 v1.2, external AHT10 and battery monitoring   | [`firebeetle2-esp32c6-aht10`](../../src/boards/firebeetle2-esp32c6-aht10/README.md)   |

## Before changing firmware

Back up configuration and stored files before changing targets or partition
layouts. An OTA application image does not install a different partition
table, and switching between internal files and an SD-primary backend does
not migrate data. Check your target's README for update support and any
required full USB flash.

Hardware documentation describes the current firmware configuration, not a
claim that every peripheral or accessory has been physically tested.
The board pages identify known validation limits where they affect use.
