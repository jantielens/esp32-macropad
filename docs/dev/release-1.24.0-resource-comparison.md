---
title: Release 1.24.0 Resource Comparison
description: Reproducible ESP32-P4 build-size and DataStream layout comparison for release 1.24.0.
author: ESP32 Macropad Team
ms.date: 2026-07-30
ms.topic: reference
---

## Scope

This record compares immutable `main` and `release/1.24.0` source revisions on
`jc1060p470c`. It also measures a temporary release build with Home Assistant
history disabled to identify target-ABI static cost. The temporary override,
build directories, worktrees, and copied artifacts are not part of the firmware
repository.

Runtime heap measurements are pending because no physical board serial port was
available during this run. No compaction decision is made without all nine
required cold-boot readings.

## Build Environment

| Item | Value |
|------|-------|
| Arduino CLI | 1.5.1 (`01f3d4f2b`, 2026-06-05) |
| ESP32 Arduino core | 3.3.7 |
| Board | `jc1060p470c` |
| FQBN | `esp32:esp32:esp32p4:FlashSize=16M,PSRAM=enabled,PartitionScheme=ota_6mb_16MB,USBMode=hwcdc,CDCOnBoot=cdc` |
| Compiler cache | Disabled (`USE_CCACHE=0`) |
| Python | 3.14.4 |
| Node.js | 24.16.0 |
| PNG asset generator | `python3 tools/png2lvgl_assets.py assets/png src/app/png_assets.cpp src/app/png_assets.h --prefix img_` |
| Web asset generator | `tools/minify-web-assets.sh esp32-macropad "ESP32 Macropad"` |
| `rjsmin` | 1.2.0 |
| `csscompressor` | 0.9.5 |

Each source revision was checked out in a detached temporary worktree. The
build command was:

```bash
ARDUINO_CLI=/home/jan/dev/esp32-macropad/bin/arduino-cli USE_CCACHE=0 ./build.sh jc1060p470c
```

For each clean target build, `build.sh` first runs the listed PNG command for
display boards. It runs the listed web generator once for `jc1060p470c` using
the board's resolved product name. The generated files remain in the temporary
worktree and are not copied to the implementation worktree.

## Build Results

| Variant | Commit | Flash bytes | Flash percent | Global bytes | Global percent |
|---------|--------|------------:|--------------:|-------------:|---------------:|
| Main | `1a1022fbd51dd0b72a3a4e3c51d52a8cefb49747` | 3,129,588 | 47% | 82,356 | 25% |
| Release HA-on | `ed487298e38e5271b0602ba863e2af53181764fb` | 3,178,554 | 48% | 90,724 | 27% |
| Release HA-off | `ed487298e38e5271b0602ba863e2af53181764fb` with `HAS_HA_HISTORY=false` | 3,164,512 | 48% | 85,844 | 26% |

Release HA-on versus main increases firmware flash by 48,966 bytes (1.56%) and
global memory by 8,368 bytes (10.16%). Release HA-on versus HA-off increases
firmware flash by 14,042 bytes (0.4437% relative to HA-off) and global memory
by 4,880 bytes.

## Target-ABI DataStream Layout

The ESP32-P4 ELF is a 32-bit RISC-V executable. Measurements use
`riscv32-esp-elf/bin/nm -S --size-sort` on each `app.ino.elf`; host ABI sizes are
not used. `DATA_STREAM_MAX_STREAMS` is 64.

| Variant | ELF symbol | `g_streams` bytes | Derived `sizeof(DataStream)` |
|---------|------------|------------------:|-----------------------------:|
| Main | `_ZL9g_streams` | 14,336 | 224 |
| Release HA-on | `_ZL9g_streams` | 20,480 | 320 |
| Release HA-off | `_ZL9g_streams` | 15,872 | 248 |

The release HA-on versus HA-off `g_streams` growth is 4,608 bytes. This is
above the 4,096-byte static-layout threshold. The HA-off build retains the
display data-stream module while compiling out Home Assistant history fields.

`HAS_HA_HISTORY` defaults to `HAS_DISPLAY && HAS_MQTT && HAS_PSRAM`; therefore,
non-PSRAM builds do not contain these history-only fields. The clean
`esp32c3-withsensors` build is a compile guard only and is not a DataStream
measurement.

## Pending Runtime Measurements

The runtime comparison requires the same physical `jc1060p470c`, exported
configuration, filesystem, Wi-Fi and Home Assistant availability, active
screen, dynamic sparkline configuration, serial settings, and power source for
all variants.

For each of main, release HA-on, and release HA-off:

1. Export the device configuration once and record its SHA-256.
2. Flash only the application with `./upload.sh --app-only jc1060p470c <port>`.
3. Remove power for at least 10 seconds before booting.
4. Capture `Main: Setup complete` and the first `Heartbeat` with uptime from 60
   through 69 seconds.
5. Record the heartbeat `int=` value from
   `heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`.
6. Discard and repeat any run with a reboot, disconnect, configuration write,
   portal request, or missing checkpoint.

Three valid readings per variant are required before calculating medians.

## Compaction Gate

| Condition | Status |
|-----------|--------|
| HA-on `g_streams` growth is at least 4,096 bytes | Pass: 4,608 bytes |
| HA-on median internal heap is at least 3,072 bytes lower than HA-off | Pending three cold boots per variant |
| HA-on median internal heap is below 64 KiB, or the attributable loss is at least 5% of HA-off median | Pending three cold boots per variant |

**Verdict: Pending runtime measurements.** No DataStream storage change is
authorized from the build-only evidence.

## Limitations

Firmware size is deterministic for this toolchain and input revision, while
free internal heap is sensitive to configuration and network timing. The
build-size delta and static layout delta do not establish a runtime heap cost;
that conclusion requires the retained raw cold-boot readings and their medians.
