---
title: Native Extensions
description: Developer guide for creating and installing ESP32-P4 native Extensions
ms.date: 2026-08-15
ms.topic: how-to
---

## Overview

Extensions are trusted, flash-mapped RISC-V ELF modules for the
ESP32-P4 boards. An Extension can be installed in one slot and placed on
zero or more pad buttons through the **Extension** widget.

The firmware owns storage, flash mapping, LVGL task ownership, package slots,
and button dispatch. An Extension owns only its UI children and any resources it
allocates through its own code.

## Slots

The extension partition contains three fixed slots:

| Slot | Usable ELF capacity |
| --- | ---: |
| Small 1 | 56 KiB |
| Small 2 | 56 KiB |
| Large | 120 KiB |

Upload a signed package named `<extension-id>@<package-semver>.ext`. The ID
uses lowercase letters, digits, and hyphens. A package contains a relocation-free
ELF followed by its 64-byte signature. The portal stages the upload on the
configured storage backend; the next boot verifies and commits it into the
selected executable flash slot.

The signature is an ECDSA P-256 signature over the exact ELF bytes, stored as
a fixed-width 64-byte `r || s` value. Firmware embeds the project's
first-party public key and rejects unsigned, modified, or untrusted packages
before staging. It verifies the staged ELF again before erasing the executable
slot, which prevents a modified staging file from being installed.

## Catalog Metadata

Every extension published in the GitHub Pages catalog has an adjacent
`metadata.json` file. This catalog-only file does not change the package ABI or
ship to the device.

```json
{
  "summary": "Shows nearby aircraft from ADSB.lol in a live radar widget.",
  "usage": "Set Extension configuration to:\n{\"lat\":50.901389,\"lon\":4.484444,\"range_km\":25,\"max_planes\":20,\"interval\":5}"
}
```

`summary` and `usage` are required non-empty strings. The catalog renders
`usage` as pre-wrapped text. Describe the required Extension configuration or
state that no configuration is required.

## Build

```bash
bash tools/build-p4-extension.sh extensions/hello-world/hello_world.cpp build/extensions/hello-world@1.0.0.elf
```

Release-ready packages require `EXTENSION_SIGNING_KEY` to point at the
first-party P-256 private-key PEM file:

```bash
EXTENSION_SIGNING_KEY="$HOME/.config/esp32-macropad/extension-signing-private.pem" \
  bash tools/build-p4-extension.sh extensions/hello-world/hello_world.cpp \
  build/extensions/hello-world@1.0.0.elf
```

`tools/build-p4-extensions.sh` builds every shipped Extension as a signed
package by default. It uses
`.secrets/extension-signing-private.pem` when `EXTENSION_SIGNING_KEY` is unset
and fails rather than creating unsigned packages if no key is available.

This creates the development ELF and the single upload file
`hello-world@1.0.0.ext`. Only generate a new P-256 key pair when establishing a
project key or rotating it:

```bash
bash tools/generate-extension-signing-key.sh "$HOME/.config/esp32-macropad/extension-signing-private.pem"
```

Protect the private key and never commit it. Copy its PEM content to the
repository Actions secret `EXTENSION_SIGNING_PRIVATE_KEY`. The release workflow
is triggered only by a `v*.*.*` tag, while pull request builds use an ephemeral
test key and never access this secret. The public key is compiled into
`native_extension_signature.cpp`; when rotating the key, replace that public key
and ship firmware before uploading extensions signed by the new private key.
Future firmware can add owner-approved third-party public keys using the same
package format and verification path.

The build script rejects ELF files containing relocations. Rebuild and upload
every Extension after a firmware update that changes
`NATIVE_EXTENSION_ABI_VERSION`. Packages must use the current value declared in
`native_extension_api.h`; packages built for a different ABI are intentionally
unsupported.

The extension build links a tiny freestanding runtime that provides `memcpy`
and `memset`; it does not link the Arduino or C++ standard-library runtimes.
It uses the ESP32-P4 `ilp32f` floating-point ABI, matching firmware callbacks
that exchange `float` values. The builder derives target flags and ABI metadata
from the installed ESP32-P4 SDK and `native_extension_api.h`; do not supply
your own `-march` or `-mabi` flags.

Every package must export this fixed, pointer-free descriptor:

```cpp
extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
  NATIVE_EXTENSION_DESCRIPTOR_MAGIC,
  NATIVE_EXTENSION_ABI_VERSION,
  NATIVE_EXTENSION_TARGET_ABI,
  "example-id",
  "1.0.0",
  "Example Extension",
  NATIVE_EXTENSION_TICK_INTERVAL_DEFAULT_MS,
  0,
};
```

`tick_interval_ms` defines the default host-scheduled cadence for the optional
`tick` callback. It must be between 33 and 1000 ms. Use
`NATIVE_EXTENSION_TICK_INTERVAL_DEFAULT_MS` for ordinary widgets. A button's
External Widget configuration can override this default with
`extension_tick_interval_ms`, allowing the same package to run at an
appropriate cadence for each placement.

The loader requires the descriptor, then verifies its ID and package version
against the filename as well as its ABI and target ABI against firmware. Package
semantic versioning is independent from the firmware ABI: use
`flight-radar@1.2.0.elf`, not an ABI-derived version, when an ABI rebuild
does not itself introduce a major package behavior change.

## Lifecycle

Export these C functions from every Extension:

```cpp
extern "C" void native_extension_create_instance(
    const NativeExtensionHostApi* host,
  void* extension_context,
    uint32_t instance_id,
    void* root,
    const char* config_json);

extern "C" void native_extension_destroy_instance(
    const NativeExtensionHostApi* host,
  void* extension_context,
    uint32_t instance_id);

  extern "C" void native_extension_shutdown(
    const NativeExtensionHostApi* host,
    void* extension_context);
```

`create_instance` runs on the LVGL task. `root` is an LVGL object confined to
the button. Create child LVGL objects under it. `destroy_instance` runs before
the host deletes the root and should release only resources owned by the
Extension.

The same Extension can have multiple instances. `instance_id` is stable for a
pad/button placement during its lifetime. The per-button configuration field is
passed as `config_json`; it is a text payload limited to 511 bytes.

`extension_context` is an opaque, loader-owned handle shared by every instance
of a package. Use `context_get_data` and `context_set_data` to associate one
host-allocated service state with the package. This is the supported location
for mutable state because a native ELF is flash-mapped and should not rely on
writable globals.

`shutdown` is required. It runs on the Arduino main loop after the
final widget is destroyed and every package worker has returned. Release
package-owned service state here, then clear it with
`host->core->context_set_data(extension_context, nullptr)`. It must not call
LVGL.

## Events

These optional exports receive button events:

```cpp
extern "C" NativeExtensionEventResult native_extension_on_tap(
    const NativeExtensionHostApi* host, void* extension_context, uint32_t instance_id);

extern "C" NativeExtensionEventResult native_extension_on_long_press(
    const NativeExtensionHostApi* host, void* extension_context, uint32_t instance_id);
```

Return `NATIVE_EXTENSION_HANDLED` to suppress the normal button action list, or
`NATIVE_EXTENSION_PASS_THROUGH` to allow it. The advanced sample demonstrates
both policies.

An Extension may also export
`native_extension_tick(host, extension_context, instance_id)`. The
firmware schedules it no more frequently than the descriptor's validated
`tick_interval_ms` value for an active Extension widget. Use it only for
lightweight periodic UI updates; do not perform blocking I/O.

`create_instance`, `destroy_instance`, event callbacks, and `tick` run on the
LVGL task. Only these callbacks may manipulate the LVGL objects supplied by the
host API. An Extension worker task may block and make network requests, but it
must never call an LVGL helper. Copy worker results into fixed state, then read
that state from `tick` to update the UI.

The current ABI permits one host-managed worker per package. When the final widget is
destroyed, firmware requests cancellation and wakes an interruptible worker
wait. Use `host->task->task_cancel_requested(extension_context)` in worker
loops and `host->task->task_wait_or_cancel(extension_context, timeout_ms)` for
delays. The main loop waits up to 20 seconds for return, accommodating the
current 15-second HTTP limit. A worker that misses the deadline is marked as an
error and remains mapped; firmware never force-deletes it or frees its state.
When a pad save immediately recreates the same extension widget, the widget
shows `Extension restarting` and retries automatically after the prior worker
has joined. Keep worker waits cancellation-aware so this transition is prompt.

## Host API

`NativeExtensionHostApi` is the current ABI surface. Extension packages cannot
directly import firmware symbols, so it provides grouped C-style service views:

* `host->core` — time, allocation, math, mutexes, logging, notifications, status
* `host->task` — extension worker creation
* `host->http` — bounded HTTP GET
* `host->ui` — opaque LVGL objects, labels, layout, styling, and events
* `host->canvas` — RGB565 canvas allocation, drawing, sprite blitting, and text
* `host->binding` — on-demand read-only binding-template resolution

These grouped services are the complete extension API. The portal displays the
descriptor title, target ABI, and package runtime status.

### RGB565 Sprite Blitting

`host->canvas->canvas_blit_rgb565(canvas, x, y, pixels, source_width,
source_height, destination_width, destination_height)` copies a source RGB565
sprite into an exact destination rectangle with nearest-neighbor scaling. The
source buffer is caller-owned and must remain readable for the call. The host
clips the destination to the canvas, so callers can draw partially off-screen
sprites without manual clipping.

Use this for frequently redrawn pixel-art sprites. It avoids one canvas API call
per source pixel and is preferable to composing a sprite from many fill
rectangles in a tick callback.

### Binding Resolution

Call `host->binding->resolve(extension_context, instance_id, template, out,
out_size)` from `create`, event callbacks, or `tick` to resolve the current
value of any existing binding template. The resolver uses the widget instance's
own pad context, so `[pad:name]` works as well as `[mqtt:...]`,
`[health:...]`, `[time:...]`, `[expr:...]`, `[timer:...]`, `[list:...]`, and
`[net:...]`.

Resolution is on demand, not a one-time substitution. Resolve in `tick` at an
extension-chosen cadence, cache the previous result, and update LVGL only when
it changes. The service must never be called from an extension worker; resolve
on the LVGL task and copy values into synchronized extension state when a
worker needs them. A true return means the call context was valid; normal
binding output such as `---` or `ERR:...` is still written to `out`.

The host's HTTP helper supports plain HTTP and HTTPS. HTTPS currently calls
`setInsecure()` and does not verify certificates. Use it only for trusted,
low-risk integrations until certificate validation is added.

The exposed LVGL surface matches the controls enabled by the firmware build.
Use generic clickable objects for button-like controls. When a future Extension
needs an LVGL feature not in this table, add a host wrapper, rebuild the
firmware, and rebuild all installed Extensions.

Canvas buffers are owned by the Extension and must use RGB565. Allocate them
with `host->core->alloc(host->canvas->canvas_buffer_size(width, height))`, then
release them with `host->core->free`. Use
`host->canvas->canvas_draw_text()` to rasterize a firmware font directly into a
canvas. Font names are `default`, `dseg7`, `bebas`, and `doto`; unknown names
fall back to `default`.

`canvas_fill_rect` fills an axis-aligned RGB rectangle efficiently and is the
preferred primitive for block-based animations. Use `canvas_set_pixel` only for
individual pixels and `canvas_draw_line` or `canvas_draw_circle` for vector
visuals.

`canvas_invalidate_rect` schedules only a clipped canvas region for LVGL redraw.
Use it after direct buffer writes when an Extension repaints selected dynamic
regions. `canvas_clear` still invalidates the full canvas for simple Extensions.
High-frame-rate Extensions should preserve static pixels in their buffer, repaint
only changed scene regions, and call `canvas_invalidate_rect` for the old and
new bounds of every moving object.

### Animation Performance Lessons

The following practices were established while validating animated Extension
canvases on the ESP32-P4:

* Design for the real button size and aspect ratio. An RGB565 canvas requires
  `width * height * 2` bytes, so a full canvas is not a free abstraction.
* Use `canvas_clear` for static or infrequent updates only. It repaints and
  invalidates the full canvas.
* Preserve static pixels for continuous motion. Repaint only the regions
  affected by the previous and new positions, then invalidate those bounds.
* Include both old and new bounds in damage tracking. Omitting the old bounds
  leaves stale pixels visible as motion trails.
* Match the descriptor tick interval to visible motion. Higher rates increase
  canvas work and display flushing even when no scene state changed.
* Cache binding results, button colors, and label text. Update labels and
  invalidate canvas regions only when their visible values change.
* Prefer a small number of large RGB forms and bounded fills over dense
  per-pixel drawing. `canvas_fill_rect` is the efficient primitive for
  axis-aligned color regions.
* Keep performance diagnostics opt-in. Timing, logging, and counters affect
  the workload being measured.

### Button Snapshot

`host->button->get(extension_context, instance_id, &snapshot)` returns a
read-only snapshot of the owning button's resolved appearance and visible label
text. It is available only from Extension lifecycle, event, and tick callbacks.

```cpp
NativeExtensionButtonSnapshot snapshot = {};
if (host->button->get(extension_context, instance_id, &snapshot)) {
  // snapshot.background_rgb
  // snapshot.foreground_rgb
  // snapshot.label_top / label_center / label_bottom
}
```

Use button background and foreground colors for Extension styling. The labels
are the current resolved display text, including bindings. Extension JSON should
remain limited to Extension-specific behavior rather than duplicate button
appearance or label configuration.

Extension-owned, pure C/C++ source dependencies may be compiled into a package
alongside its entry source, provided the final ELF has no relocations and fits a
slot. Do not link directly to firmware-owned platform libraries such as LVGL,
Wi-Fi, HTTPClient, ArduinoJson, or ESP-IDF services; use the host API instead.

Use the LVGL host API only from lifecycle and event callbacks. Do not retain
host function pointers or LVGL object pointers across firmware updates.
Extensions run as trusted native code: a bad pointer or blocking operation can
still crash the device.

## Tips

* Keep extensions small and use the small slots whenever possible.
* Do not use relocations, dynamic linking, exceptions, RTTI, or standard-library
  helpers that introduce unresolved runtime symbols.
* Treat string literals as read-only data. Use stack buffers for transient
  formatted text.
* Do not make extension-created objects clickable unless the Extension also
  deliberately handles LVGL input. The host button receives the normal tap and
  long-press dispatch.
* Use `host->log_info` while developing. The firmware logs instance creation and
  event disposition under the `Extensions` tag.

## Samples

`extensions/hello-world` is the minimum create/destroy implementation.
`extensions/advanced-sample` demonstrates lifecycle logging, a visible
handled-tap notification, pass-through long press, and on-demand
`[health:cpu]` resolution in `tick` to display live device CPU usage.
`extensions/nixie-clock` demonstrates palette-RLE RGB565 artwork embedded in
the package, per-instance PSRAM sprite decode buffers, a dark aspect-fit canvas
layout, and a timezone-aware `time` binding that supports either four or six
clock digits.
`extensions/brick-breaker-clock` demonstrates a self-playing canvas game with
embedded RGB565 arcade sprites, a timezone-aware `time` binding, and a
responsive time-brick layout that scales to the owning button.
`extensions/flight-radar` is a stateful example: it shares one fixed-buffer
ADSB.lol polling worker across active widgets, uses `config_json` for location
and range, and renders a radar canvas plus labels. `interval` is an optional
refresh period in seconds (1-3600, default 10). Its HTTPS requests are insecure
under the current ABI policy.

The radar worker supports up to four distinct scan configurations at once. A
widget attaches to a scan slot by its complete normalized configuration; widgets
with identical values share a snapshot, while different values receive separate
snapshots. Due scans are fetched sequentially by the one package worker. The
configuration must be valid JSON: use commas between every property, not
semicolon-delimited settings.

```json
{"lat":50.901389,"lon":4.484444,"range_km":25,"max_planes":20,"interval":5}
```

For example, a second widget can independently scan Brussels:

```json
{"lat":50.9014,"lon":4.4844,"range_km":15,"max_planes":20,"interval":2}
```