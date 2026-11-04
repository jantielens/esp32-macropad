---
title: Connect keyboard, mouse, and gamepad output
description: Select USB/BLE transport, pair a host, write complete keyboard sequences, and use standalone controller actions.
---

HID (Human Interface Device) output lets the device act as an input controller
for a computer or another host. Buttons and touch widgets can send keyboard,
mouse, or gamepad input over either USB or BLE. The same widgets and actions
use the selected transport; there are no separate BLE controls.

## Select and connect

Open **Connectivity > Keyboard, Mouse & Gamepad**. Select a supported transport,
save, then reboot. Off is the initial default. There is no automatic fallback.
The active-status section reflects the running transport, not an unsaved or
pending selector.

USB provides keyboard/consumer keys, relative mouse, and generic HID gamepad.
Connect a data-capable cable to the native device/OTG port, not a USB-UART or
host-only connector. Check [board connections and power warnings](../hardware.md)
before attaching more than one powered cable.
BLE provides keyboard/consumer keys, relative mouse, and generic HID gamepad
over the paired Bluetooth connection. Gamepad output is generic HID, not XInput
or a console-specific controller protocol; host and application support varies.

The host sees the configured name with USB/BLE suffix after reboot.
Cached host names can need reconnect/removal. Changing transport while a macro
is busy is rejected.

## Pair BLE

BLE uses bonded Legacy Just Works pairing: no passkey entry or number
confirmation. LE Secure Connections is disabled. Pairing has no
man-in-the-middle protection, and a passive observer who captures the initial
pairing can derive its encryption key. Pair only in a trusted environment.
Saved bond keys allow the owner to reconnect without reopening pairing.

1. With BLE active, open **Connectivity > Keyboard, Mouse & Gamepad**.
2. Remove the old saved device in the host's Bluetooth settings.
3. Select **Pair New Device** in the portal. It clears previous bonds and opens
	a 60-second pairing window without reboot.
4. Select the advertised device in the host's Bluetooth settings.
5. Check that the portal reports connected, bonded, and encrypted before testing
	a harmless key action.

A **Start BLE pairing** action provides the same control from a pad. New hosts
cannot become the owner outside the pairing window. A radio connection alone
does not enable HID reports; encryption and bonding must complete first.

If Windows repeatedly connects and disconnects, remove its saved device and
repeat these steps. Failed authentication does not automatically open pairing
or clear every bond. A confirmed missing-key failure allows at most one recovery
retry within an already-open window; another closes the window. Select
**Pair New Device** again to restart. Firmware security changes may require a
fresh pairing on both sides. After updating from keyboard-only BLE firmware,
remove the saved host device and pair again to refresh its HID report layout.

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

Disconnects, transport changes, USB reset, and OTA clear pending input/holds
without replay. Host acceptance,
application mapping, and physical contact limits still need checking.

[Integration tasks](README.md) | [User guides](../README.md)
