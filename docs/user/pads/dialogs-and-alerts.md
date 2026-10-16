---
title: Notifications, confirmations, and visual alerts
description: Display messages, request approval, configure shared dialog appearance, and clear visual overlays.
---

Notifications show a message, confirmations wait for a user's decision, and
visual alerts draw attention to the screen. Choose a dialog when input must
pause, or a visual alert when you want emphasis without an approval step.

## Show a notification

Add **Display > Show notification** to an action list. Enter a nonempty message,
up to 191 UTF-8 bytes, with optional line breaks/bindings.
Bindings resolve once when displayed. Choose 1-300 seconds or Until dismissed.
New editor actions start with sample text and persistent timing; replace them.
Save the pad or host configuration.

Notification does not pause its action list. It blocks underlying pad input,
even with a transparent backdrop. Tap or physical Tap dismisses it without
activating a control underneath.

## Request approval

Add **Ask for confirmation** before the action that needs consent.
Its message has the same capacity and one-time binding resolution.
Captions are literal, up to 27 UTF-8 bytes each; choose a timeout of 1-300 seconds
or No timeout. It requires usable touch or configured physical navigation.

Neither confirmation choice starts selected. With physical navigation, first
Next selects Cancel, first Previous selects Approve, and first Tap selects
Cancel without activating it. Subsequent Next/Previous changes selection;
Tap activates it. Hold does nothing.
Touch selects directly.

Approval resumes the remaining list once. Cancel/timeout/replacement stops it.
A successfully opened newer dialog replaces either dialog type; replacing a
confirmation cancels its waiting list permanently. Failed replacements preserve
the old dialog. Navigation, display teardown, and OTA also dismiss/cancel it.
Do not assume a second client can replace a prompt without consequences.

## Set shared appearance

Open **Pads > Dialog appearance**. Notification and confirmation have separate
templates for fonts, colors/opacity, borders, spacing, size, alignment, position,
and confirmation buttons. Save dialog appearance persists both.
Newly opened dialogs use the saved templates, not per-action styling.

Browser previews use sample text. **Test now** displays a fixed sample on the
device for up to ten seconds using unsaved appearance. It does not save or run
follow-up actions. A successful test replaces the current dialog and cancels
any waiting confirmation. Avoid testing during a real approval.

Supported backgrounds include patterns, spacing 2-64 px, and stroke 1-8 px,
not greater than spacing. Background opacity affects pattern and base together.
TTF dialog sizes accept whole pixels 8-96, or empty for Automatic.
Builtin font sizes use the offered dropdown.

## Use a visual alert

Where supported, **Visual alert > Start alert** wakes the display and raises
a full-screen tint. Choose color/binding, Breathe/Blink/Solid, period,
intensity 1-100%, and duration in milliseconds.
Duration 0 persists until stopped, tapped, or replaced.
Use a comfortable intensity/cadence; reserve it for meaningful events.

Stop alert clears it. Expiry or tapping also clears it; a new alert replaces it.
A notification can sit above the tint. Audio and scheduled alarm ownership are
separate; stopping a tint does not cancel an [alarm](../device/alarms.md).

For remote notification commands, use
[Home Assistant controls](../integrations/home-assistant-control.md#send-a-notification).

[Pad tasks](README.md) | [User guides](../README.md)
