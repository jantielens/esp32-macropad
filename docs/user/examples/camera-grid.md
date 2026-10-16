---
title: Build a remote camera grid
description: Place several reachable JPEG snapshot sources on a pad while controlling refresh traffic and privacy.
---

A camera grid places several remote views side by side so you can check them
from one pad. Periodically fetched snapshots offer a starting point without
requiring several continuous video streams.

Use remote camera JPEG snapshot URLs reachable by the device.
This differs from [Camera Preview](../media/camera.md), which renders the
device's attached camera.
Review [remote image setup](../media/images.md) for accepted source syntax,
authentication limitations, and image sizing.

## Place the sources

Create a 2x2 pad and give each button a useful span.
In each button's Content > Background image > Camera Feed or Remote Image,
enter a snapshot URL, optional Basic Auth user/password, and Refresh Interval
5000 ms. Choose Cover/Letterbox and Save Pad.

| Button     | Example source                     |
| ---------- | ---------------------------------- |
| Front door | `http://192.168.1.61/snapshot.jpg` |
| Driveway   | `http://192.168.1.62/snapshot.jpg` |
| Garden     | `http://192.168.1.63/snapshot.jpg` |
| Garage     | `http://192.168.1.64/snapshot.jpg` |

These are examples of endpoint shape, not universal camera paths.
Test each actual URL from the device's network and use a supported image format.
Keep a short label identifying the source and a separate navigation control.

## Tune and verify

Choose a modest refresh interval for every source; start with several seconds,
not video-rate polling. Confirm four distinct images arrive, then disconnect
one source and inspect its error/fallback behavior.
Save Pad and check physical device memory/network behavior.

JPEG polling is not a synchronized multi-camera recorder.
An old image can remain after a failed refresh, so never infer current security
status from a single unchanged frame. For live MJPEG, use the explicitly supported
streaming path and its resource limits, not four presumed unlimited streams.
Do not embed camera credentials in shareable URLs/exports.

[Images](../media/images.md) | [Examples](README.md) | [User guides](../README.md)
