---
title: Actions without a pad-button tap
description: Configure swipe, startup, physical-button, and MQTT-triggered actions with safe context and save behavior.
---

Triggers run actions in response to events, without needing a tap on a pad
button. Events can include startup, swipes, physical input, and incoming MQTT
messages, depending on the firmware.

The portal's **Actions** category offers the hosts supported by your firmware.
They use the same action editor as pad buttons, but each event has its own
context. An action is not automatically meaningful on every host.

## Swipe actions

Open **Actions > Swipe Actions**, choose one action for each direction, and
select **Save Swipe Actions**. Changes apply without reboot.
The default right swipe navigates back.

Touch-consuming widgets, including Mousepad, Scrollpad, Gamepad Joystick, and
Gamepad Button, own contacts that start inside them; those contacts do not
become screen/pad swipes. Their **Disable pad swipe actions (entire pad)**
option disables all pad swipe actions if enabled on any such widget.
Check the pad's own swipe settings as well as the device defaults.

See [widget interaction](../widgets/README.md) before using a
full-screen touch control as your only navigation surface.

## Boot actions

Open **Actions > Boot Actions** and configure up to three sequential actions.
Select **Save Boot Actions**; the new list runs on the next boot, once the
first screen has appeared. Saving does not execute the list now.

For an initial check, use a short notification or other harmless action.
Avoid a boot action that immediately sleeps, restarts, or shuts down the
device until you have confirmed a recovery path.

## Physical buttons

Open **Actions > Hardware Buttons**, choose tap/hold action lists for the
displayed physical inputs, and select **Save Hardware Buttons**.
The settings apply without reboot. Saving an empty list disables the
corresponding runtime default.

These actions do not redefine electrical reset, power switching, or the chip's
download-mode straps. Board-specific button names, defaults, and special
actions belong in the [hardware reference](../hardware.md).
A headless device cannot use display, notification, or timer actions even if
other physical-button actions are available.

## MQTT triggers

Configure the broker under **Connectivity > MQTT**, save, and reboot if required
before using **Actions > MQTT Triggers**.

1. Enter an exact topic, such as `house/pad/command`. Wildcards `+` and `#`
   are not supported for these triggers.
2. Leave **Value filter** empty for any message, or enter an exact payload
   string such as `ON`.
3. Configure up to three sequential actions.
4. Select **Save MQTT Triggers**. Changes apply immediately; subscriptions are
   also restored when the broker reconnects.

Use the number of slots the portal shows; it depends on firmware capacity.
Test from a separate publisher and check that each intended message causes
one event.

> [!WARNING]
> Avoid publishing back to the same topic that triggers the action, especially
> with an empty filter. A feedback loop can repeatedly dispatch actions.

## Check action context

Navigation can select an available screen or a **Binding value** target.
Keep the resolved screen ID valid. A List-selection binding needs a List
selection event; boot, swipe, and MQTT messages do not supply that context.

Save each host's configuration separately from **Save Pad**. Actions that need
external services still require those services and credentials.
For timer expiry and alarm ring/stop hosts, use [timers](timers.md) and
[alarms](alarms.md). The [pad action guide](../pads/actions.md)
covers sequencing, confirmation, delays, and per-button authoring.

[Device tasks](README.md) | [User guides](../README.md)
