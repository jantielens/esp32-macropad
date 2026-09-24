---
title: ESP32 Macropad Copilot Instructions
description: Project orientation, build commands, and routing to task-specific contracts.
---

## Project

ESP32 firmware built with `arduino-cli` from `src/app/app.ino`. Default feature
flags live in `src/app/board_config.h`; each board overrides them in
`src/boards/<board>/board_overrides.h`. Use `HAS_*` capability gates and `IS_*`
device-class selectors as defined in the board instructions.

Arduino only compiles `.cpp` files at the sketch root. Code in subdirectories
must be included through the appropriate sketch-root aggregator or manifest;
otherwise registration can silently fail. Persistent file I/O uses the
`src/app/storage.h` facade, not direct LittleFS or SD_MMC calls.

## Keep It Simple

Understand the existing path and ask whether a change is necessary. Prefer
removing complexity or reusing an existing path before adding code; introduce
an abstraction only when it solves a present problem. Preserve correctness,
security, accessibility, and focused validation.

## Task-Specific Contracts

Read the relevant instruction before editing, including for cross-cutting work
that does not match a file's `applyTo` pattern:

* New button actions: [action type conventions](instructions/action-types.instructions.md)
* OTA and competing background work: [OTA activity](instructions/ota-activity.instructions.md)
* Board flags and driver selection: [compile-time flags](instructions/compile-time-flags.instructions.md)
* Display and touch: [display/touch](instructions/display-touch.instructions.md)
* Web components, routes, and UI: [web portal](instructions/web-portal.instructions.md)
* Configuration settings: [config settings](instructions/adding-config-settings.instructions.md)
* Binding schemes: [binding system](instructions/binding-system.instructions.md)
* Audio: [audio subsystem](instructions/audio.instructions.md)

For device-class wiring and branding, use the
[device-class guide](../docs/dev/adding-a-device-class.md) and
[build/release guide](../docs/dev/build-and-release-process.md).
The [MCP guide](../docs/mcp-guide.md) covers device control and pad authoring.

## Build And Tests

* `./build.sh <board>` builds one board; bare `./build.sh` builds all boards
  and must run only when explicitly requested.
* `cmake -S . -B build/host-tests`, then
  `cmake --build build/host-tests --parallel` and
  `ctest --test-dir build/host-tests --output-on-failure` run host tests.
* Choose focused checks from `tests/` for the changed subsystem and follow
  [build verification rules](instructions/agent-guidelines.instructions.md)
  before deciding whether a firmware build is needed.

See [README](../README.md) for setup and supported boards,
[scripts](../docs/dev/scripts.md) for build/upload/monitor commands, and
[WSL development](../docs/dev/wsl-development.md) for serial access.
