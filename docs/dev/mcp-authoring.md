---
title: MCP authoring and asynchronous contracts
description: Preserve identity, replacement, tool metadata, and completion semantics without duplicating the live tool catalog.
---

[Client setup and safe operation](../user/integrations/ai-assistants.md) is the
user entry point. [MCP transport](web-portal.md#mcp-server-api) owns HTTP/JSON-RPC
details. The registry's current tool schemas are authoritative, not a static
pad/configuration JSON catalog.

## Identity and gates

[Device identity](../../src/app/mcp_device_identity.cpp) supplies immutable
device_id, independent of hostname/client namespace.
Protected writes compare expected_device_id before mutation.
Read/control/authoring permission checks must remain consistent between
tools/list and tools/call. Missing capabilities remove tools rather than
creating usable stubs.

MCP bearer authentication is independent from portal Basic Auth.
AP mode, Origin rejection, body caps, and unavailable/busy dispatch retain
their transport errors; do not convert them into success-shaped results.
Screenshots remain a separate authenticated binary REST response.

## Metadata and replacement

[Tool registry](../../src/app/mcp_tool_registry.cpp),
[pad tools](../../src/app/mcp_tools_pads.cpp), and
[component adapter](../../src/app/mcp_components.cpp) own tool schemas and
component exposure.
Action/scheme metadata comes from production registrations, not handwritten
cross-board catalogs.

Read a component before a full-replacement write and preserve unrelated fields.
Timer components store per-slot expire_actions; a countdown snapshots them
when it starts, including Toggle/Resume of a prepared countdown.
The component's exists indicator describes physical configuration-file
existence, not whether normalized content is nonempty.
Concurrent portal/MCP pad saves remain last-write-wins.

## Home Assistant completion

[Press dispatch](../../src/app/mcp_press_button.cpp) keeps ordinary lists
synchronous. Eligible lists containing HA requests reserve tracked execution,
enqueue actions, and return an execution_id while requests remain pending.
get_ha_execution_result reports pending or completed with results indexed by
the original action_index, including entity/service, status, HTTP status,
duration, and error details as applicable.

Terminal results can include service errors and skipped later actions, not
merely success. Four execution records are retained for roughly 60 seconds.
An expired/missing record does not authorize replay of a possibly executed
command. Pausable confirmation/continuation lists use ordinary dispatch and do
not enter this tracked path.
Admission, HTTP completion, and real-world external effect are separate events.

## Alarm queue and durability

Alarm tools expose scheduling readiness, active session, pending/completed
commands, command errors, save_state, and storage/OTA deferral.
alarm_control configuration uses slot 1; active snooze/cancel can use ID 0.
Queued response fields queued:true/persisted:false acknowledge admission,
not per-client durable completion.

The queue has four positions and processes one command per loop without
coalescing. Substantive live edits defer save/verification until ten seconds
after the latest change; unchanged edits do not extend it.
Failed saves retry and OTA defers writes. completed_commands counts processing,
not persistence. Explicit component writes are immediately durable and can
flush pending live edits.

Read current component metadata before replacement: definition fields and
lateness_minutes share validation with the portal/manager.
Occurrence records, one-shot instants, and session state have distinct
persistence semantics. User-facing recovery and behavior stay in the
[alarm topic](../user/device/alarms.md); implementation authority is the
[alarm manager](../../src/app/alarm_manager.cpp).

## Remote input verification

tap_screen queues a single native-coordinate tap for display-task dispatch;
it does not synchronously certify delivery, bypass wake/physical release,
or simulate arbitrary drag/hold.
Use current screen metadata and a fresh image, then read/inspect the resulting
state. A framebuffer does not prove physical panel presentation.
