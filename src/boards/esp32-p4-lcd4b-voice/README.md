---
title: Waveshare LCD4B Voice Assistant
description: Requirements and differences for the LCD4B Azure Voice Assistant firmware.
---

Target `esp32-p4-lcd4b-voice` uses the
[Waveshare LCD4B hardware](../esp32-p4-lcd4b/README.md), including its ES7210
microphone input and ES8311 speaker output. That page owns the shared pin
assignments, power warning, flash-size limit, and serial recovery procedure.

## Requirements

Voice Assistant functionality requires Azure Speech configuration and network
access. An external reply workflow is needed to turn recognized speech into
an assistant response; the device is not a standalone local language model.
Configure the service credentials and reply integration in the Voice Assistant
portal before testing microphone-to-speaker interaction.

## Differences from the standard target

The firmware enables the Voice Assistant device class and its portal controls.
USB HID is disabled, so do not expect the native USB connection to type as a
keyboard. The base board's standard-target USB serial assumptions do not
describe this variant's build settings; use USB TO UART for diagnostics and
recovery.

Wi-Fi/MQTT, BLE keyboard support, MCP, remote images, native Extensions,
speaker playback, and microphone/music-analysis support remain configured.
Files use internal flash; no primary SD backend is enabled.

The physical 32 MB flash remains limited to a 16 MB firmware layout.
Use this exact target for the initial full flash. OTA is supported afterward.
Back up settings and files before changing variants.

## References

* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)
