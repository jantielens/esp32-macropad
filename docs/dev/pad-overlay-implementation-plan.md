---
title: Pad Overlay Implementation Plan
description: Implementation contract for one shared pad with Main and Overlay button layers.
ms.date: 2026-10-23
---

## Status And Scope

Approved design, ready for implementation. This plan does not implement the
feature. Follow repository instructions and read current source before editing;
the worktree and preferred verification board may have changed since planning.

Implement one pad with a Main layer and one Overlay layer. Each layer contains
normal buttons, including widgets, and uses the same editor. Per-button Button
State controls visibility and interaction. Overlay buttons cover Main content;
they do not replace, rebuild, or suspend the underlying buttons.

Prioritize KISS and DRY. This is greenfield work: breaking existing configuration
or devices is acceptable. Do not add migrations, legacy-format readers, aliases,
dual implementations, version switches, or compatibility branches. Existing
ordinary pads without overlay buttons remain valid because overlays are optional,
not because of a compatibility mechanism.

Do not implement multiple overlays, binding-driven stacking order, a separate
overlay pad or screen, layer visibility state, new actions, input pass-through
states, configurable failure-state settings, or automatic threshold hysteresis.
Do not commit, push, flash hardware, or modify unrelated work.

## Approved Behavior

### Layer And State Contract

* Draw Main first and Overlay second, using the existing LVGL display buffers.
* Evaluate each button's Button State independently, using existing binding polling.
* Share grid dimensions, track sizes, spacing, insets, background, named bindings,
  wake behavior, and pad settings across the two layers.
* Reject button overlap within each layer. Allow overlap between layers.
* Enforce one combined limit: Main count plus Overlay count must not exceed
  `MAX_PAD_BUTTONS`, currently 64 by default and overridable per board.
* Keep Main widgets and data updates running while covered. Hidden objects remain
  allocated; hiding is not a memory reclamation mechanism.

| State | Visible | Can execute its actions | Blocks underlying input |
| --- | --- | --- | --- |
| `enabled` | Yes | Yes | Yes |
| `disabled` | Yes | No | Yes |
| `hidden` | No | No | No |

An enabled button with no actions still blocks underlying input. Blocking covers
the button rectangle, including transparent-looking areas. Shadows and decorative
overflow do not extend that input rectangle. Empty Overlay positions do not block.
Preserve current disabled appearance and widget lifecycle behavior unless a
focused test proves it conflicts with this contract.

An empty Button State means `enabled` on either layer. For a nonempty state:
accept the three exact state values; unresolved, unavailable, empty resolved, or
invalid results fall back to `enabled` on Main and `hidden` on Overlay. Use the
resolver's status API rather than interpreting placeholder text as valid data.
Resolve initial states before the first visible frame or input opportunity.

### Input Safety

* Touches in a visible Overlay rectangle must not invoke Main actions, widget
  interactions, long presses, swipes, or full-pad background actions beneath it.
* Touches outside visible Overlay rectangles retain normal Main behavior, even
  when only part of a Main button is covered.
* Physical-button navigation skips an entire Main button when its rectangle
  intersects any visible Overlay button, including a disabled one. Overlay
  buttons follow normal action/widget navigation eligibility.
* A visibility transition cancels an in-flight interaction if its target becomes
  hidden, disabled, or covered at the active contact. Do not transfer a held press
  or release to another button. Require a fresh press before another action.
* Invalidate covered navigation focus when visibility changes. Audit touch,
  synthetic navigation, raw widget input, gesture propagation, and pad-wide action
  routing; do not assume LVGL disabled flags provide input blocking.
* Keep existing device dialogs above both pad layers. Define pad-wide actions as
  background actions below visible Overlay buttons, without inventing a new modal
  overlay policy for the whole pad.

Use existing cancellation and input-generation mechanisms. Prefer the existing
LVGL hit-test path with correct object ordering and event guards. Add a dedicated
input shield only if necessary to meet and test the contract; do not build a
second event dispatcher or a per-position winner table.

### Editor Experience

* Add an accessible Main / Overlay segmented control at the top left of the pad
  editor. Main is initially selected; selection is editor state, not persisted.
* Reuse the grid, inspector, widget controls, actions, bindings, icons, colors,
  resize, drag/drop, and validation code for the selected layer.
* While editing Overlay, render Main dimmed beneath it and prevent Main selection.
  Do not dim the actual device output or mutate Main appearance settings.
* Shared pad controls remain editable in Main and read-only in Overlay. Do not
  create another settings object, pad identity, or named-binding scope.
* Maintain separate layer-qualified selection, form drafts, and error identities.
  Identical Main and Overlay coordinates must not overwrite each other's drafts.
* Switching layers must preserve edits and existing transaction semantics. It
  must not save, discard, or silently commit an invalid inspector draft.
* Provide an editor-only combined preview, reusing existing preview rendering.
  Reuse available resolved values; when bindings are unavailable, allow an
  editor-only state override instead of pretending to evaluate live device data.
* Empty-slot creation and occupancy checks apply to the selected layer. The
  combined capacity remains visible and is enforced across layers.
* Save, reload, import/export, clone, clear, and reuse operations must preserve
  both layers unless explicitly targeted at one layer. Verify desktop and mobile.

## Configuration And Runtime Representation

Persist one pad object, with `buttons` for Main and optional `overlay_buttons`:

```json
{
  "layout": "grid",
  "cols": 6,
  "rows": 8,
  "buttons": [
    {"col": 3, "row": 4, "label_center": "Heating"}
  ],
  "overlay_buttons": [
    {
      "col": 3,
      "row": 4,
      "label_center": "High power",
      "btn_state": "[expr:[pad:high_power]?\"enabled\":\"hidden\"]"
    }
  ],
  "bindings": {"high_power": "0"}
}
```

Use one exact-size trailing `ScreenButtonConfig` allocation in `PadConfig` and
the existing fixed-capacity runtime arrays. Keep Main entries before Overlay
entries and record one Main count or Overlay start boundary. Total button count
includes both layers. Avoid a per-button layer field where a boundary suffices;
do not duplicate `PadConfig`, `PadScreen`, binding arrays, or font ownership.

Reuse one button parser, validator, and serializer path for both JSON collections.
Capacity and grid checks must reject invalid input rather than truncate it.
Missing overlay collection means empty; malformed collections must be rejected.
Layer-local overlap rejection is an intentional tightening of current firmware
validation, which presently permits overlaps. Do not preserve that exception.

All button identities that can be ambiguous must include the layer, including
editor keys and API/MCP authoring targets. Use `main` and `overlay` values for a
layer selector, with omitted selector meaning Main. Array positions are local to
the selected layer; grid coordinates are not array positions. Combined limits
still apply. Reject invalid layers. Do not add parallel overlay-specific tools.
Keep read output, schemas, capabilities, and error messages consistent with this
contract. Review existing button event payloads for coordinate ambiguity and
expose layer identity where needed; no duplicate legacy event format.

Keep template inheritance Main-only for v1: inherit source Main buttons into
empty destination Main positions. Do not inherit source Overlay buttons or let
local Overlay occupancy suppress Main inheritance. Include local Overlay buttons
in the combined limit and reject overflow instead of silently dropping buttons.
Shared bindings retain the existing template semantics. Whole-pad copy/export
retains both layers; selected-layer block operations affect that layer only.

Both layers must participate in widget registration, data-stream snapshots, image
fetching, font ownership, icon retirement, rebuild, clone, eviction, and cleanup.
Audit consumers of button count and coordinate-based lookup, especially duplicate
coordinates across layers. Preserve one owning pad binding context.

## Memory Budget And Stop Gate

Additional internal RAM above 1 KiB requires explicit user review before accepting
the design. Treat 1 KiB as a gate, not a target, and aim for negligible fixed
internal growth. Prefer existing PSRAM-first allocation paths for feature-owned
dynamic storage. This does not authorize changing the global allocator or silently
exhausting internal RAM through fallback.

* Keep `MAX_PAD_BUTTONS` and all runtime array capacities unchanged.
* Keep added persistent metadata constant-size per pad; account for padding and
  multiplication across cached pads. Report `sizeof` changes for affected structs.
* Do not introduce a framebuffer, saved screenshot, per-pixel coverage mask,
  second task, larger task stack, or large local arrays for this feature.
* Compute rectangle intersections on demand over the existing bounded arrays.
  Reuse layout results and binding change detection; do not cache a winner grid.
* Reuse current PSRAM-first config and LVGL allocators. Test failure paths for any
  new allocation. If an existing fallback can add over 1 KiB internally in the new
  path, report it and seek review rather than claiming PSRAM makes it harmless.

Measure static internal data/BSS and runtime internal free heap, minimum free
heap, and largest block separately from PSRAM. Compare a baseline pad with the
same total buttons all in Main against its two-layer equivalent to isolate layer
overhead. Also measure the practical scenario of adding a warning to a pad, making
the added-content cost explicit. Sample steady state and peak construction,
rebuild, visibility changes, navigation, and repeated load/eviction cycles.

Record board, feature flags, button counts, and measurement limitations. A firmware
size report or host `sizeof` result alone does not establish runtime internal heap
use. Hardware measurements require user-authorized access; do not flash to obtain
them. If measurements cannot be collected, mark the memory gate unverified and
do not report implementation as fully verified.

## Implementation Sequence

### Phase 1: Data Contract And Authoring Interfaces

Inspect and update the owning paths in
[pad_config.h](../../src/app/pad_config.h),
[pad_config.cpp](../../src/app/pad_config.cpp),
[pad_validate.cpp](../../src/app/pad_validate.cpp),
[web_portal_pad.cpp](../../src/app/web_portal_pad.cpp), and
[mcp_tools_pads.cpp](../../src/app/mcp_tools_pads.cpp).
Follow dependent snapshot, template, event, and block-reuse consumers locally.

Implement flat allocation plus layer boundary, shared parsing/validation,
layer-aware mutations, round trips, combined capacity, and Main-only inheritance.
Update runtime schemas and capability metadata with the contract, not at the end.

Extend existing config and validation tests first. Cover same coordinates across
layers, rejection within one layer, combined capacity boundaries, invalid layer
or collection, ownership/clone cleanup, replacement at capacity, and template
merge limits. Run the smallest focused check immediately after the first edit.

### Phase 2: Rendering, State Resolution, And Input

Reuse [pad_tile_builder.cpp](../../src/app/screens/pad_tile_builder.cpp),
[pad_screen_poll.cpp](../../src/app/screens/pad_screen_poll.cpp),
[pad_screen_events.cpp](../../src/app/screens/pad_screen_events.cpp), and
[pad_screen.h](../../src/app/screens/pad_screen.h).
Build both collections in fixed order through the same path. Ensure shadows,
focus indicators, and tap feedback do not escape the agreed stacking order.

Add layer-specific invalid-state defaults, correct input blocking, contact
cancellation, and navigation exclusion without a dynamic winner model. Reuse
existing lifecycle hooks and widget contracts. Visibility transitions must not
rebuild the pad or allocate persistent resources.

Extend an existing real-LVGL/input fixture if suitable. If no fixture covers this
boundary, justify one focused test target and register it in the normal suite.
Do not emulate rendering with source-spelling assertions or stripped-source
harnesses. Check pixels, hit targets, actions, and navigation outcomes.

### Phase 3: Shared Editor And Preview

Use [pad-editor.fragment.html](../../src/app/web/pad-editor.fragment.html),
[portal_pad_editor.js](../../src/app/web/portal_pad_editor.js),
[portal_pad_workspace.js](../../src/app/web/portal_pad_workspace.js),
[portal_pad_grid.js](../../src/app/web/portal_pad_grid.js),
[portal_pad_dialog.js](../../src/app/web/portal_pad_dialog.js), and
[portal_pad_io.js](../../src/app/web/portal_pad_io.js), plus existing preview,
icons, block, and CSS owners as required. Do not duplicate an editor or add a
frontend framework. Use one selected-layer accessor and layer-qualified keys
where they remove actual duplication.

Extend existing portal tests for dirty state, same-coordinate independent edits,
invalid drafts across switches, inactive-layer preservation, import/export,
occupancy, block operations, combined limits, and preview composition. Update
device-free fixtures only as needed to exercise the production UI.

### Phase 4: Verification And Documentation Handoff

Reconcile the completed aggregate diff, not only the last phase. Update existing
owners [editing.md](../user/pads/editing.md),
[recipe-authoring.md](../user/pads/recipe-authoring.md),
[reuse.md](../user/pads/reuse.md), and
[web-portal.md](web-portal.md) where affected. Update the root API table only if
its published contract changes. Read the user-documentation instructions before
those edits. Keep examples synthetic and exclude credentials from pasted inputs.

Update generated metadata/fixtures through existing generators if their source
changes. No new narrative documentation files are needed for implementation.
Report behavior, memory evidence, test results, and pending manual checks. Do not
declare ready to commit while required verification is unperformed.

## Verification Gates

Use existing suites before adding permanent coverage:

```bash
cmake -S . -B build/host-tests
cmake --build build/host-tests --parallel --target \
  test_pad_config_dynamic_layout test_pad_confirmation test_pad_layout
ctest --test-dir build/host-tests --output-on-failure -R 'test_pad_config_dynamic_layout|test_pad_confirmation|test_pad_layout'
node tests/test_portal_pad_dirty.js
node tests/test_portal_pad_dialog_transaction.js
node tests/test_portal_pad_import.js
node tests/test_portal_pad_preview.js
```

Confirm current registered names with CTest before extending the command set.
Run additional focused targets for touched runtime/input, widget, binding,
data-stream, API/MCP, and lifecycle contracts. New permanent coverage must be
registered in [tests/CMakeLists.txt](../../tests/CMakeLists.txt).

The behavior matrix must include enabled, disabled, hidden, absent/invalid binding
values, two independent Overlay buttons, partial coverage, no-action warnings,
press-time visibility changes, physical navigation, widget input, and pad-wide
actions. Assert no underlying action leaks through a visible overlay.

Use the device-free portal server on a free port, such as:

```bash
python3 tools/portal-dev-server.py --port 8765
```

Verify the real editor with browser automation at desktop and mobile sizes:
layer switching, selection isolation, shared controls, combined preview, save and
reload, widget editing, and no overlapping controls or clipped labels. Run the
existing asset syntax/build checks for changed frontend modules.

Firmware changes require a single-board build. Read the current preference in
`/memories/board-preferences.md`; at planning time it is `esp32-p4-lcd4b`:

```bash
./build.sh esp32-p4-lcd4b
```

Do not run bare `./build.sh` or add another verification board without approval.
Review compilation warnings as well as errors. No firmware build is required
for writing this plan alone. Validate affected documentation links and Markdown.

## Completion Criteria

* A normal Main button is usable when its covering Overlay button is hidden.
* A visible enabled or disabled Overlay button prevents all underlying activation
  inside its rectangle without blocking unrelated content.
* Three configured Overlay buttons can independently resolve to one visible and
  two hidden buttons; no whole-layer state is required.
* Main and Overlay share one pad runtime, one grid, and the existing total limit.
* Editor and API operations cannot confuse equal coordinates across layers or
  lose the inactive layer's content.
* No compatibility machinery, duplicated render/editor paths, extra framebuffers,
  or feature-specific large internal allocations are introduced.
* Focused tests, browser checks, the preferred-board build, documentation checks,
  and the memory review gate have results or explicit outstanding blockers.
