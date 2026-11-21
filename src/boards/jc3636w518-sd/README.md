---
title: JC3636W518 SD-primary Macropad
description: MicroSD wiring and persistent-storage requirements for the round JC3636W518.
---

Target `jc3636w518-sd` uses the
[JC3636W518 hardware](../jc3636w518/README.md). Its display limits, Extension
capacities, and recovery procedure remain applicable. SD targets select HID
support independently of the internal-storage target.

## Choose a HID variant

| Target                 | Compiled HID support |
| ---------------------- | -------------------- |
| `jc3636w518-sd`         | None                 |
| `jc3636w518-sd-usb`     | USB                  |
| `jc3636w518-sd-ble`     | BLE                  |

The unsuffixed SD target no longer includes USB HID. Select the USB variant
to retain that capability when updating. All three targets share the same
partition layout and SD wiring; switching does not migrate or copy files.

HID starts Off. On HID variants, select a supported transport under
**Connectivity > Keyboard, Mouse & Gamepad**, save, and reboot.
Follow the [keyboard, mouse, and gamepad guide](../../../docs/user/integrations/keyboard-mouse-gamepad.md)
for pairing and host connection requirements. All SD variants use UART0
for diagnostics; they do not expose a runtime USB serial console.

The BLE variant uses the core backend, not the NimBLE-Arduino telemetry backend.
Combined Wi-Fi, display, SD, and HID workloads still need physical validation.
Alarms, MCP, audio, and remote image fetching remain disabled.

## HID memory measurements

Measurements taken on 2026-11-21 used the same ESP32-S3 hardware with 16 MiB
flash, 8 MiB PSRAM, 4 MiB OTA app slots, and the 128-byte PSRAM allocation
threshold. The firmware reported version 1.50.0 while testing the unreleased
1.51.0 changes. Wi-Fi was active, MQTT was disabled, and the same pad was
displayed. Samples came from separate boots at different uptimes; they are not
an isolated allocation benchmark or a workload acceptance test.

### Compiled footprint

All sizes below are bytes. Application size is the OTA application binary,
not the merged full-flash image. Static DRAM includes data, BSS, and no-init
sections; IRAM includes executable sections, vectors, and alignment. Neither
column includes runtime heap allocations or task stacks.

| Compiled HID             | Application | Static DRAM | IRAM    |
| ------------------------ | ----------- | ----------- | ------- |
| None                     | 2,955,824   | 76,880      | 147,712 |
| USB                      | 3,062,352   | 112,728     | 147,712 |
| BLE                      | 3,237,696   | 82,980      | 169,472 |
| USB + BLE (withdrawn)    | 3,309,520   | 115,716     | 169,472 |

Relative to no HID, USB adds 106,528 application bytes and 35,848 static DRAM
bytes. BLE adds 281,872 application bytes, 6,100 static DRAM bytes, and 21,760
IRAM bytes. Compiled support consumes memory even when the runtime transport
is Off.

### Runtime snapshots

With HID Off, `/api/info` reported these heap values in bytes:

| Compiled HID | Heap size | Free heap |
| ------------ | --------- | --------- |
| None         | 255,664   | 127,800   |
| BLE          | 226,180   | 98,940    |

BLE reduced the reported heap pool by 29,484 bytes. Comparing only a
"bytes used after boot" reading would conceal that smaller pool. Use
`/api/health` internal free, minimum free, and largest free block to assess
remaining headroom. DMA-capable internal memory is a subset, not an additional
pool to add to internal free.

The following `/api/health` values are bytes. Minimum free is the lifetime
low-water mark since boot, not the free memory at the sample time.

| Compiled HID          | Selected | Uptime (s) | Internal free | Internal min | Largest block | DMA free | DMA min |
| --------------------- | -------- | ---------- | ------------- | ------------ | ------------- | -------- | ------- |
| BLE                   | BLE      | 50         | 21,516        | 21,460       | 13,300        | 13,852   | 13,796  |
| USB                   | Off      | 38         | 89,676        | 76,192       | 49,140        | 82,012   | 68,528  |
| USB                   | USB      | 34         | 84,284        | 66,736       | 40,948        | 76,620   | 59,072  |
| USB + BLE (withdrawn) | Off      | 30         | 58,876        | 47,588       | 31,732        | 51,212   | 39,924  |
| USB + BLE (withdrawn) | USB      | 59         | 58,500        | 41,588       | 31,732        | 50,836   | 33,924  |
| USB + BLE (withdrawn) | BLE      | 21         | 61,672        | 13,364       | 19,444        | 54,008   | 5,700   |

| Compiled HID          | Selected | PSRAM free | PSRAM min | Internal fragmentation |
| --------------------- | -------- | ---------- | --------- | ---------------------- |
| BLE                   | BLE      | 7,708,544  | 7,701,912 | 38%                    |
| USB                   | Off      | 7,708,908  | 7,635,040 | 45%                    |
| USB                   | USB      | 7,708,104  | 7,633,496 | 51%                    |
| USB + BLE (withdrawn) | Off      | 7,708,068  | 7,629,968 | 46%                    |
| USB + BLE (withdrawn) | USB      | 7,706,204  | 7,630,616 | 45%                    |
| USB + BLE (withdrawn) | BLE      | 7,709,024  | 7,629,832 | 68%                    |

USB-only reached `keyboard_status: ready`. BLE-only initialized and entered
pairing, but remained disconnected, unbonded, and unencrypted; a connected
host was not validated. Its roughly 21 KiB internal free and 13 KiB largest
block leave limited headroom despite abundant free PSRAM.

The combined build reached USB ready, but selecting BLE reported
`ble_state: disabled` and `keyboard_status: disconnected`. Current telemetry
labels uninitialized BLE as disabled even after an initialization error.
The 13,364-byte internal minimum and 5,700-byte DMA minimum suggest startup
memory pressure, but the failure cause was not confirmed by serial logs.
The later recovery in free memory does not demonstrate working BLE.

The combined `jc3636w518-sd-hid` target is withdrawn; its measurements remain
here only to explain the three-variant choice. BLE-only still requires
connected-host and Wi-Fi/display/SD/input workload testing before claiming
reliable operation under load.

## Storage differences

Persistent files use the onboard MicroSD slot. Device settings stored in NVS
remain in internal flash.

> [!IMPORTANT]
> Insert a readable FAT32 card before boot. Startup stops if it cannot mount
> the card, without automatic formatting or fallback to internal file storage.
> Check the card and reboot.

Switching from the base target does not copy internal files to SD. Back up
before switching and restore the files to the card. Power down before removing it.

## Card connections

The slot uses one-bit SDMMC at up to 20 MHz, not SPI.

| Signal   | GPIO   |
| -------- | ------ |
| D0       | 2      |
| Clock    | 3      |
| CMD      | 4      |

Use the variant's full image/partition table for first installation or layout
changes. OTA remains supported. The base board's older-layout migration warning
also applies.

## References

* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)
