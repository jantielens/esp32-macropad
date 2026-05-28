<!-- markdownlint-disable-file MD041 -->
# Shutter Tester

The Shutter Tester device class turns the ESP32 Macropad firmware into a precision instrument for measuring camera shutter speeds. An ADC sensor array reads the light pulse from a film-plane LED bar; the firmware computes the open time at each sensor position, derives curtain timing for focal-plane shutters, and presents results in a touch UI and in the web portal.

## At a glance

- **Reference board**: `jc4880p433-shutter` (4.8" 480×800 MIPI-DSI, GT911 touch, ESP32-P4, 16 MB flash, 32 MB PSRAM, SD-MMC storage)
- **Sensors**: 1, 3, or 4 photodiodes on dedicated ADC pins, selected by preset
- **Sample rate**: ~27.7 kHz continuous, triggered captures with pre/post buffers
- **Storage**: SD card (sessions persist across reboots; large captures stream to flash)
- **Connectivity**: Web portal session browser, MQTT, BLE HID

## Build

```bash
./build.sh jc4880p433-shutter
./upload.sh jc4880p433-shutter
```

All shutter-specific code is gated by `IS_SHUTTER_TESTER`. Other boards in the same firmware build never link the shutter modules.

## Documentation

- [user-guide.md](user-guide.md) — End-user guide: setup, calibration, running test sessions, interpreting results
- [technical-details.md](technical-details.md) — Capture pipeline, presets, geometry projection, verdict thresholds, ADC engine
- [../README.md](../README.md) — All device classes

## Dev tools

The development tooling for the shutter tester lives in [tools/shutter-tester/](../../../tools/shutter-tester/):

- `led-test-rig/` — Independent ESP32-S3 Super-Mini firmware that drives an LED bar with known pulse widths, used to validate sensor response
- `monitor_meas.sh` — Serial monitor that filters `[MEAS]` log lines into a timestamped CSV file
- `portal-dev-server.py` — Local Python web server for live-editing the shutter portal fragments without flashing the device
