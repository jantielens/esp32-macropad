---
title: Button images and remote feeds
description: Upload JPEG/PNG backgrounds, follow the shared slideshow, and configure snapshot or MJPEG sources.
---

Images provide button backgrounds, shared slideshows, and views of remote
snapshots or streams. A local image is stored on the device, while a remote
source must be fetched over the network.

Local image storage, remote fetching, and the attached
[camera](camera.md) are separate firmware capabilities.
Use the controls available on your device.

## Upload a local background

1. Open **Display > Image Library**.
2. Choose a JPEG or PNG and select **Upload**. Each file can be at most 4 MiB;
   PNGs are additionally limited to 2 million source pixels.
3. Wait for success and the refreshed file list.
4. In **Pads > Pad Editor**, select a button and open **Content > Background
   image > Local Image**.
5. Choose the uploaded image, set **Scale**, then **Save Pad**.

Cover fills and crops to the button. Letterbox preserves the whole image
with bars matching the button's background color. The shared Scale control
also applies to remote backgrounds.
Prepare images near the intended display size to reduce decoding/memory load.

Delete unused files through Image Library. A pad referencing a deleted image
does not receive a replacement automatically.

## Follow a slideshow

In Image Library, set the **Slideshow** interval to 0-3600 seconds and select
its **Save** control. Changes apply without reboot. Zero disables automatic
advancement; Image Next and Image Previous actions still work.

Set a button's **Local Image Binding** to:

```text
[image:current]
```

All buttons using that binding follow one shared cursor, each with its own
layout. A fixed image selection does not advance. Use Image Next/Previous
actions for manual controls and save the pad.
To inspect local image failures, place `[image:error]` in a label; it starts
as `ok` and reports the most recent upload/display failure since boot.

## Fetch a snapshot or stream

Open the button's **Background image > Camera Feed or Remote Image**.
Choose one intended source rather than leaving competing source settings.

* Enter an HTTP/HTTPS URL returning JPEG, PNG, or a multipart MJPEG stream.
  A browser dashboard URL or raw RTSP URL is not an image source.
* Supply **Auth User** and **Auth Password** for Basic Authentication if needed.
  A blank password preserves one already saved; exports do not recover it.
* For a periodically fetched snapshot, set **Refresh Interval (ms)**, such as
  `5000` for five seconds.
* For continuous MJPEG, set the interval to `0`. The endpoint must serve
  `multipart/x-mixed-replace`. For a static image, zero means fetch once.
* Set Cover/Letterbox and **Save Pad**.

The device, not just your browser, must be able to reach and authenticate to
the URL. An RTSP camera can need an external image/MJPEG relay.
Start with a small snapshot and a moderate interval before enabling streams.
Multiple large sources increase network, decoding, and memory load; there is
no universal frame-rate guarantee.

> [!WARNING]
> Keep private feeds on a trusted network. The remote-fetch HTTPS path does not
> validate server certificates; do not treat it as verified server identity.
> Do not embed secrets in a shared URL or exported pad.

## Check a missing image

Check the selected firmware capability, source format, URL reachability,
credentials, local file existence, and free [storage](../device/storage.md).
Try a smaller image or slower refresh. Verify the saved pad rather than only
the draft. [Diagnostics](../device/diagnostics.md) can help distinguish a
fetch/decode failure from a physical display problem.

The attached-camera **Camera Preview** widget has its own scale control.
See [camera](camera.md) for that workflow.

[Media tasks](README.md) | [User guides](../README.md)
