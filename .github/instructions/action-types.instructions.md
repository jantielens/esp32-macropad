---
description: "Use when adding or changing button actions, built-in action types, device-class actions, action parsing, dispatch, or the action catalog. Covers registration and Arduino compilation."
applyTo: "src/app/actions/**, src/app/actions.cpp, src/app/action_*.cpp, src/app/pad_config.h"
---

# Action Type Conventions

`ActionTypeDef` registers typed operations, availability, execution policy,
and portal/MCP catalog metadata. The shared operation parser and validator
handle serialization and bindings. Keep operation handlers in their
self-registering modules, not in `action_parse.cpp` or `action_dispatch.cpp`.
Read `src/app/action_registry.h` and a neighboring action module for
implementation patterns.

## Typed operations

The pad format uses `action_format_version: 1` and actions shaped as
`{"op":"screen.open","args":{"target":"pad_1"}}`. Dynamic arguments use
`{"$template":"[item:id]"}`. Register built-in operations with the owning
action type and expose them through the shared catalog and editor. Shared
hosts validate, own, and snapshot operation arguments before dispatch.
Respect each host's execution and interactivity restrictions.
Do not translate legacy pad actions or silently rewrite incompatible files.
Keep operation descriptors with their registered action type, validate before
effects, and retain captured event, item, and originating pad data through
continuations. Variable and device bindings are resolved at execution time.
Boolean literals are JSON booleans; dynamic booleans accept only `true` and
`false`. Enum arguments use exact string values from their descriptor's choices;
provide `choice_labels` when fixed choices have user-facing labels.
Resolved booleans are passed to handlers as integer 0 or 1, and resolved enum
and string values as text. String arguments may explicitly allow an empty value
and specify additional text validation.
Operation IDs are not limited by the legacy inline type field; the stored
action retains the registered descriptor and uses `@` as its discriminator.

Operation descriptors accept up to `ACTION_OPERATION_MAX_ARGUMENTS` arguments.
String limits must be positive and no larger than `ACTION_OPERATION_MAX_TEXT_LENGTH`.
Template syntax must fit within `BINDING_TEMPLATE_MAX_LEN - 1` bytes.
The descriptor limit applies to both literals and resolved templates, up to
2,048 UTF-8 bytes for explicitly long-text operations. Individual binding
sources retain their own storage limits. Handlers
receive normalized arguments and a continuation token. Resolved text is valid
only during the handler call, so copy anything an asynchronous worker needs.
Declare pausable operations with `ACTION_EXECUTION_PAUSABLE`; synchronous
operations default to `ACTION_EXECUTION_SYNC` and do not reserve a continuation.
Use `variable_transaction` only for synchronous operations that must keep
variable resolution and assignment atomic; their handlers execute under the
display-then-variable lock order.
The runner reserves the suffix before invoking a pausable operation and rejects
the operation without starting its effect if reservation fails. Return
`ACTION_PENDING` only after accepting the token, and complete or cancel it
through the existing continuation machinery.

## Built-in Actions

1. Define stable operation IDs, argument descriptors, and normalized handlers
   in `src/app/actions/<type>_action.cpp`. Reuse the shared parser, storage,
   validation, and catalog projection.
2. Keep argument definitions in the descriptor and make the operation's
   completion condition explicit. Test actual handler dispatch and failures,
   not registration alone. See the [typed action framework](../../docs/dev/action-framework.md)
   for the shared execution contract.
3. Reject legacy action payloads; do not add a legacy parser or conversion adapter.
4. Call `DEFINE_AND_REGISTER_ACTION_TYPE(...)` exactly once in the module.
5. Include the module in `src/app/actions/action_modules.inc`. Arduino compiles
   sketch-root `.cpp` files, not modules in subdirectories; sketch-root
   `src/app/actions.cpp` includes this manifest.
6. Run `tests/test_action_catalog_completeness.sh` and the relevant action tests.

For device-class actions, keep the module under
`src/app/device_classes/<class>/`, register it once, and include it through
the device-class aggregation path. Do not add it to the built-in manifest.
See `docs/dev/adding-a-device-class.md` for the full device-class checklist.
