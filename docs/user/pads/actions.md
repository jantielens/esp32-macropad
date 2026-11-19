---
title: Sequence actions and navigate
description: Configure tap and hold lists, continuation and failure behavior, confirmation, and pad-level navigation.
---

Actions tell the device what to do when a button or another event is activated.
They can change a screen, send a command, adjust a feature, or show feedback,
and several actions can form an ordered sequence.

> [!WARNING]
> Action configuration uses the versioned operation format. Older actions
> cannot run or be saved in this format and are not converted automatically.
> Back up your configuration and recreate affected actions using the operation
> picker. If the pad cannot load, see
> [pad replacement guidance](../device/backup-and-restore.md#export-and-restore-one-pad).
> Available operations depend on the installed firmware.

## Configure one interaction

In Button **Actions**, choose an unused Tap or Long-press slot.
Ordinary buttons support up to three ordered actions for each gesture.
Set a slot to **(none)** to remove it, then **Save Pad**.
Holding an ordinary button for about 500 ms invokes its long-press list.
Touch-owning [widgets](../widgets/README.md) can replace these interaction rules.

The dropdown groups supported actions by category. Use **Search operations** to
filter by name, category, or operation ID. Use the current fields and help
instead of assuming another firmware's action catalog applies.
An action that queues network work is not proof its external effect completed.
**Load saved note** also returns after accepting a request, so a following
navigation action does not wait for storage. See
[loaded note details](../media/voice-notes.md#show-a-loaded-voice-note-on-a-pad).

## Set a runtime variable

Choose **Set variable** and enter a **Name** and a **Value**. Fixed values
are stored literally; choose **Dynamic** beside an argument to resolve its
binding or expression when the action runs. The action does not require MQTT
or Home Assistant.

1. Add **Set variable** to a button's Tap actions. Set **Name** to `counter`
	and select **Dynamic** for **Value**. Enter `[expr:[var:counter|0] + 1]`.
2. Set a button label to `[var:counter|0]`. Add a reset button with **Set
	variable**, **Name** `counter`, and **Value** `0`.
3. Choose **Save Pad**, activate the counter button, and observe the label
	increase. Activate the reset button to return it to zero.

The saved action configuration persists, but current variable values are
volatile. Values survive pad changes and configuration reloads without a
reboot; reboot clears them. Empty Values are valid. If a Value cannot resolve,
the variable store is full, or memory cannot be allocated, the action fails, leaves the previous value
unchanged, and stops the remaining actions in that list. See
[runtime-variable binding syntax](bindings.md#read-a-runtime-variable) and
[expression examples](expressions.md#update-runtime-state).

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

**Navigate to screen** offers currently available screens. Choose **Dynamic**
for a target such as `[item:id]` in a List item action.
The result must be a valid screen ID within 31 UTF-8 bytes.
The activated List row and originating pad bindings remain available throughout
the action sequence, including after a pause or navigation.
The [sample pad](../../samples/action-framework-gate1.json) includes a fixed
screen choice, a List target, and a Numeric Rocker timer adjustment. Create Pad 2
before using its fixed screen action.

Successful Navigate to screen/back/pad sequence wakes the display and resets
inactivity without disabling the screensaver. Failed navigation does not wake it.
Automated repeated navigation can keep it awake.

**Navigate pad sequence** visits saved, nonempty effective pads in numeric order.
Inherited, disabled, or dynamically hidden buttons count; a local full-screen
tap action also makes a pad eligible. Choose Next/Previous, optional Wrap, and
comma-separated 1-based exclusions such as `2,5,8`.
Empty pads are skipped; the action fails if no destination is available.

## Use full-screen tap actions

Pad **Actions > Full-Screen Tap Actions** configures up to three actions.
They capture normal taps before buttons/widgets, including controller widgets.
They do not run on long press; swipes retain their own handling.
Clear the list to restore normal button interaction.
These actions are local to the pad and not inherited from a template.

Full-screen taps use normal tap feedback. An explicit Sound alert suppresses
the default beep. Touch-owning widgets can also disable pad swipes;
check both layers before investigating an unreachable control.

## Restart into setup AP

The **Restart into AP** action restarts
the device into its setup network without clearing saved Wi-Fi settings or the
AP password. The active button action ends when the device restarts. If the
device is already in setup mode, use its displayed credentials and portal URL.

## Choose the feature owner

* [Keyboard, mouse, and gamepad actions](../integrations/keyboard-mouse-gamepad.md)
* [MQTT publishing](../integrations/mqtt.md) and [Home Assistant calls](../integrations/home-assistant.md)
* [BTHome Bluetooth button events](../integrations/home-assistant.md#send-a-bthome-button-event)
  use device-wide event timing in **Connectivity > Bluetooth**, not
  per-action radio settings.
* [Timers](../device/timers.md), [alarms](../device/alarms.md), and [audio](../media/audio.md)
* [Boot, swipe, physical-button, and MQTT-trigger hosts](../device/triggers.md)
* [Voice capture and speech](../specialized/voice-assistant.md)

Validate bindings and test harmless actions before adding external effects.

[Pad tasks](README.md) | [User guides](../README.md)
