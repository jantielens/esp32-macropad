---
title: Native Extensions
description: Developer guide for creating and installing ESP32-P4 native Extensions
ms.date: 2026-08-11
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

Upload a relocation-free ELF named `<extension-id>@<version>.elf`. The ID uses
lowercase letters, digits, and hyphens. The portal stages the upload on SD; the
next boot validates and commits it into the selected executable flash slot.

## Build

```bash
bash tools/build-p4-extension.sh extensions/hello-world/hello_world.cpp build/extensions/hello-world@1.0.0.elf
```

The build script rejects ELF files containing relocations. Rebuild and upload
every Extension when `NATIVE_EXTENSION_ABI_VERSION` changes.

## Lifecycle

Export these C functions from every Extension:

```cpp
extern "C" void native_extension_create_instance(
    const NativeExtensionHostApi* host,
    uint32_t instance_id,
    void* root,
    const char* config_json);

extern "C" void native_extension_destroy_instance(
    const NativeExtensionHostApi* host,
    uint32_t instance_id);
```

`create_instance` runs on the LVGL task. `root` is an LVGL object confined to
the button. Create child LVGL objects under it. `destroy_instance` runs before
the host deletes the root and should release only resources owned by the
Extension.

The same Extension can have multiple instances. `instance_id` is stable for a
pad/button placement during its lifetime. The per-button configuration field is
passed as `config_json`; it is a text payload limited to 511 bytes.

## Events

These optional exports receive button events:

```cpp
extern "C" NativeExtensionEventResult native_extension_on_tap(
    const NativeExtensionHostApi* host, uint32_t instance_id);

extern "C" NativeExtensionEventResult native_extension_on_long_press(
    const NativeExtensionHostApi* host, uint32_t instance_id);
```

Return `NATIVE_EXTENSION_HANDLED` to suppress the normal button action list, or
`NATIVE_EXTENSION_PASS_THROUGH` to allow it. The advanced sample demonstrates
both policies.

## Host API

`NativeExtensionHostApi` provides a small, versioned surface:

* Create and update labels under the supplied root
* Center an LVGL object
* Write an informational device log entry
* Show a short device notification bubble

Use the host API only from lifecycle and event callbacks. Do not retain host
function pointers or LVGL object pointers across firmware updates. Extensions
run as trusted native code: a bad pointer or blocking operation can still crash
the device.

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