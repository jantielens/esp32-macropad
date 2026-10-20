---
title: Control the device from Home Assistant
description: Use discovered screens, audio, camera controls, and the complete notification command language.
---

Home Assistant can expose supported device features as entities for dashboards
and automations. These controls let other events navigate the device, show a
notification, or request sound and camera operations without a local button tap.

First configure [MQTT/discovery](home-assistant.md#discover-the-device).
Use entity IDs from your HA device page; names below describe capabilities,
not guaranteed identifiers.
Sleeping [Frame firmware](../specialized/epaper-frame/status.md) uses a different
telemetry model and is not an always-connected remote-control endpoint.

## Navigate and wake

The screen Select entity navigates to supported screens.
MQTT equivalents use `<base>/screen/set` and `<base>/screen/state`.
Use an advertised screen ID, not a pad's display label.
Screen navigation wakes/reset inactivity on success.
The [wake binding](../device/display-and-sleep.md) can keep the display awake
while an external ON value remains true; it is different from a one-time wake.

## Send a notification

The Notify text entity accepts plain text or a JSON command:

```json
{"text":"Washer finished","duration_ms":5000}
```

Text is required in JSON and holds up to 191 UTF-8 bytes.
Duration is a literal integer: 1000-300000 ms, default 3000, or 0 for persistent.
Plain text uses the default duration. To send literal text beginning with `{`,
wrap it as the text value in valid JSON.
Send an empty value to dismiss the active notification.

```yaml
action: text.set_value
target:
  entity_id: text.YOUR_DEVICE_notify
data:
  value: '{"text":"Washer finished","duration_ms":0}'
```

Replace the entity placeholder using HA's entity selector.
Direct topics are `<base>/notify/set` and retained `<base>/notify/state`.
State is the last accepted text, not proof a dialog remains visible.
Do not retain notification commands.

Appearance comes from [Dialog appearance](../pads/dialogs-and-alerts.md).
Notifications block underlying input and can replace confirmations, canceling
their waiting lists. Tap, dismissal, screen changes, or replacement can end them.

## Audio controls

Siren supports tones default, alert, doorbell, warning, and custom.
Turn on with optional duration in seconds and volume_level 0.0-1.0.
Without duration it loops until stopped/replaced.
Volume number 0-100 changes the persistent device volume;
a siren volume override affects that playback only.
Custom Tone text uses the [tone language](../media/audio.md#write-a-tone-pattern);
a trailing gap controls repetition spacing.
Beep/Double/Triple buttons request one-shot alerts.

New ordinary alerts replace prior ordinary alerts; they do not override a
scheduled ringing alarm. Feedback, Music interruption, and stop ownership follow
[audio priority](../media/audio.md#understand-sound-priority).
Use a bounded duration and an explicit stop control when automating a siren.
See the [alert example](../examples/home-assistant-alerts.md).

## Camera controls

Camera-capable firmware discovers Save Camera Snapshot, Save Camera Snapshot
and Roll, and Camera Presence when motion sensing is enabled.
Presence is motion-derived; a still person can time out.
Snapshot roll needs valid time and available storage.

For video, add HA's MJPEG IP Camera integration with:

```text
http://DEVICE_IP/api/camera/stream
http://DEVICE_IP/api/camera/snapshot.jpg
```

Use portal Basic Auth credentials if enabled and avoid stream preloading.
The [camera guide](../media/camera.md) owns stream limits, motion tuning, and
snapshot privacy.

[Integration tasks](README.md) | [User guides](../README.md)
