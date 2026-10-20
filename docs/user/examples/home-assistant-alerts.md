---
title: Build a doorbell alert with quiet hours
description: Combine a persistent notification, optional bounded siren, quiet-hour condition, and explicit dismissal.
---

A doorbell alert can leave a visible message on the device while sounding only
during chosen hours. Notification and audio are independent, so quiet hours
can suppress the sound without losing the visual reminder.

Set up [HA discovery](../integrations/home-assistant.md), and select the actual
Notify/Siren entities from your device.
Use the UI to replace every example entity below.
Choose whether replacing a current confirmation is acceptable before sending
remote notifications.

## Notify always, sound only by day

```yaml
alias: Doorbell on pad
triggers:
  - trigger: state
    entity_id: binary_sensor.front_doorbell
    from: "off"
    to: "on"
actions:
  - action: text.set_value
    target:
      entity_id: text.YOUR_DEVICE_notify
    data:
      value: '{"text":"Someone at the door","duration_ms":0}'
  - if:
      - condition: time
        after: "08:00:00"
        before: "22:00:00"
    then:
      - action: siren.turn_on
        target:
          entity_id: siren.YOUR_DEVICE
        data:
          tone: doorbell
          duration: 3
          volume_level: 0.6
mode: restart
```

HA evaluates those quiet hours in HA's own time settings.
The persistent notification is independent of the siren's bounded duration.
For a custom pattern, configure Custom Tone using the
[audio language](../media/audio.md#write-a-tone-pattern) and use tone custom.
Scheduled ringing alarms retain their higher audio ownership.

## Dismiss and test

Tap the notification or set Notify to empty text. To stop a running siren,
call siren.turn_off separately.
A transparent notification still blocks underlying controls.
New notifications can replace it, and screen changes can dismiss it.
The retained notification text entity is not visibility feedback.

Test daytime, quiet hours, repeated doorbell events, and dismissal without
depending on an actual visitor.
If discovery lacks either entity, the firmware may not support that feature.
Use [remote control semantics](../integrations/home-assistant-control.md),
not guessed entity names.

[Examples](README.md) | [User guides](../README.md)
