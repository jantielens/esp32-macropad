---
title: Native Extensions
description: Developer guide for creating and installing ESP32-P4 native Extensions
ms.date: 2026-08-12
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

Upload a relocation-free ELF named `<extension-id>@<package-semver>.elf`. The ID uses
lowercase letters, digits, and hyphens. The portal stages the upload on the
configured storage backend; the next boot validates and commits it into the
selected executable flash slot.

## Build

```bash
bash tools/build-p4-extension.sh extensions/hello-world/hello_world.cpp build/extensions/hello-world@1.0.0.elf
```

The build script rejects ELF files containing relocations. Rebuild and upload
every Extension after a firmware update that changes
`NATIVE_EXTENSION_ABI_VERSION`. ABI 8 is greenfield: older Extension packages
are intentionally unsupported.

The extension build links a tiny freestanding runtime that provides `memcpy`
and `memset`; it does not link the Arduino or C++ standard-library runtimes.
It uses the ESP32-P4 `ilp32f` floating-point ABI, matching firmware callbacks
that exchange `float` values. The builder derives target flags and ABI metadata
from the installed ESP32-P4 SDK and `native_extension_api.h`; do not supply
your own `-march` or `-mabi` flags.

Every ABI 8 package must export this fixed, pointer-free descriptor:

```cpp
extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
  NATIVE_EXTENSION_DESCRIPTOR_MAGIC,
  NATIVE_EXTENSION_ABI_VERSION,
  NATIVE_EXTENSION_TARGET_ABI,
  "example-id",
  "1.0.0",
  "Example Extension",
};
```

The loader requires the descriptor, then verifies its ID and package version
against the filename as well as its ABI and target ABI against firmware. Package
semantic versioning is independent from the firmware ABI: use
`flight-radar@1.2.0.elf`, not `flight-radar@8.0.0.elf`, when an ABI 8 rebuild
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

`shutdown` is required in ABI 8. It runs on the Arduino main loop after the
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
firmware calls it no more than once every 250 ms for an active Extension widget.
Use it only for lightweight periodic UI updates; do not perform blocking I/O.

`create_instance`, `destroy_instance`, event callbacks, and `tick` run on the
LVGL task. Only these callbacks may manipulate the LVGL objects supplied by the
host API. An Extension worker task may block and make network requests, but it
must never call an LVGL helper. Copy worker results into fixed state, then read
that state from `tick` to update the UI.

ABI 8 permits one host-managed worker per package. When the final widget is
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

`NativeExtensionHostApi` is the ABI 8 surface. Extension packages cannot
directly import firmware symbols, so it provides grouped C-style service views:

* `host->core` — time, allocation, math, mutexes, logging, notifications, status
* `host->task` — extension worker creation
* `host->http` — bounded HTTP GET
* `host->ui` — opaque LVGL objects, labels, layout, styling, and events
* `host->canvas` — RGB565 canvas allocation and drawing

The direct fields remain as ABI 8 source-compatibility helpers for early
packages, but new extensions should use grouped services. The portal displays
the descriptor title, target ABI, and package runtime status.

The host's HTTP helper supports plain HTTP and HTTPS. HTTPS currently calls
`setInsecure()` and does not verify certificates. Use it only for trusted,
low-risk integrations until certificate validation is added.

The exposed LVGL surface matches the controls enabled by the firmware build.
Use generic clickable objects for button-like controls. When a future Extension
needs an LVGL feature not in this table, add a host wrapper, rebuild the
firmware, and rebuild all installed Extensions.

Canvas buffers are owned by the Extension and must use RGB565. Allocate them
with `alloc(canvas_buffer_size(width, height))`, then release them with `free`.
Canvas text is normally represented with labels layered above the canvas, which
keeps the draw API small while still allowing custom visualizations.

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
`extensions/advanced-sample` demonstrates configuration rendering, lifecycle
logging, a visible handled-tap notification, and pass-through long press.
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
{"lat":51.2189473,"lon":5.4216694,"range_km":25,"max_planes":20,"interval":5}
```

For example, a second widget can independently scan Brussels:

```json
{"lat":50.9014,"lon":4.4844,"range_km":15,"max_planes":20,"interval":2}
```