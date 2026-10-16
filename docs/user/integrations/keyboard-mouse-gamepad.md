---
title: Connect keyboard, mouse, and gamepad output
description: Select USB/BLE transport, pair a host, write complete keyboard sequences, and use standalone controller actions.
---

HID (Human Interface Device) output lets the device act as an input controller
for a computer or another host. Buttons and touch widgets can send keyboard,
mouse, or gamepad input over the supported transport; BLE supports keyboard
output, while USB also supports mouse and gamepad.

## Select and connect

Open **Connectivity > Keyboard, Mouse & Gamepad**. Select a supported transport,
save, then reboot. Off is the initial default. There is no automatic fallback.
The active-status section reflects the running transport, not an unsaved or
pending selector.

USB provides keyboard/consumer keys, relative mouse, and generic HID gamepad.
Connect a data-capable cable to the native device/OTG port, not a USB-UART or
host-only connector. Check [board connections and power warnings](../hardware.md)
before attaching more than one powered cable.
BLE provides keyboard, not USB mouse/gamepad output.

The host sees the configured name with USB/BLE suffix after reboot.
Cached host names can need reconnect/removal. Changing transport while a macro
is busy is rejected.

## Pair BLE

With BLE active, remove the old host's saved Bluetooth device, then select
**Pair New Device**. It clears the previous bond and opens a 60-second window
without reboot. Select the advertised device on the new host.
Check connected/bonded/encrypted status.
A Start BLE pairing action provides the same control from a pad.

## Write a key sequence

Add **Send keys** in an action list. Both backends use US ASCII mapping and
the same space-separated language:

| Input                                                                  | Meaning                        |
| ---------------------------------------------------------------------- | ------------------------------ |
| `a`, `enter`, `tab`, `esc`, `space`, `backspace`, `delete`             | Single key                     |
| `up`, `down`, `left`, `right`                                          | Arrow key                      |
| `f1` through `f12`                                                     | Function key                   |
| `ctrl+c`, `ctrl+shift+t`, `alt+f4`, `gui+l`                            | Modifier chord                 |
| `vol_up`, `vol_down`, `mute`, `play_pause`, `next_track`, `prev_track` | Consumer key                   |
| `"Hello World"`                                                        | Literal text, including spaces |
| `200ms`                                                                | Delay in milliseconds          |

Modifiers are ctrl, shift, alt, and gui (Windows/Command).
For example:

```text
ctrl+a 200ms ctrl+c
"Hello World" enter
```

The sequence field supports bindings. Do not put passwords in reusable macros,
exports, or public examples. A host using another keyboard layout can interpret
US-mapped output differently; test harmless text first.
Save the pad or action host separately.

## Understand failure

Only one macro can be pending/running. Disabled, busy, or disconnected requests
fail instead of building a queue. Disconnect, suspension, report failure, OTA,
or the 60-second total deadline aborts it and stops the remaining action suffix.
Reconnection never replays interrupted input.

## Mouse and gamepad actions

An ordinary **Mouse button** action clicks Left, Right, or Middle once.
It can run from a pad, rocker, or physical input; touch is not required.
It does not move the pointer or hold for dragging. Overlapping a held same
button is rejected.
Use [Mousepad](../widgets/mousepad.md) and [Scrollpad](../widgets/scrollpad.md)
for surfaces.

Ordinary **Gamepad** actions target button 1-16, a hat direction, or a binary
trigger with Tap/Down/Up. Down/Up shares an idempotent latch per control:
any standalone Up releases that latch, but not a widget-owned hold.
Tap rejects an already-held target and completes after its release report.
Use [held widgets](../widgets/gamepad.md) for independent touch-owned releases.

USB reset and OTA clear pending input/holds without replay. Host acceptance,
application mapping, and physical contact limits still need checking.

[Integration tasks](README.md) | [User guides](../README.md)
