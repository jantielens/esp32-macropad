---
title: Attached camera and motion sensing
description: Configure camera output, test captures, save snapshots, preview on a pad, and tune motion detection.
---

An attached camera can supply a live preview, saved snapshots, and motion
detection on supported firmware. These features use the camera connected to
the device, rather than a feed downloaded from another camera.

Use this path when the firmware supports an attached camera.
An external camera URL is a different capability; use
[remote images and feeds](images.md#fetch-a-snapshot-or-stream).

## Configure and test capture

1. Open **Camera > Camera settings** and check whether the camera is detected.
2. Choose JPEG quality, an offered JPEG output size, and capture rotation.
3. Set the shared capture rate. The current controls offer 1-5 FPS; lower rates
   reduce load but increase preview and motion-response latency.
4. Adjust shutter time and red/blue white-balance correction if needed.
   Longer shutter time brightens the image; the portal shows its approximate
   duration. White-balance multipliers adjust the scene estimate.
5. Select **Save settings**, or **Test capture** to save the current settings
   and capture a JPEG in one operation.
6. Inspect the preview and use **Download JPEG** to keep that test image.

These settings apply without reboot. Capture rotation affects the shared image
used by Camera Preview, test capture, and MJPEG streams.
Output sizes are validated choices, not arbitrary raw-sensor modes.

The section provides snapshot and MJPEG links. Its displayed concurrent-stream
limit is authoritative for the running firmware. Close unused streams before
testing another client. A network snapshot request is not automatically an
entry in the saved camera roll.

## Put a preview on a pad

Select **Camera Preview** as the button's content/widget and choose its
**Image scale**: Cover, Letterbox, or Center crop. This setting is separate
from a button's remote/local background Scale.
Save the pad and verify the live result.
Capture cadence is shared with other camera consumers; a complex pad can
benefit from a lower rate.

## Save and manage snapshots

Use a **Camera** action in the button editor. Its **Save to** choice is
latest, roll, or both. Latest writes `/camera/latest.jpg`; the camera roll
uses UTC date folders and numbered images.

> [!IMPORTANT]
> Camera-roll storage requires valid synchronized time. Without it, a latest
> copy can still be saved but the dated roll copy is skipped. Check
> [device time](../device/time.md) before relying on roll capture.

Open **Camera > Snapshots** to view latest and expand dated groups.
Each group initially loads 48 entries; use **Show more snapshots** for more.
Deletion can remove one image, a day's folder, or the latest copy.
Download anything you need before deleting; the operation is not undoable.
Monitor [free storage](../device/storage.md) during repeated capture.

## Detect motion

Open **Camera > Motion sensing**. It is off by default.

1. Enable local motion analysis.
2. Choose **Analyze every** first through fourth frame. The displayed analysis
   rate follows the shared camera capture rate.
3. Set sensitivity, 1-10. Higher settings detect smaller changes but can
   increase reactions to lighting/shadows.
4. Set **Presence hold time**, 10-600 seconds after the last motion.
5. Optionally configure **Keep display awake while motion is detected** and
   up to three actions on motion detection.
6. Save, then test actual movement and quiet conditions.

Analysis runs locally without sending frames to a cloud service. It continues
while the display sleeps, pauses during firmware updates, and does not request
preview/JPEG frames when motion is its only camera use.
Disabled sensing does not perform motion analysis.

> [!IMPORTANT]
> Camera Presence is motion-derived, not a reliable occupancy sensor.
> A person who remains still creates no new motion, so presence expires after
> the hold time. Do not use it as proof that a room is empty.

Actions run on the transition from absent to present, once per presence episode.
A Device command **Wake display** is a one-time wake. The keep-awake option
resets inactivity on each confirmed motion sample; the presence hold time alone
does not hold the display on after movement stops.

## Tune and inspect

Check Camera Presence under **Sensors > Sensor Data**, or use local bindings:

```text
[camera:presence]
Tiles [camera:changed_tiles]/[camera:tile_threshold]
Score [camera:score]/[camera:score_threshold]
[camera:global_change]
[camera:sample_age]
```

Compare scores with thresholds rather than raising sensitivity blindly.
Global-change rejection helps identify broad illumination changes; sample age
helps detect stalled analysis. The Binding reference provides the other motion
keys. MQTT publishes presence when that integration is configured.

Camera images and recordings can contain private information. Keep access on
a trusted network and include saved captures in your separate backup/privacy
plan. See [diagnostics](../device/diagnostics.md) for failures.

[Media tasks](README.md) | [User guides](../README.md)
