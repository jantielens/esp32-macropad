---
title: Find a fix by symptom
description: Start with the right feature owner instead of resetting the device or searching a long manual.
---

Unexpected behavior can come from configuration, missing data, connectivity,
or hardware rather than a single device-wide fault. Match the symptom below
to a first check and its detailed feature guide before resetting or replacing
settings.

| Symptom                                     | First check and owning guide                                                                                         |
| ------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Cannot open the portal                      | [Wi-Fi, address, login, and recovery](device/network-and-access.md)                                                  |
| Save did not change another section         | [Independent saves and pending reboot](portal.md)                                                                    |
| Device shows an older pad                   | [Save Pad before Show on Device](pads/editing.md)                                                                    |
| Button tap does something unexpected        | [Action ordering/full-screen precedence](pads/actions.md)                                                            |
| Controls stop responding under a popup      | [Modal notification/confirmation behavior](pads/dialogs-and-alerts.md)                                               |
| Binding displays unknown/error              | [Syntax, fallback, context, and live data](pads/bindings.md)                                                         |
| Conditional division still fails            | [Eager expression evaluation](pads/expressions.md)                                                                   |
| Widget scale/lines look wrong               | [Widget-specific ranges, pairing, and data contracts](widgets/README.md)                                             |
| HID output/pairing fails                    | [Transport, data cable, reboot, and host state](integrations/keyboard-mouse-gamepad.md)                              |
| MQTT or HA state is missing                 | [Broker/topics](integrations/mqtt.md) and [independent HA paths](integrations/home-assistant.md)                     |
| AI tools are missing/refused                | [MCP permissions, token, identity, and client origin](integrations/ai-assistants.md)                                 |
| Font/icon/image is missing                  | [Media sources and limits](media/README.md)                                                                          |
| No sound or unexpected interruption         | [Audio output, volume, and ownership](media/audio.md)                                                                |
| Clock/alarm schedule is wrong               | [Time synchronization/timezone](device/time.md) and [alarm state](device/alarms.md)                                  |
| Camera preview or presence is stale         | [Camera clients, motion, and snapshots](media/camera.md)                                                             |
| E-paper looks stale despite correct preview | [Interactive presentation](specialized/interactive-epaper.md) or [Frame results](specialized/epaper-frame/status.md) |
| Sleeping sensor cannot be reached           | [Duty-cycle operation and recovery](specialized/sensor-node.md)                                                      |
| Instrument measurements are implausible     | [Calibration and first-use topics](specialized/README.md), then [board hardware notes](hardware.md)                  |
| Storage is full or import is incomplete     | [Storage](device/storage.md) and [backup/import boundaries](device/backup-and-restore.md)                            |
| Reboot/crash/update trouble                 | [Diagnostics](device/diagnostics.md) and [updates/recovery](device/updates-and-recovery.md)                          |

Before reset, export valuable data and record firmware version, exact action,
feature settings, and relevant logs. Redact credentials/private payloads.
A screenshot verifies pixels, not physical hardware or a remote action's effect.

[User guides](README.md)
