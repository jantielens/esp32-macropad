---
title: Configure a sensor node
description: Choose always-on, MQTT duty cycle, or BLE telemetry and keep a recoverable configuration path.
---

A sensor node collects measurements and sends them to another system for
display or automation. It can remain active or wake briefly to publish and
sleep again; the operating mode chooses that power and connectivity behavior.

Configure [network/access](../device/network-and-access.md) before switching
into a sleeping mode. In **Device > Operating Mode**, choose an available mode,
save, then reboot.

## Choose transport and cadence

| Mode              | Normal wake behavior                                                     | Configure cadence                 |
| ----------------- | ------------------------------------------------------------------------ | --------------------------------- |
| Always-On         | Runs continuously; available display, portal, and services remain active | MQTT Publish Interval             |
| Duty-Cycle (MQTT) | Wi-Fi connect, one telemetry publish, then deep sleep                    | Wake Interval                     |
| Duty-Cycle (BLE)  | BTHome v2 advertising burst, then deep sleep; no Wi-Fi/MQTT              | Wake Interval and BLE Advertising |

Duty-cycle operation does not keep the portal/display/audio running between
samples. Use the [board's recovery control](../hardware.md) to open configuration.
Wake Interval is sleep time between cycles, not guaranteed wall-clock sample
spacing; awake work adds time. Zero requests immediate wake, not disabled sampling.
MQTT's periodic interval is ignored in Duty-Cycle MQTT.

## Tune MQTT reliability

First verify telemetry on a trusted [broker](../integrations/mqtt.md).
Set Wi-Fi Backoff Max to bound exponential retry sleep after connection failure.
A poor signal can cost more energy than a longer sample interval saves.
Check power/sensor details in board-local notes.

## Tune BLE reception

Use a receiver supporting BTHome v2, such as HA's Bluetooth integration.
Burst Count 1-255 defaults to 3; Advertising Interval 20-10240 ms defaults to 100.
The implementation holds the advertiser for count times interval plus a short
margin; this is not a guaranteed exact packet count at the receiver.
Transmit power ranges from -12 through +9 dBm.
More/stronger advertising trades battery for reception probability.

BLE telemetry is not BLE HID keyboard pairing and needs no MQTT broker.
Check received freshness, not only the last retained measurement.

## Preserve recovery access

Recovery Portal Auto-Sleep applies in Config/AP recovery.
At nonzero delay, an idle portal sleeps for that duration then retries Wi-Fi.
Zero keeps recovery awake indefinitely.
Choose a practical window for editing/updating, and test recovery before leaving
the device unattended.

[Storage/reset](../device/storage.md) | [Specialized workflows](README.md) | [User guides](../README.md)
