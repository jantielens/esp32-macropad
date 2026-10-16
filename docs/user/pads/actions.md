---
title: Sequence actions and navigate
description: Configure tap and hold lists, continuation and failure behavior, confirmation, and pad-level navigation.
---

Actions tell the device what to do when a button or another event is activated.
They can change a screen, send a command, adjust a feature, or show feedback,
and several actions can form an ordered sequence.

## Configure one interaction

In Button **Actions**, choose an unused Tap or Long-press slot.
Ordinary buttons support up to three ordered actions for each gesture.
Set a slot to **(none)** to remove it, then **Save Pad**.
Holding an ordinary button for about 500 ms invokes its long-press list.
Touch-owning [widgets](../widgets/README.md) can replace these interaction rules.

The dropdown groups supported actions by category. Use its current fields/help
instead of assuming another firmware's action catalog applies.
An action that queues network work is not proof its external effect completed.

## Pause and confirm safely

Delay accepts a literal whole number, 1-55000 ms, and pauses only that list.
Send keys, gamepad taps, recording-until-silence, and confirmation also have
their own completion/failure behavior. Pending-action capacity is bounded;
when it is exhausted, another pausable request fails and stops its list.

Place **Ask for confirmation** before destructive work. Earlier actions already
ran. Approval resumes the suffix once; cancellation, timeout, replacement, or
failure stops it. Confirmation counts toward the three-action limit.
MCP button presses do not bypass it. See [dialogs](dialogs-and-alerts.md).

Terminal actions such as shutdown do not run later actions or save drafts.
Finish saves/uploads first. Restart, sleep, firmware updates, and navigation
can interrupt UI/work; do not sequence them as though they were a save barrier.

## Navigate between screens

**Navigate to screen** offers currently available screens. Choose **Binding
value** for a dynamic target such as `[list:pads.selected]`.
The result must be a valid screen ID within 31 UTF-8 bytes.
A List selection target only resolves with the appropriate selection context.

Successful Navigate to screen/back/pad sequence wakes the display and resets
inactivity without disabling the screensaver. Failed navigation does not wake it.
Automated repeated navigation can keep it awake.

**Navigate pad sequence** visits saved, nonempty effective pads in numeric order.
Inherited, disabled, or dynamically hidden buttons count; a local full-screen
tap action also makes a pad eligible. Choose Next/Previous, optional Wrap, and
comma-separated 1-based exclusions such as `2,5,8`.
Empty pads are skipped; without Wrap, a boundary is a no-op.

## Use full-screen tap actions

Pad **Actions > Full-Screen Tap Actions** configures up to three actions.
They capture normal taps before buttons/widgets, including controller widgets.
They do not run on long press; swipes retain their own handling.
Clear the list to restore normal button interaction.
These actions are local to the pad and not inherited from a template.

Full-screen taps use normal tap feedback. An explicit Sound alert suppresses
the default beep. Touch-owning widgets can also disable pad swipes;
check both layers before investigating an unreachable control.

## Choose the feature owner

* [Keyboard, mouse, and gamepad actions](../integrations/keyboard-mouse-gamepad.md)
* [MQTT publishing](../integrations/mqtt.md) and [Home Assistant calls](../integrations/home-assistant.md)
* [Timers](../device/timers.md), [alarms](../device/alarms.md), and [audio](../media/audio.md)
* [Boot, swipe, physical-button, and MQTT-trigger hosts](../device/triggers.md)
* [Voice capture and speech](../specialized/voice-assistant.md)

Validate bindings and test harmless actions before adding external effects.

[Pad tasks](README.md) | [User guides](../README.md)
