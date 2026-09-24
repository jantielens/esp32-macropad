---
description: "Use when adding or changing button actions, built-in action types, device-class actions, action parsing, dispatch, or the action catalog. Covers registration and Arduino compilation."
applyTo: "src/app/actions/**, src/app/actions.cpp, src/app/action_*.cpp, src/app/pad_config.h"
---

# Action Type Conventions

`ActionTypeDef` is the contract for parsing, serialization, dispatch, availability,
authoring validation, bindings, and portal/MCP catalog metadata. Keep action-specific
behavior in a self-registering module, not in `action_parse.cpp` or
`action_dispatch.cpp`.
Read `src/app/action_registry.h` for callback signatures and a neighboring
action module for implementation patterns.

## Built-in Actions

1. Add the persisted action type and payload to `src/app/pad_config.h`.
2. Implement parse, serialize, dispatch, validation, binding handling, and
   `describe()` metadata in `src/app/actions/<type>_action.cpp`.
3. Call `DEFINE_AND_REGISTER_ACTION_TYPE(...)` exactly once in that module.
4. Include the module in `src/app/actions/action_modules.inc`. Arduino compiles
   sketch-root `.cpp` files, not modules in subdirectories; sketch-root
   `src/app/actions.cpp` includes this manifest.
5. Run `tests/test_action_catalog_completeness.sh` and the relevant action tests.

For device-class actions, keep the module under
`src/app/device_classes/<class>/`, register it once, and include it through
the device-class aggregation path. Do not add it to the built-in manifest.
See `docs/dev/adding-a-device-class.md` for the full device-class checklist.
