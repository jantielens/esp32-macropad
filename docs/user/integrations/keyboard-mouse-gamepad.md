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

Select **Host keyboard layout** in the same section, save, then reboot.
US QWERTY is the default. The 20 available layouts are:

* US QWERTY, US International with dead keys, and UK QWERTY
* Traditional French AZERTY, Belgian AZERTY, and German QWERTZ
* Spanish, Italian, Portuguese, and Brazilian ABNT2
* Swedish, classic Finnish, Norwegian, and Danish
* Swiss German, Swiss French, Polish Programmers, Czech QWERTZ, Turkish Q,
  and Hungarian QWERTZ

Match the active OS layout, not the physical keycap labels. There is no automatic
layout detection or custom-layout editor. Reference mappings follow Linux/XKB
PC layouts, including ISO punctuation keys and the extra ABNT2 key. US
International uses dead keys, not an AltGr-only international variant. Other
OS variants and macOS layouts can differ; test harmless text first.

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

Add **Send keys** in an action list. Both backends use the selected host layout
and the same space-separated language:

| Input                                                                  | Meaning                        |
| ---------------------------------------------------------------------- | ------------------------------ |
| `a`, `enter`, `tab`, `esc`, `space`, `backspace`, `delete`             | Single key                     |
| `up`, `down`, `left`, `right`                                          | Arrow key                      |
| `f1` through `f12`                                                     | Function key                   |
| `ctrl+c`, `ctrl+shift+t`, `alt+f4`, `gui+l`                            | Modifier chord                 |
| `vol_up`, `vol_down`, `mute`, `play_pause`, `next_track`, `prev_track` | Consumer key                   |
| `"Hello World"`                                                        | Literal text, including spaces |
| `200ms`                                                                | Delay in milliseconds          |
| `ctrl+hid:04`                                                          | Ctrl plus raw HID usage 04     |

Modifiers are ctrl, shift, alt, and gui (Windows/Command).
Bare letters and digits, including in chords, use the selected layout.
Digit keys add Shift when needed, including on AZERTY and Czech QWERTZ.
Named punctuation keys such as
`minus`, navigation keys, function keys, and consumer keys remain physical
HID usages. `hid:04` through `hid:df` select raw keyboard usages in hexadecimal,
independent of the layout; use modifier names for modifier keys.
Changing layout changes macro behavior without rewriting saved actions.
For example:

```text
ctrl+a 200ms ctrl+c
"Hello World" enter
```

The sequence field supports bindings and accepts up to 255 UTF-8 bytes.
Quoted text uses the supported characters of the selected layout.
Do not put passwords in reusable macros, exports, or public examples.
Save the pad or action host separately.

## Type literal text

Use **Type text** for ordinary text, not **Send keys**. Quotes, backslashes,
and text such as `ctrl+a 200ms` are typed literally. Do not add surrounding
macro quotes. The text field supports bindings and accepts up to **2,048 UTF-8
bytes**, not 2,048 characters. Empty text, invalid UTF-8, unsupported characters,
and oversized results fail before any keys are sent.

Newlines and tabs are rejected unless **Allow Enter and Tab** is enabled.
When enabled, CRLF produces one Enter; lone CR or LF produces Enter, and tab
produces Tab. These keys can submit a form or move focus. No Enter is appended
automatically.

For microphone dictation, [type a Voice Notes transcript](../media/voice-notes.md#type-a-transcript-on-a-connected-computer)
automatically after transcription or from a preview button.

For larger text, split at UTF-8 character boundaries into requests of at most
2,048 bytes and wait for completion before sending the next chunk. For example,
5 KiB needs three chunks. An action list waits for each Type text action before
continuing. Stop on failure or cancellation, without retrying already typed
chunks. Callers holding a larger message should validate all its characters
before sending its first chunk. Other HID actions can run between chunks;
there is no multi-chunk reservation or request queue.

Accepted text and its layout remain fixed throughout the request, even if its
source binding changes. Binding expressions themselves remain limited to
191 bytes, and individual sources keep their own storage limits; runtime
variables, for example, hold at most 127 bytes. Long output can come from a
capable source or composition of smaller values.

All layouts support printable ASCII. Depending on the selected layout,
additional supported characters include precomposed Latin-1 and Latin Extended-A
letters, accented uppercase letters, and the euro sign when a one- or
two-stroke recipe is available. Examples include Spanish `ñ`, Brazilian `ç`,
Nordic `å ä ø æ`, Polish `ł`, Czech `Č`, Turkish `ı İ`, and Hungarian `ő ű`.
Plain US QWERTY remains ASCII-only.

Use precomposed accented characters, such as `é`, not a letter followed by a
combining accent. Unsupported characters are rejected, not dropped or
transliterated. Arbitrary Unicode and OS-specific Unicode-entry shortcuts
are not supported.

Keep Caps Lock off, release physical modifiers, and finish any pending host
dead key before starting. Some characters require a dead key followed by a
letter or Space. On US International, this also applies to literal apostrophes,
double quotes, backticks, carets, and tildes. Cancellation between those reports
can leave a dead key pending on the host; clear it manually before resuming.

## Understand failure

Only one keyboard request can be pending/running. Disabled, busy, or disconnected requests
fail instead of building a queue. Disconnect, suspension, report failure, OTA,
or the total deadline aborts it and stops the remaining action suffix.
Send keys keeps its 60-second deadline; Type text uses a bounded,
stroke-count-based deadline of at least 60 seconds and at most five minutes.
Reconnection never replays interrupted input.

Use the **Cancel typing** action to stop remaining output.
Cancellation and failure release held keys, but cannot remove text already
typed. Output goes to whichever host application has focus. Completion means
HID reports were executed, not proof that the application inserted the intended
text.

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
