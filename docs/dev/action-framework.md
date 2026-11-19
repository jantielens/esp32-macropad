---
title: Typed action framework
description: Why actions moved to typed operations and where to trace authoring, validation, and execution.
---

## Why actions changed

The old editor mixed action families, commands, targets, and values in
purpose-built forms. Those forms made fixed choices easy but often left no way
to pass a widget value or List item into the same action. The redesign keeps
labeled choices and resource pickers for fixed values and adds a **Dynamic**
textbox per eligible argument. The selected operation itself stays fixed.

The shared foundation and built-in migration landed in `7eb47e78`. The commit
that adds this reference completes the specialized and board-specific
migration and removes the legacy execution and editor paths.

## The shared contract

Actions persist a stable operation ID and typed arguments, for example
`{"op":"screen.open","args":{"target":{"$template":"[item:id]"}}}`.
Fixed arguments are native JSON values; only explicit `$template` arguments
are evaluated. One [firmware descriptor](../../src/app/action_registry.h)
defines argument types, limits, and execution policy. The
[catalog projection](../../src/app/action_catalog.cpp) exposes those
descriptors to the [portal editor](../../src/app/web/portal_action_editor.js)
and MCP discovery. Operation handlers stay with their subsystem owners.

The [parser](../../src/app/action_parse.cpp) and
[validator](../../src/app/action_validate.cpp) check the saved contract.
[Dispatch](../../src/app/action_dispatch.cpp) resolves and validates dynamic
results before calling the handler. Legacy action documents are rejected, not
silently translated. See [action authoring instructions](../../.github/instructions/action-types.instructions.md)
and the [user action guide](../user/pads/actions.md) for examples.

## Execution and troubleshooting

The [sequence runner](../../src/app/action_list.cpp) captures the widget
event, the activated List row, and originating pad bindings in an
[owned run context](../../src/app/action_run_context.cpp). Those inputs survive
navigation and a pause; variable and device bindings are read when each
operation runs. Argument and run storage use bounded allocations in PSRAM
on supported boards.

Only pausable operations reserve a
[continuation](../../src/app/action_continuation.cpp). Failure, cancellation,
or timeout stops the remaining operations; it does not undo an effect already
accepted. Check each handler's completion contract when diagnosing timing:
for example, acceptance of a Wi-Fi station request is not proof of a connection.
