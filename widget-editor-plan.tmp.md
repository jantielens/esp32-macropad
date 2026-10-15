---
title: Widget Editor Ownership Implementation Plan
description: Temporary phased plan and execution tracker for issue 124.
ms.date: 2026-10-15
---

## Status

Phase 1 complete; D1..D5 approved through the decision interview (2026-10-15).
Phase 2 authorized and implemented (2026-10-15); focused checks and mock-portal
browser validation passed. Phase 3 decisions D6..D9 were approved through the
decision interview; the user subsequently authorized Phase 3 implementation,
now complete (2026-10-15), with verification recorded below.
The user approved a Phase 3 commit followed by the bounded Phase 3a ownership
follow-up below. Phase 3 is committed as `889698b`; Phase 3a is complete and
verified and committed as `7d25267`. Phase 4 decisions D10..D18 and implementation
are approved through the decision interview (2026-10-15). The user requested
implementation on a subsequent request, including continuation after interruption.
Phase 4 is now implemented and verified (2026-10-15), with the family checkpoints
below. Final portal acceptance is recorded under Phase 6.
Phase 4 is committed as `1f8ded9`. Phase 4b's single-manifest design and
implementation are approved (2026-10-15). Phase 4b is implemented and verified.
The user confirmed it works without errors and authorized its commit.
Phase 4b is committed as `fbd5d0f`. The Phase 5 decision interview is recorded
below (2026-10-15). After the initial record-and-stop interview, the user
approved Phase 5 implementation, including previous recommendations and restoring
Waveform's configurable controls through an owned editor rather than retiring
the partial. Phase 5 is implemented and verified; its firmware remains unchanged.
The user subsequently authorized committing the code and a bounded Phase 6
acceptance update. Phase 6 is complete for the agreed portal ownership scope,
with explicit limitations below. No flashing or runtime refactor is authorized.
Integration and verification evidence is below.
Use this document as the shared decision canvas; distinguish
observed behavior, proposed decisions, and approved scope. Update checkboxes and the
execution log as each phase is executed and verified.

Related: [issue 124](https://github.com/jantielens/esp32-macropad/issues/124).
This agreed scope supersedes the issue's original compatibility requirements.
This is a temporary tracking document, not permanent developer documentation.

## Goal

Adding a widget should require its own implementation, markup, any widget-only
styles, and explicit registration or manifest wiring, without modifying generic
editor orchestration.
Common field mechanics should work out of the box. Specialized behavior must
remain owned by the widget, not accumulate in a generic widget-exceptions file.

Clean ownership does not require identical implementations. The approximate
80 percent shared fields and 20 percent specialized code is a direction, not a
coverage target.

## Agreed Decisions

* Cover all active widget editors, migrating and validating incrementally.
* Introduce a small registry and a limited declarative field layer.
* Keep field declarations, defaults, bounds, and special behavior widget-owned.
* Reuse shared binding, color, action-list, and ordinary input helpers.
* Retain all currently editable settings; plan no JSON-only settings or new
  advanced JSON editing mechanism.
* Retain unsaved-change warnings, per-button drafts, focused validation errors,
  capability gates, and Content / Actions / Appearance tabs.
* Prefer predictable static groups over automatic disclosure or dynamic labels
  when those conveniences complicate the implementation.
* Allow stable shared capability or family concepts where useful. An individual
  widget exception in generic code requires a concrete justification and the
  user's explicit approval.
* Keep one Navigate to Screen action. Add a Binding value dropdown choice that
  reveals a binding input, replacing List-specific synthetic target options.
* Restore Waveform's owned portal editor and existing configurable controls;
  retain its active firmware without changing runtime behavior.
* Treat this work as greenfield: existing device configurations may break or
  lose settings and may require manual reconfiguration. Backward compatibility
  is not a requirement. Add no legacy readers, migrations, compatibility
  branches, or old-configuration preservation machinery.
* Group widget settings under a widget-owned object only if that materially
  reduces complexity. A new persisted format is not mandatory.
* Phase 1 decision: retain flat widget JSON and existing persisted type strings;
  introduce only portal ownership abstractions. No firmware format rollout is
  planned. Reopening the format decision requires a concrete problem and review.
* Unavailable widget settings may be dropped during edits; no preservation
  machinery or guarantee is required for unsupported widgets or missing
  Extension packages. This permits loss, not proactive deletion on load.
* Focus on supported editing workflows: save the configuration produced by the
  available editor, without retaining unused fields for features that might be
  re-enabled. Preserve current user edits and drafts, not obsolete configuration.
* Literal screen targets must come from the available screen catalog; dynamic
  targets use explicit Binding value mode. Do not synthesize options to retain
  missing screen IDs. Reject targets over 31 UTF-8 bytes rather than truncate
  them or expand firmware storage.
* Numeric Rocker saves tap and adjustment actions only. Saving removes existing
  long-press entries; this configuration change is explicitly approved. Do not
  change firmware interaction behavior or expose long-press controls.
* Adopt the lifecycle safeguards, representative-by-representative browser
  checks, and immediate removal of each migrated slice's superseded logic.
* Propose device runtime behavior or native Extension ABI changes separately
  and obtain approval before implementing them.

## Design Boundaries

### Greenfield Compatibility Policy

Implement one current configuration/editor contract, not old and new contracts
side by side. Do not retain aliases, dual readers/writers, fallback parsing,
version-based behavior, or tests solely to support superseded configurations.
Existing device configurations and imports need not round-trip or keep working;
manual reconfiguration is acceptable. Retaining flat JSON is a simplicity
decision, not a compatibility promise. Retain current editing capabilities and
draft safety, not historical configuration behavior. This policy does not waive
the separate approval gates for firmware runtime or native Extension ABI changes.

### Shared Field Mechanics

Support the field kinds already needed: text, number, checkbox, select, binding,
and color. Descriptors may identify a field, its configuration key, its default,
and straightforward bounds or conversion rules. Reuse native input validation
and existing helpers rather than recreate them.

Begin with existing markup and widget-owned descriptors for load/build behavior.
Do not make generated forms a prerequisite. Add shared rendering only if the
representative widgets demonstrate a concrete reduction in duplication.

Do not introduce a conditional expression language, schema compiler, automatic
discovery, cross-language schema generation, or speculative hook surface.
When a descriptor becomes a small program, use explicit widget-owned code.

### Widget Ownership

Each widget owns its field declarations, section roles, and optional specialized
initialization, refresh, load/build, validation, and action integration behavior.
These are candidate responsibilities, not a requirement to implement every
hook for every widget. Keep closely related widget families together only when
that improves ownership; do not collect all special cases in one shared module.

Shared orchestration owns lifecycle sequencing, draft capture/restoration,
dirty tracking, error presentation, and applying declared section/action policy.
It must not inspect private widget fields or branch on individual widget names.

The actual interface is established by representative migrations, not frozen
before those migrations. Reuse existing disclosure/source rules only where
they remain useful; do not preserve them at the cost of a complex abstraction.

### Action Ownership

Widgets declare supported action slots and special integration requirements.
Generic code owns reusable action editor mechanics and consumes that policy
for both visibility and serialization. Do not maintain duplicate widget lists.

Preserve Rocker's directional actions, Numeric Rocker's adjustment action and
step substitution, Gamepad Button's held action and release semantics, and input
widgets' consumed actions. Simplifying labels must not make direction or action
meaning ambiguous. Device behavior changes need separate approval.

Navigate to Screen owns its screen-versus-binding UI. Persist the actual target
string, never the Binding value UI sentinel. Restore binding mode on load and
validate it using existing binding facilities. No List DOM access, provider
inspection, or pad-specific action prefixes belong in the Screen action editor.

### Firmware Ownership

The firmware already calls widget-owned `parseConfig` through `widget_find` in
[src/app/pad_config.cpp](src/app/pad_config.cpp). Reuse this boundary; do not
introduce a parallel firmware registration or parsing framework.

A nested persisted widget object is a candidate simplification, not an approved
shape. Compare its benefit with the existing flat fields, including portal,
firmware, defaults/catalogs, MCP authoring, samples, imports, and extensions.
Keep runtime config storage bounded and avoid increasing per-button memory
without a demonstrated need. Retain opaque Extension configuration semantics.

## Phase Tracker

| Phase | Deliverable | Status |
| --- | --- | --- |
| 0 | Scope and implementation plan | Complete through Phase 6 for the agreed portal scope |
| 1 | Contract baseline and format decision | Complete; D1..D5 approved |
| 2 | Generic screen binding target | Complete; focused and mock-browser checks passed |
| 3 | Shared fields and representative editors | Complete; focused and mock-browser checks passed, with one baseline test failure noted |
| 3a | Representative ownership boundaries | Complete; 13 focused checks and mock-browser checks passed |
| 4 | Remaining editor migrations | Complete and committed as `1f8ded9` |
| 4b | Single-manifest widget wiring | Complete and committed as `fbd5d0f` |
| 5 | Ownership cleanup and Waveform editor restoration | Complete; focused checks and desktop/mobile browser verification passed |
| 6 | Documentation and final verification | Complete; existing evidence reconciled and final criteria checked |

### Phase 0 Scope Approval

* [x] Interview and record architectural preferences and tradeoffs
* [x] Inspect representative portal paths and firmware parsing boundary
* [x] Create temporary execution tracker
* [x] Obtain approval to start Phase 1
* [x] Obtain approval to proceed beyond Phase 1 (Phase 2 only)

### Phase 1 Contract Baseline and Format Decision

Primary surfaces: widget registry/parser interfaces, widget modules, pad dialog,
editor, workspace, firmware defaults/catalogs, and existing test fixtures.

* [x] Inventory active editors and their current fields, defaults, bounds,
  sentinel behavior, action policy, capability gates, and specialized behavior
* [x] Read applicable subsystem instructions for the investigation
* [x] Run existing focused portal tests to establish a baseline
* [x] Compare existing and nested widget configuration shapes against actual
  ownership problems; record the recommendation and affected consumers below
* [x] Assess firmware scope and identify conditional verification requirements
* [x] Agree the format decision and resolve the contract decisions below

Exit gate: a bounded contract sketch, field-coverage inventory, explicit format
decision, and baseline results. If a breaking format is chosen, propose its
concrete shape and scope for approval before implementing it. Do not expand
this phase into a whole-repository redesign.

### Phase 2 Generic Screen Binding Target

Primary surfaces: Screen action editor and existing action-editor tests.

* [x] Add Binding value mode and a binding input to Navigate to Screen
* [x] Load/build the existing action's actual target consistently in all hosts
* [x] Remove List-specific option injection and its refresh call sites
* [x] Check literal screen selection, binding selection, validation, draft
  restoration, and asynchronous screen catalog population
* [x] Enforce the existing 31-byte stored target limit without truncating an
  expression or increasing firmware storage; check the boundary in UTF-8 bytes
* [x] Agree available-screen-only literal targets, without missing-ID retention
  (revised D3)

Exit gate: List selected-item navigation works through an ordinary binding
target; other action hosts do not depend on the pad editor or List DOM. Retain
one action type unless concrete evidence requires a separately approved change.

Verification: extended existing startup checks for binding round-trips, catalog
refreshes, mode switches with invalid drafts, and ASCII/multibyte length limits.
The pad-level target check now uses the production Screen population function.
Existing action-list, pad transaction, dirty-state, Timer binding, binding help,
and action-picker checks passed. Production asset gates passed headless and P4
profiles (with and without PSRAM); no firmware or ABI changed, so no firmware
build ran. No new test harness or dependencies were added.

Follow-up: pad target labels include their one-based index (for example
`Pad 1: Living Room`). After `(none)`, options are numerically ordered pads,
Binding value, then built-in screens. Startup, pad dirty-state, and production
asset checks passed after this change. Missing diagnostic screens in the mock
catalog are a fixture limitation; firmware already registers them where
supported. The mock catalog is unchanged, as agreed with the user.

Mock portal: `http://localhost:8766/?profile=esp32-p4-lcd4b&fragment=pad-editor`.
Verified invalid draft restoration across button switches, correction and save
to the mock API (HTTP 200, actual binding target, no sentinel), literal/binding
mode changes, and blocked overlength save with zero pad POST requests. Boot
Actions built `[mqtt:screen]` with no pad DOM and focused its own overlength
field. Inspected control screenshots at 390px and 1440px widths. Some pad
interactions required DOM event dispatch because integrated-browser pointer
automation was inconsistent; a normal pointer click selected **Binding value**
in Boot Actions. Hardware navigation remains untested; no flashing requested.

### Phase 3 Shared Fields and Representative Editors

Primary surfaces: widget-owned web modules/partials, small shared field/registry
helpers, pad dialog/editor/workspace, and asset manifests.

#### Phase 3 Interview Decisions

These decisions supplement D1..D5. The user initially selected decisions only,
then separately authorized Phase 3 implementation.

* D6: Reject explicit invalid or out-of-range numeric input rather than silently
  clamp it or replace it with a default. Preserve the raw draft, block save,
  and focus the offending field. Blank optional fields may use declared
  defaults; bindable fields continue to use existing binding validation.
  Apply this contract to migrated fields, not unrelated editors in advance.
* D7 (revised and approved): Use stable, manually collapsible groups rather
  than feature-dependent field hiding or automatic disclosure. Feature toggles
  and controlling values stay inside their groups; dependent settings remain
  editable even when the feature is off. Use fixed initial open/closed states
  and preserve manual collapse choices in drafts. Refresh must not reset values
  or collapse choices. Do not add a conditional disclosure framework. Apply
  this simplification incrementally as editors migrate, retaining every field.
* D8: Keep Gamepad Button's existing held-action section in Content. Its
  specialized action policy still owns serialization and suppression of the
  ordinary tap/long-press lists. Keep the existing Gamepad editor, first-tap
  action storage, and firmware-owned automatic release semantics.
* D9 (clarified and approved): Preserve and serialize retained settings even
  when their feature is off; disabling a feature does not reset its settings.
  Validate shared fields and the selected, supported widget's retained fields,
  including fields in collapsed groups or inactive Actions/Appearance tabs.
  Do not validate unrelated mounted widget editors. On failure, select the
  relevant workspace tab, open the containing groups, and focus the invalid
  field without enabling its feature. Use the ordinary validation path, not
  separate disabled-feature rules or disclosure overrides. Do not silently
  normalize or discard settings. This does not add preservation requirements
  for unavailable widgets or capabilities under D2.

Implementation order remains Table, Gauge, then Gamepad Button, with focused
checks and a browser check before opening the next representative. Existing
markup and explicit manifest registration remain the starting point; generated
forms are not a prerequisite. No firmware, native ABI, dependency, or persisted
format change is authorized by these decisions.

* [x] Establish minimal shared field load/build and validation mechanics
* [x] Register section roles and action policy without private-ID routing
* [x] Migrate Table as a straightforward field editor
* [x] Migrate Gauge to exercise repeated sources, bindings, colors, and bounds
* [x] Migrate Gamepad Button to exercise specialized action ownership
* [x] Validate each migration before proceeding to the next representative
* [x] Check each representative in the browser, including draft restoration,
  inactive Actions/Appearance tabs, validation focus, and repeated mounting
* [x] Remove the migrated slice's superseded logic immediately; reserve Phase 5
  for cross-cutting cleanup rather than keeping duplicate ownership until then
* [x] Retain flat JSON and existing type strings under D1; no firmware format
  change, native ABI change, dependency, or firmware build was needed
* [x] Review the resulting interface for unnecessary hooks or descriptor logic

Exit gate: representative editors retain setting coverage and workflows, while
their specific code stays widget-owned. Temporary old/new implementation paths
may coexist during migration, but no compatibility layer may survive completion.
No firmware-format rollout is planned under D1. Reopening that decision needs
separate evidence, review, and approval.

Implemented modules: `portal_widget_editor.js`, `portal_widget_table.js`,
`portal_widget_gauge.js`, and `portal_widget_gamepad_button.js`. Shared code
applies declared field, section, action, track, and icon policies. Gauge's
automatic disclosure and shared field ladders were removed; other widgets keep
their existing paths until Phase 4. Existing guides describe this partial
migration and the changed Gauge behavior.

Verification: existing transaction coverage now checks Table round-trips,
Gauge retained disabled settings, raw invalid numbers, text target serialization, blank defaults,
inactive-widget isolation, and unavailable Gamepad Button omission. Existing
dirty-state, appearance, preview, import, startup, action, binding-help, and
production asset checks passed. The final focused CTest run passed all 13 checks.
The asset gate covers the headless C3 profile
and P4 with and without PSRAM. `test_timer_action_editor` fails because its
input double lacks `closest()`; the identical failure reproduces from an
untouched HEAD archive. It was not changed as part of Phase 3.

Mock browser checks used port 8766: Table binding/false-scroll round-trip and
draft restore; Gauge static defaults, disabled needle rejection without raw
value loss, save-time Content/group/focus navigation, shared-binding error focus, retained disabled styles,
and manual disclosure across button switches; Gamepad Button held-trigger
draft restore, Content role, hidden ordinary actions, mock save/read-back, and
fragment remount with released old DOM references. Gauge layout screenshots
were inspected at 1440px and 390px widths, with no page overflow. Field probes
used DOM events because integrated-browser pointer automation is unreliable;
physical device behavior was not exercised. An initial Gamepad probe ran before
the pad finished loading; its transient span error disappeared when repeated
against the loaded fixture. No firmware build, flashing, or commit was performed.

### Phase 3a Representative Ownership Boundaries

Approved after the Phase 3 retrospective (2026-10-15). Commit the verified
Phase 3 checkpoint first; implement and commit this follow-up separately.

* [x] Remove the Screen action's dependency on `padWorkspace`; report validation
  errors to the host, which owns error messages, focus, and tab selection
* [x] Move Gauge and Gamepad Button-specific CSS into explicitly bundled,
  widget-owned stylesheets; retain genuinely reusable layout utilities
* [x] Document the minimal ownership contract and its unproven boundaries
* [x] Run existing focused action/portal checks and production asset gates
* [x] Check error presentation and unchanged desktop/mobile layout with the
  mock portal; no firmware build or flashing is needed

Phase 3 checkpoint: `889698b`. Screen validation now reports structured errors
without host-dependent presentation. Non-pad hosts catch build failures within
their save boundary and use the ordinary shared action error presenter; pad
workspace error navigation is unchanged. Gauge/Gamepad-only styles are owned
assets with explicit display-gated CSS bundle entries.

The browser caught a mock-only gap: CSS serving had hardcoded core files rather
than reading the production manifest. The mock now reuses its existing
feature-aware bundle loader for both JS and CSS, with coverage in its existing
HTTP asset test. Restarted the mock on port 8766; in-memory fixtures reset.

Existing startup coverage checks quiet Screen builds, invalid-save focus and
zero writes across Boot/Swipe/Hardware Button/MQTT Trigger/Timer hosts, and a
corrected Boot save. Browser checks confirmed Boot error feedback and corrected
serialization, plus pad Actions-tab/group/focus navigation, zero invalid writes,
and corrected mock save/read-back. Boot persistence is not browser-verified:
the mock Boot config endpoint returns 404. Gauge and Gamepad layouts remained
grids without page/field overflow at 1440px and 390px; desktop Gauge and mobile
Gamepad screenshots were inspected after selecting Content and foregrounding
the integrated browser. Probes used DOM events, not physical pointer testing.
Final verification: all 13 focused CTest entries passed, including the extended
startup and dev-server tests and the three production asset profiles. No firmware
build, flashing, dependency, or native ABI change was needed. The previously
recorded baseline Timer editor fixture failure is outside this suite and unchanged.

Exit gate: reusable actions do not inspect pad workspace state, representative
widget styles are locally owned, and existing editing behavior is retained.
No visual redesign, automatic asset discovery, speculative descriptor options,
or additional widget migration belongs in this phase.

The interface remains provisional until behavior-heavy widgets exercise it.
Begin Phase 4 with Rocker and Numeric Rocker before batching simpler editors;
treat asynchronous Extension integration as a separate checkpoint. This records
the approved sequence. Phase 4 authorization is recorded below.

### Phase 4 Remaining Editor Migrations

Use the representative interface, which remains provisional for directional
actions, consumed actions, and asynchronous catalogs. Validate each family
before opening the next slice; make only small, evidence-driven interface changes.

#### Phase 4 Interview Decisions

The user approved all Phase 4 implementation after the interview, retaining
family-by-family focused and browser validation gates. The user explicitly
requested recording decisions and stopping this turn; no production migration
was performed during the interview. Phases 5 and 6 are not authorized.

* D10: Authorize all Phase 4 families in the listed order, beginning with Rocker
  and Numeric Rocker. Validate each family before proceeding; Extension remains
  a separate asynchronous integration checkpoint. Stop for new decisions,
  unapproved scope, or firmware/runtime/ABI changes. Separate approval for each
  already-listed family is not required.
* D11: Write explicit ordinary field defaults through the shared builder rather
  than preserve historical omit-default rules. Keep omission where absence has
  meaning, such as an optional action or automatic setting. Retain flat keys,
  existing type strings, device-provided color defaults, and firmware behavior.
  Verify absence-versus-explicit-value semantics against the owning parser for
  each migrated field. Do not add a generic omission-rule language.
* D12: Numeric Rocker may save with no adjustment action; omit the nested action
  when empty. Validate configured actions and preserve signed `{step}` handling.
  D4 still removes long-press entries on save. Rocker retains axis-dependent
  Up/Down or Left/Right labels and existing action-slot semantics.
* D13: Sparkline rejects invalid timing rather than replacing or clamping it:
  the window is 10 seconds through seven days and the requested interval is at
  least 100 milliseconds. Keep whole-second window conversion, derived
  point-count rounding, and bounded 2..1024-point sampling. Display the resulting
  actual interval; derived capacity adjustment is not an input-validation error.
* D14: Remove Sparkline's redundant backfill checkbox. A nonempty history entity
  configures backfill; an empty entity means off, matching the existing firmware
  contract. Keep supported entity/stat controls statically editable and validate
  their retained values. This supersedes the interview's preliminary proposal
  to persist a separate enabled flag: no such firmware field exists. Do not add
  off-state entity storage, portal-only shadow configuration, or firmware changes.
  Unsupported HA-history fields may still be omitted under D2.
  This explicitly revises D9 for backfill: clearing the entity disables history
  and forgets that entity on save. Temporarily disabling history while retaining
  its entity is no longer an editing capability; other D9 guarantees remain.
* D15: While the catalog is pending or failed, block committing edits to an
  Extension configuration with a local error distinguishing loading, failure,
  and a successfully empty catalog. Persistence saves a whole pad, so do not
  globally invalidate untouched Extension buttons or block unrelated pad edits
  merely because the catalog is unresolved. Missing packages follow D2; do not
  require unrelated configuration repair or add preservation machinery. Offer
  only installed enabled packages after success; remove synthetic missing-ID
  options. Retain pending selection until the response settles and protect newer
  edits and mounted-editor identity. Preserve invalid user drafts through the
  ordinary draft/error path, not by silently discarding their edits. Reuse
  existing reload/remount recovery; do not add a new recovery system.
* D16: Keep Extension configuration as trimmed opaque text with its existing
  511-character limit. Do not parse JSON, introduce package-specific forms, or
  change the native ABI. Blank tick interval remains the package default.
* D17: Reject incomplete configured backfill with a focused error rather than
  silently omit it. Under D14, a nonempty entity requires its live stream binding;
  an empty entity is intentionally off and does not require a binding. Do not
  validate inactive or unsupported widget editors.
* D18: Reject fractional input for integer-only fields rather than truncate or
  round it, extending D6 consistently. This includes opacity, Numeric Rocker zone
  scale, Mousepad Back size, and Extension tick interval. Retain fractional input
  for fields that support it, including step sizes, sensitivity, and Gamepad
  Joystick dead-zone percentage conversion.

The user approved the decision-review clarifications above and these ownership
guardrails (2026-10-15):

* Keep unit conversion, derived sampling, catalog loading, and specialized
  actions in explicit widget-owned code. Do not turn descriptors into programs
  to maximize reuse; modest duplication is preferable to a speculative framework.
* Complete each family's ownership across every affected generic surface:
  load/build, action labels and policy, workspace routing, colors/patterns, icon
  sizing, preview fields, capability checks, styles, and lifecycle callbacks.
  Inspect applicable surfaces, not unrelated modules. Remove superseded branches
  within that family; do not defer incomplete ownership to Phase 5.
* Phase 4 completion does not establish final acceptance. At that boundary,
  cross-cutting cleanup, proposed Waveform retirement, and final verification
  still required authorization. Phase 5 later approved restoration instead.

Implementation hypothesis for the first family: widget-owned descriptors,
declared action labels, and an Actions-role adjustment section can replace the
Rocker/Numeric Rocker shared branches and private-ID workspace routing without
changing runtime semantics. The existing dialog transaction suite is the first
cheap discriminating check; extend it for directional serialization, optional
adjustment, D4 long-press omission, and invalid raw numeric drafts. Then check
dirty-state/lifecycle behavior and the production asset gates, followed by
desktop/mobile mock-browser action, draft, and error-focus checks before moving
to the next family. Remove superseded logic within this slice, including the
private-ID reparenting listed in Phase 5; do not defer duplicate ownership.

* [x] Migrate Rocker and Numeric Rocker
* [x] Migrate Mousepad, Scrollpad, and Gamepad Joystick
* [x] Migrate List and Camera Preview
* [x] Migrate Bar Chart and Sparkline
* [x] Migrate Extension, including catalog loading and opaque configuration
* [x] Confirm the Phase 1 inventory contains no additional unmigrated editor
* [x] Apply D10..D18 and update affected portal guidance and fixtures alongside
  each migration; retain D1's flat format with no firmware rollout

Exit gate: all active editors use the ownership contract. Tests cover actual
serialization, lifecycle, action policy, and capability behavior, not source
spelling. Runtime interaction semantics remain unchanged unless approved.

#### Phase 4 Verification Checkpoints

All 13 selector types have registered owners. Each migrated family's superseded
load/build, action, section, fill, icon, capability, and preview branches were
removed from generic editor code. Bar Chart and Sparkline own their CSS through
explicit display-gated bundle entries; other new owners reuse shared utilities.
The flat JSON keys and firmware remain unchanged.

* Rocker/Numeric Rocker: directional action labels, Actions routing, optional
  adjustment, signed `{step}`, long-press omission, numeric drafts, and capability
  behavior verified through the transaction and dirty suites and browser checks
* Input family: touch/USB capability gates, fractional sensitivity/dead-zone,
  static retained controls, navigation policy, and action suppression verified
* List/Camera: trimmed provider/filter, Select labels, Content routing, and
  camera capability/image-scale behavior verified; camera availability on the
  P4 mock required a controlled capability probe, not physical camera testing
* Bar Chart: sources, patterns, outline, radius, preview contributions, retained
  disabled styles, raw numeric errors, draft switching, and desktop/mobile
  screenshots verified
* Sparkline: whole-second range, 100-ms minimum interval, 2..1024 derived points,
  actual interval, automatic bounds/colors, entity-driven history and live-binding
  requirement verified; 7 days at capacity displays 590.625 seconds per point
* Extension: pending/current selection, failed versus empty catalog, installed
  enabled options, opaque text/511-character limit, blank tick omission, integer
  tick bounds, stale mount responses, and newer drafts verified. Browser save
  checks prove untouched Extension buttons do not block unrelated saves. A
  pending error on a different button revalidates on reopening after success,
  with regression coverage and no additional dirty revision

Final focused verification passed these existing scripts:

```bash
node tests/test_portal_pad_dialog_transaction.js
node tests/test_portal_pad_dirty.js
node tests/test_portal_startup.js
node tests/test_portal_pad_colors.js
node tests/test_portal_pad_preview.js
node tests/test_portal_action_list.js
node tests/test_portal_action_editor_cycle.js
node tests/test_portal_action_picker.js
node tests/test_portal_binding_help.js
node tests/test_portal_pad_import.js
node tests/test_portal_timer_binding.js
node tests/test_portal_asset_gates.js
git diff --check
```

The asset gate minified headless C3, P4 with PSRAM, and P4 without PSRAM profiles.
Editor diagnostics found no errors in touched JavaScript. Dirty tests' simulated
span/network failures are expected passing cases. The earlier unrelated Timer
editor fixture failure remains outside this slice; the full suite is not claimed
green. Mock-browser Extension and Sparkline screenshots at 1440x1000 and 390x844
show readable controls and no horizontal overflow. Async browser probes used
controlled fetches and DOM selection, not device input or camera/runtime proof.
Existing developer/user guides now reflect the approved Phase 4 behavior.
No firmware build, flashing, dependency change, ABI change, commit, or branch
change was performed. At the Phase 4 boundary, Phase 5 cleanup and Phase 6 final
acceptance still required authorization; D32 subsequently approved Phase 5.

### Phase 4b Single-Manifest Widget Wiring

The user approved this additional phase after reviewing the remaining manual
registration and packaging steps. One manifest replaces duplicated widget
inclusion lists; behavior remains in the owning C++ and JavaScript modules.

#### Approved Decisions

* D19: Use one JSON manifest with explicit paths and existing compile gates.
  Cover every currently registered firmware widget, including device-class
  widgets. Portal assets are optional. Do not use filename auto-discovery.
* D20: Keep firmware registration and `padRegisterWidgetEditor()` local to their
  implementations. The manifest owns discovery and packaging, not fields,
  defaults, rendering, validation, actions, or asynchronous workflows.
* D21: Generate build outputs when needed; do not commit generated files.
  Remove superseded hand-maintained widget lists rather than maintaining both.
* D22: The manifest owns selector label and order. Firmware preview metadata
  remains firmware-owned; no new selector-metadata edit is required elsewhere.
* D23: Existing compile gates determine asset and firmware inclusion; existing
  owner `available()` callbacks determine runtime editor visibility.
* D24: Permit shared assets, including the input-family JS module. Deduplicate
  them deterministically while preserving declared dependency order.
* D25: Firmware builds, asset builds, the device-free portal server, and relevant
  host tests consume the same manifest. No development-only widget list exists.
* D26: Fail early for duplicate types, missing files, invalid gates, or mismatched
  registration types. Verify compiled/runtime registration where applicable;
  do not build a parser for arbitrary C++ or JavaScript behavior.

At the Phase 4b boundary, Waveform retained its gated firmware entry without a
portal editor entry. Phase 5 subsequently restored that editor under D32.
Phase 4b does not alter
firmware interaction, persisted JSON, the native Extension ABI, or dependencies.
The retrospective's Extension/workspace boundary is not implicitly added here.

#### Build Integration Proposal

Use a small reusable Python manifest loader/generator with the existing toolchain.
Generate a guarded firmware include consumed by
[widgets.cpp](src/app/widgets.cpp) before Arduino compilation. The generated
include preserves the current gates and implementation order; it is ignored
build output, not another tracked source list.

Replace widget entries in the JS/CSS bundles and the template/selector mappings
with manifest expansion at their existing dependency positions. Keep non-widget
bundle entries hand-maintained. Both
[minify-web-assets.sh](tools/minify-web-assets.sh) and
[portal-dev-server.py](tools/portal-dev-server.py) reuse the same loader/rendering
path. Integrate generation with [build.sh](build.sh) before compilation and with
relevant host CMake targets using declared manifest/source dependencies, without
relying on outputs left by a prior firmware build. The user authorized these
routine integration decisions together with implementation.

Expected changes: one manifest and a small loader/generator; firmware aggregator,
build hook, relevant CMake/test integration, HTML renderer, mock server, bundle
widget entries, selector/partial wiring, ignore rules, and existing developer
guides. Reuse current rendering and bundle machinery rather than introducing a
parallel asset pipeline. Main risks are compile-gate parity, shared-asset order,
missing registration, and stale generated outputs in clean checkouts.

#### Implementation And Verification

* [x] Approve and record the manifest scope and ownership boundaries
* [x] Confirm generated-output locations and integration hooks
* [x] Add the manifest and loader with focused validation in the existing suite
* [x] Replace firmware include wiring and verify registration/gate parity
* [x] Replace widget bundles, partial mappings, and selector wiring
* [x] Integrate the mock server and relevant clean-checkout host checks
* [x] Verify duplicates, missing assets, invalid gates, shared-asset ordering,
  firmware-only entries, and mismatched registration types
* [x] Run existing portal behavior checks and all three asset-gate profiles
* [x] Check mock-browser selector availability and representative custom UI
* [x] Build only the preferred verification board, `jc1060p470c-sd`, because
  firmware compilation wiring changes; no flashing or all-board build
* [x] Update existing extension-path guidance and record remaining limitations

Exit gate: a normal new widget requires owned files, local C++/JS behavior
registrations, and one manifest entry. No widget-specific edits to generic
orchestration, bundle lists, template mappings, or selector markup are required.
Existing types, capabilities, shared-asset ordering, and custom UI remain intact.
Phases 5 and 6 remain unauthorized.

#### Execution Evidence

One explicit manifest now drives guarded firmware includes, deduplicated JS/CSS
bundle entries, partial mounting, selector labels/order, and mock-server assets.
Local registrations and runtime availability remain owner-controlled. The
generated header is ignored and recreated by `build.sh`; host CMake writes its
copy to the binary directory. Source-only snapshot configuration, generation,
and the registered mock-server suite passed without prior generated assets.

All 13 focused portal CTest entries passed, including three asset-gate profiles
(headless C3, P4 with PSRAM, P4 without PSRAM). Existing tests cover invalid
manifest entries, shared paths, firmware-only Waveform, capability gates,
mounted IDs, selector order, and actual JS registration mismatch failures.
The existing Camera Preview wiring guard also passed. Python syntax checks and
editor diagnostics found no errors in the touched slice.

The initial firmware build exposed Arduino's failure to stage `.inc` files,
mixing original and copied headers. Changing the generated include to `.h`
fixed that build-path issue without altering firmware headers or behavior.
`./build.sh jc1060p470c-sd` then passed: 3,199,184 bytes, no compiler warnings.
The generated flag reference only removes aggregator references whose guards
now live in the manifest. No flag values, dependencies, persisted JSON, or
native ABI changed. No flashing or other board build was performed.

Independent headless browser checks and screenshots at 1440x1000 and 390x844
verified selector parity, representative chart/input/Extension sections,
Numeric Rocker's Actions placement, Extension's initial disclosure, editable
opaque configuration, and real custom-dropdown pointer selection. No page errors or
horizontal overflow occurred. Integrated-browser screenshots were blank, so
visual evidence uses the independent browser. Hardware interaction and
firmware-only Waveform compilation were not verified on another board.
Phase 5 cleanup and Phase 6 final project acceptance remain outside this phase.

### Phase 5 Ownership Cleanup and Waveform Editor Restoration

#### Decision Interview

The user initially requested a progress assessment and interview, followed by
recording decisions and stopping. Subsequent approval authorized implementation
and resolved Waveform in favor of editor restoration.

* D27: Evaluate extensibility for widgets using existing runtime/editor
  capabilities. Such a widget should need owned files and one shared manifest
  entry, without widget-specific changes to generic editor orchestration.
  Shared firmware runtime refactors remain outside Phase 5. A genuinely new
  interaction capability requires a separate proposal rather than a universal
  promise of zero infrastructure changes.
* D28: Remove Extension's direct workspace access through a small host
  notification. The widget owns asynchronous catalog state and mount guards;
  the host owns deciding whether to revalidate and how to present errors.
  Preserve raw drafts, unrelated saves, newer edits, and dirty revision
  behavior. Do not add a general event framework or speculative hook surface.
* D29: Normalize widget presentation using small shared layout utilities for
  spacing, rows, and responsive behavior. Preserve controls, field order,
  groups, tabs, and specialized interactions. Keep widget-specific styles
  scoped to their owners. No generated forms or functional redesign.
* D30: No demonstration widget, temporary end-to-end probe, or permanent
  example is required to establish extensibility. The user accepts a confident
  architectural assessment. Existing focused regression and asset checks
  remain required for subsequent implementation changes.
* D31: Record decisions in this tracker and stop. Phase 5 implementation,
  Phase 6 acceptance, firmware/runtime changes, flashing, and commits are not
  authorized by the initial interview alone.
* D32: The subsequent approval authorizes Phase 5 and previous recommendations,
  including Waveform editor restoration. Retain the existing flat fields,
  firmware defaults, HTML bounds, ordinary actions, and catalog availability.
  Do not expand firmware behavior, native ABI, or Phase 6 acceptance.

#### Extensibility Assessment

The ordinary widget authoring path already meets the structural goal: three
registration points, but only one shared registration file:

1. Owned C++ implementation with local `REGISTER_WIDGET_*` registration,
   parsing, bounded configuration, runtime behavior, and preview metadata.
2. Owned editor JavaScript with local `padRegisterWidgetEditor()` registration,
   field declarations and specialized behavior; owned HTML and optional CSS.
3. One entry in [manifest.json](src/app/widgets/manifest.json), the only shared
   packaging/selector registration file.

The manifest drives firmware inclusion, JS/CSS packaging, partial mounting,
selector labels/order, and the mock portal. Ordinary additions do not require
editing bundles, template mappings, selector markup, or generic load/build
orchestration. Firmware parsing already delegates through `widget_find()` in
[pad_config.cpp](src/app/pad_config.cpp). This assessment is based on inspected
code and the recorded Phase 4b verification, not a newly implemented example
or newly rerun suite. Widget tests and relevant documentation remain normal
authoring responsibilities, not additional production registration points.

Two boundaries qualify that assessment. First, D28's remaining portal ownership
gap is now closed: [portal_widget_extension.js](src/app/web/portal_widget_extension.js)
notifies its host after asynchronous catalog settlement without inspecting
workspace state. Second, shared firmware still
names particular widgets in screen layout, event routing, and swipe policy,
including [pad_tile_builder.cpp](src/app/screens/pad_tile_builder.cpp),
[pad_screen_events.cpp](src/app/screens/pad_screen_events.cpp), and
[widget_registry.h](src/app/widgets/widget_registry.h). Therefore complete
firmware interaction independence is not claimed. D27 leaves these runtime
boundaries outside this phase rather than silently expanding its scope.

#### Waveform Clarification And Approved Restoration

Waveform is active firmware, registered locally in the shutter tester's
[waveform_widget.cpp](src/app/device_classes/shutter_tester/widgets/waveform_widget.cpp)
and included by the manifest under `HAS_DISPLAY` and `IS_SHUTTER_TESTER`.
Keep its firmware implementation, registration, parsing, and runtime unchanged.
It does not need a firmware update to remain included through this cleanup.

The previously unmounted [_widget_waveform.html](src/app/web/_widget_waveform.html)
partial now mounts through the manifest with its owned JavaScript registration.
The editor restores six colors, line width, sensor choice, four toggles, and
ordinary tap/long-press actions. Colors participate in shared binding validation
and preview hooks. Existing limits remain: colors 11 characters, width 1..5,
and All or Sensor 1..4. No expansion to the firmware's maximum sensor count is
included. Availability follows the actual firmware catalog, not a board name.

The inspected history did not establish a prior fully wired editor or a precise
regression date. The restoration exposes settings consumed by active firmware;
the earlier proposed partial retirement is superseded by D32.

#### Implementation Checklist

* [x] Check for superseded shared field ladders, widget lists, and temporary paths
* [x] Remove Numeric Rocker private-ID reparenting from generic workspace code
  (completed within its Phase 4 ownership slice)
* [x] Remove Extension's workspace dependency using the D28 host notification
* [x] Normalize layout presentation under D29 without changing editing behavior
* [x] Keep necessary widget styles scoped to their owning editor
* [x] Resolve Waveform in favor of an owned editor; retain active firmware
* [x] Mount the existing Waveform partial and add its owner registration
* [x] Check asset ordering, feature gates, and stale references
* [x] Confirm no new individual-widget exception in generic code is necessary
* [x] Run existing focused lifecycle, draft, validation, and asset checks;
  verify normalized layouts on desktop/mobile without a demonstration widget
* [x] Obtain subsequent authorization before implementing Phase 5

Exit gate: generic code handles mechanics and declared policies; widget-specific
behavior is owned locally. No unused framework, compatibility code, or dead
portal artifacts remain from the migration. The ordinary-widget path needs owned
files plus one shared manifest
entry; existing firmware runtime coupling is explicitly outside this gate.

#### Phase 5 Execution Evidence

Extension now receives a narrow settlement callback. Existing startup and dirty
tests cover success, failure, disposed stale responses, unrelated selection,
selected-error revalidation, and unchanged dirty revisions. Waveform's existing
transaction test covers catalog availability, defaults, explicit values, false
toggles, color bindings, blank numeric defaults, and invalid fractional width.
No new test harness or demonstration widget was added.

Shared row/caption utilities replace repeated chart grids and generic inline
flex rows. Gauge's redundant stylesheet is removed; Bar Chart, Sparkline, and
Gamepad retain specialized styling. Field IDs, bounds, order, groups, tabs,
and custom interactions remain intact.

All 13 focused portal CTest entries passed, including 24 mock-server tests and
the existing three production asset profiles. Color, transaction, and preview
checks also passed after adding Waveform's binding metadata. JavaScript editor
diagnostics are clean; the Python test retains its existing unresolved-import
editor warning for the runtime-added tools path, while its suite passes.

Independent headless Chromium checked 11 affected editors at both 1440x1000 and
390x844: no page or control overflow and no page errors. It verified normal-P4
Waveform omission, then used a page-local catalog override to exercise Waveform
with real inputs, fractional-width errors, raw draft restoration, correction,
sensor/toggle edits, and mock save/read-back. Screenshots were captured for
Gauge, Sparkline, and Waveform. A final browser check verified catalog-driven
initialization, real custom-dropdown pointer selection, malformed color-binding
rejection, and correction. The normal mock profiles do not emulate shutter
hardware; firmware rendering and physical interactions remain unverified.

Existing developer and user guides now describe restoration, availability,
shared layouts, and host-owned settlement. No firmware source, native ABI,
persisted format, build workflow, dependencies, commits, or flashing changed.
Portal-only checks suffice; no firmware build was run. Subsequent Phase 6
authorization and acceptance are recorded below.

### Phase 6 Documentation and Final Verification

The user authorized committing the completed work and updating this phase and
the final criteria. This is a bounded acceptance pass, not another migration or
redesign. Reuse the recorded evidence; do not repeat completed checks merely to
close this tracker, review its entire history, or add a demonstration widget.

* [x] Confirm existing developer guidance describes owned C++/JS registration,
  HTML, optional CSS, one shared manifest entry, lifecycle, and shared mechanics
* [x] Confirm existing user guidance covers Screen binding targets, retained
  settings, approved action policies, Extension behavior, and Waveform restoration
* [x] Check MCP's existing flat-field contract; no API or persisted-format change
  requires MCP code or guide changes
* [x] Reuse Phase 5's 13 passing focused portal checks, including the 24-test
  mock-server suite and all three production asset profiles
* [x] Reuse the recorded family-specific browser checks for actions, drafts,
  validation focus, capabilities, asynchronous Extension behavior, and saves
* [x] Record the subsequent spacing fix: button settings match the pad's 14px
  top gap at 1440px and 390px; browser measurements/screenshots and CSS diagnostics pass
* [x] Reuse Phase 4b's clean-source host/generation checks and successful
  `jc1060p470c-sd` build; later portal-only changes require no new firmware build
* [x] Check the pending diff and whitespace, and mark the scoped final criteria

Acceptance: the agreed portal ownership and KISS goals are met. Ordinary widgets
using existing contracts need owned implementation/assets, local behavior
registrations, and one shared manifest entry. Shared orchestration handles
mechanics rather than individual widget exceptions. Keep explicit specialized
code and modest HTML/JS coordination; do not introduce a schema framework.

Remaining limitations are accepted scope boundaries, not verified behavior:

* Shared firmware still contains widget-specific layout, interaction, and swipe
  routing. New runtime capabilities require a separate proposal.
* Waveform's firmware registration has no detailed `describeSchema` metadata for
  MCP. This predates the restored portal editor; complete MCP field discovery
  is not claimed or added here.
* Physical shutter rendering, hardware navigation, camera behavior, and native
  Extension runtime interactions are not established by mock-browser tests.
* The earlier unrelated Timer editor test-double failure remains recorded.
  The focused suite passes; full-suite health is not claimed.
* The Python mock-server test retains an editor import-resolution warning for
  its runtime tools path; its tests pass. Phase 4b's board build had no compiler
  warnings; the portal-only follow-up did not rebuild firmware.

Exit gate met for portal ownership, with the limitations above. Commit is
authorized; no tracker deletion, flashing, all-board build, dependency change,
firmware runtime refactor, or native ABI change is part of this closeout.

## Phase 1 Findings And Decision Canvas

### Evidence And Scope

The initial selector inventory in
[pad-editor.fragment.html](src/app/web/pad-editor.fragment.html) contained 13
active widget types plus None. All 13 are listed below. Waveform then had no
selector entry or included editor partial. D32 subsequently approved its
restoration as a fourteenth editor rather than retiring the artifact.

Reviewed the project, web portal, action-type, binding-system, terminology, and
Markdown instructions. Only this tracker changed. No runtime, ABI, dependency,
board flag, or firmware changes are required by the proposed direction.

Primary load/build evidence is
[portal_pad_dialog.js](src/app/web/portal_pad_dialog.js). Section/action routing
and capability checks are in
[portal_pad_editor.js](src/app/web/portal_pad_editor.js); draft capture and
restoration are in [portal_pad_workspace.js](src/app/web/portal_pad_workspace.js).
The widget partials retain their HTML bounds and field lengths. The inventory
records existing portal normalization, not a claim that firmware and browser
validation are identical. Preserve that distinction during extraction.

### Active Editor Coverage

Unless noted, widgets keep the ordinary tap and long-press lists, share button
appearance, and have no additional portal capability gate beyond display.
Color defaults come from `PAD_FIRMWARE_DEFAULTS` where the current loader uses
it; do not replace those device-provided values with a new hard-coded palette.
The three chart editors currently have conditional feature/source disclosure;
static grouping may replace it only without losing editable fields or values.

| Editor / Persisted Type | Editable settings and current normalization | Specialized ownership / gate |
| --- | --- | --- |
| Table / `table` | Data binding; style string; scrollable. Empty binding/style omitted; scrolling defaults true and is serialized explicitly. Binding max 191 characters; style max 127. | Straightforward descriptor candidate; retain style string semantics. |
| Gauge / `gauge` | Four bindings, captions and arc colors; min/max default 0/100 and allow bindings; degrees default 180, range 10..360; start angle default 180, modulo 360 on build; zero-centered and two dual pairs default false; needle defaults enabled. Arc width default 15, range 5..50; track color; animation default 300, range 0..5000 ms. Needle width 2 (0..10), cutoff 0 (0..99); ticks 5 (0..20), tick width 1 (1..5); target value, width 2 (0..5), zone 0 (0..90 degrees), target/zone colors. Shared track outline. | Caption conversion uses label helpers; repeated sources, source disclosure, binding/color helpers and track control placement. Current invalid-value fallbacks are not always equivalent to clamping. |
| Bar Chart / `bar_chart` | Four bindings, captions, colors and fill patterns; min/max default 0/3 and allow bindings; vertical default orientation; zero-centered and two dual pairs false; thickness 100 (1..100); caption area 0 means automatic, positive values capped at 200. Track color/pattern; animation 300 (0..5000 ms); ticks 0 means none (0..20), width 1 (1..5), color; target blank means none, marker width 2 (0..10), zone 0 (0..100 percent), marker/zone colors. Track outline and radius 4 (0..64). | Repeated sources and pattern persistence currently cross into shared color code; icon export also reserves half the content height. |
| Sparkline / `sparkline` | Three bindings, colors and current captions; blank min/max means automatic scale; unified scale defaults true. Window default 300 seconds, build fallback below 10, maximum seven days; 60 default slots, computed from window/interval inputs, bounded 2..1024 and minimum 0.1-second interval. Three HA backfill entity/stat choices, default mean; line width 2 (1..10), offset 0 (0..6), smoothing 0 (0..8). Min/max marker sizes 0 (0..20), formats and label colors; current dot 0 (0..20); caption area 0 means default, capped at 200; three reference values/colors/patterns and include-in-scale toggle. | Unit conversion and slot rounding stay explicit; HA fields require a live binding and enabled/entity values to serialize. `ha_history` gates history UI. Empty reference values omit the line; format max 15, captions/entities max 63, streams max 191. |
| Rocker / `rocker` | Axis defaults vertical; indicator color; opacity defaults 80 (0..255), default opacity omitted. | Tap/long-press become Up/Down or Left/Right action lists; color disallows bindings in markup. |
| Numeric Rocker / `numericrocker` | Axis defaults horizontal; small/large steps 1/10, nonnegative; zone scale 100 (50..150); color; opacity 80 (0..255); nested adjustment action. Default numeric values omitted. | Tap plus adjustment; adjustment preserves `{step}` handling. Long-press UI is currently hidden but serialized; approved D4 removes that serialization on save. Color disallows bindings. Workspace currently reparents its private adjustment section into Actions. |
| List / `list` | Provider ID in `widget_data_binding`, filter in `widget_data_binding_2`; both trimmed and omitted when empty; maxima 63/191. | Tap list labeled Select action; ordinary long-press retained. Provider currently injects synthetic screen targets; Phase 2 replaces this coupling with ordinary binding mode. |
| Mousepad / `mousepad` | Sensitivity 1 (0.1..5); acceleration 0 (0..5); movement threshold 3 (0..12); inertia 0 (0..5); reverse, mouse buttons and Back false; Back size 44 (1..512), rounded; shared disable-pad-swipes false. | Requires touch plus USB HID; consumes ordinary action lists. Back controls are Mousepad-only; shared swipe toggle affects the whole pad at runtime. |
| Scrollpad / `scrollpad` | Axis vertical; sensitivity 1 (0.1..5); inertia 0 (0..5); reverse false; shared disable-pad-swipes false. | Requires touch plus USB HID; consumes ordinary action lists. |
| Gamepad Joystick / `gamepad_stick` | Stick left; center fixed; dead zone defaults 10 percent (0..90), persisted as fraction; invert X/Y false; shared disable-pad-swipes false. | Requires touch plus USB HID; consumes ordinary action lists. Retain fixed/floating and left/right choices. |
| Gamepad Button / `gamepad_button` | One held Gamepad action using the existing Gamepad editor; shared disable-pad-swipes false. | Requires touch plus USB HID; serializes held action as first tap action, no ordinary tap/long-press lists. Runtime owns automatic release; no device behavior changes. |
| Camera Preview / `camera_preview` | Scale defaults cover; cover/letterbox selection serialized. | Requires `has_camera`; no change to runtime preview behavior. |
| Extension / `external` | Enabled installed package selector; opaque text configuration max 511, trimmed by current build; optional tick interval 33..1000 ms, blank means package default; upscale 1..4, default 1. | Requires native extensions. Currently retains saved unavailable IDs as options; D2 does not require retaining this behavior or adding replacement machinery. Selector disabled during catalog loading. Empty ID causes a focused error. Text labels, icons and image controls are hidden, not new configuration semantics. |

### Shared Fields And Ownership Boundaries

* Keep button labels, state, images, confirmation, general appearance, pad
  selection, dirty tracking and draft storage shared. Confirmation is currently
  disabled for widgets. Do not relocate all button fields into widget modules.
* Shared background patterns use spacing 8 (2..64) and stroke 1 (1..8, also
  bounded by spacing), with pattern None and color black defaults. Bar Chart
  adds four series patterns plus a track pattern; Gauge/Bar Chart share outline
  width 0 (0..8) and outline color black. Preserve explicitly set pattern colors
  even when the pattern is None. Shared color helpers must consume declared
  support rather than branch on widget type or private widget hosts.
* Existing shared boundaries also include icon-export sizing for Bar Chart and
  Gauge in [portal_pad_icons.js](src/app/web/portal_pad_icons.js), and the primary
  widget binding entry in `PAD_PV_FIELDS`. Account for these in their migrations;
  moving only dialog branches would leave ownership incomplete. Keep generic
  export/preview mechanics shared and put widget contributions with the owner.
* A portal registration identifies persisted type, owned section roles,
  availability, ordinary field descriptors, and action policy. Shared mechanics
  handle ordinary conversions; explicit widget code handles repeated groups,
  units, catalogs, nested adjustment actions, and opaque Extension text.
* Descriptors need to distinguish missing, blank, zero and false; default-on-load
  is not permission to omit-on-build. Support the existing output type and
  omission rules, including bindable numbers, without a schema language.
  Use existing textarea mechanics for Extension text, not a new JSON parser.
* Action policy drives both rendering and serialization: ordinary lists,
  directional lists, tap plus adjustment, consumed lists, or a single held
  action. Labels may depend on widget-owned state. Do not add a separate shared
  widget list to determine either behavior.

### Lifecycle Contract Sketch

Approved sequencing under D5, to be proven by Table, Gauge and Gamepad Button
rather than frozen as a large hook interface:

1. Initialize shared controls and available widget-owned controls once per
   mounted editor. Repeated mounting must not duplicate listeners.
2. Capture the previous button draft and suppress dirty tracking while loading
   the next button's shared and widget-owned persisted fields.
3. Restore that button's raw draft values, then recompute visibility, action
   policy and derived presentation without resetting those values. Restore
   disclosure/scroll state after refresh. Refresh must not mark a draft dirty.
4. On a user edit, capture raw input before build/validation. Only publish a
   valid built button; preserve invalid raw input and focused field errors in
   the draft. An inactive workspace tab is not an inactive widget field.
5. Guard asynchronous catalog/image/binding work by mounted editor and current
   selection generation. Catalog updates must retain edited/pending values;
   selection guards alone do not prevent overwriting newer edits on the same
   button. Reuse current generation/AbortController patterns, not a new task
   framework. Disposal invalidates callbacks and removes owned listeners.

### Format Comparison And Decisions

| ID | Status | Recommendation / Decision | Evidence / Impact |
| --- | --- | --- | --- |
| D1 | Approved 2026-10-15 | Keep the existing flat widget JSON and persisted type strings. Add only portal ownership abstractions. No firmware format rollout in Phases 3/4. | `widget_find` already calls widget-owned `parseConfig` with the button object. Widget modules can own flat keys without changing storage. Nesting would change common bindings, every parser, MCP schemas/help, defaults/catalogs, samples, import/export, fixtures and Extension fields without removing the portal lifecycle work. |
| D2 | Approved 2026-10-15; preservation waived | Save the configuration produced by the supported editor. Allow unavailable widget settings to be dropped during edits, including settings for unsupported capabilities and missing Extension packages. Do not retain unused fields for possible future re-enablement, add preservation machinery, or block unrelated edits solely to preserve those settings. | User prioritizes the happy path and clean abstractions over rare capability transitions. Existing missing-Extension retention is not a required contract; avoid special preservation paths. Proactive deletion on load is not required. Capability gates still apply. This explicitly supersedes the original issue's unsupported-configuration round-trip requirement. |
| D3 | Approved 2026-10-15; revised for KISS | Literal screen targets use the available catalog; dynamic targets use explicit Binding value mode. Do not retain missing IDs through synthetic options or fallback state; require an available screen or explicit binding before saving a nonempty target. Keep the existing empty choice. Reject targets above 31 UTF-8 bytes with a field error. | Supersedes the interview's missing-ID preservation choice. `CONFIG_SCREEN_ID_MAX_LEN` is 32 including terminator; do not truncate expressions or expand storage. Binding mode stores the actual expression, validates syntax, and does not promise resolution in every host. Ordinary asynchronous catalog loading must still work; do not confuse a pending catalog with an unavailable screen. |
| D4 | Approved 2026-10-15; configuration change | Numeric Rocker saves tap plus adjustment actions only; omit `lp_actions` when saving that widget. Keep long-press controls hidden. | User explicitly accepted removing existing long-press entries on save. Other widgets' ordinary long-press policy is unchanged. Preserve `{step}` substitution and firmware interaction behavior; no runtime or native ABI change is authorized. |
| D5 | Approved 2026-10-15 | Use the lifecycle sequence above, browser-check each representative, and delete superseded code within each migrated slice. | Draft restoration reruns action/widget refresh; color helpers reparent shared track controls; catalog loading can race edits. Reuse existing helpers and guards rather than introducing a larger lifecycle framework. |

No nested shape is planned. D1 is a deliberate scope reduction,
not a new compatibility requirement. Revisit it only if a representative exposes
a concrete problem that flat keys cannot solve. The registry's exact JavaScript
API remains a Phase 3 outcome, not a Phase 1 framework deliverable.

KISS principle: preserve active edits, invalid drafts and correct asynchronous
ordering because these protect normal use. Do not introduce recovery stores,
merge-on-save preservation of unsupported fields, missing-ID compatibility
branches, or a generalized lifecycle framework for hypothetical future needs.

Firmware evidence:
[pad_config.cpp](src/app/pad_config.cpp),
[widget_registry.h](src/app/widgets/widget_registry.h),
[table_widget.cpp](src/app/widgets/table_widget.cpp),
[external_widget.cpp](src/app/widgets/external_widget.cpp), and
[mcp_tools_pads.cpp](src/app/mcp_tools_pads.cpp). MCP currently explicitly
advertises flat widget fields; retaining them avoids changing that authoring
contract. Existing schemas and device defaults remain authoritative where used.

### Baseline And Verification Commands

2026-10-15: all nine behavior suites below passed against unchanged production
code. These are a baseline, not proof of future migration correctness.

```bash
node tests/test_portal_pad_dialog_transaction.js
node tests/test_portal_pad_dirty.js
node tests/test_portal_startup.js
node tests/test_portal_action_list.js
node tests/test_portal_pad_import.js
node tests/test_portal_pad_colors.js
node tests/test_portal_pad_preview.js
node tests/test_portal_action_editor_cycle.js
node tests/test_portal_action_picker.js
node tests/test_portal_asset_gates.js
```

The asset-gates suite also passed. It invokes
`bash tools/minify-web-assets.sh esp32-macropad 'ESP32 Macropad'`, which checks
JavaScript syntax and bundle entries, renders partials, minifies assets and
regenerates the ignored asset header. The suite then preprocesses/selects assets
for headless C3, display P4 with PSRAM, and display P4 without PSRAM, checking
decompressed JavaScript syntax. These are asset profiles, not firmware builds.
The P4 profile's two success lines share the same board name in existing output.

No new tests or dependencies were added. Existing transaction tests exercise
input-widget consumption, held actions, numeric normalization and invalid drafts;
startup tests exercise pending catalogs and selection restoration. They do not
replace real-browser testing of refresh order, hidden tabs, DOM reparenting,
stale responses or unavailable configurations. Extend existing suites for those
specific contracts during migration. Phase 1 did not start a portal server or
claim browser/hardware verification.

Under D1, firmware host checks and a firmware build are not needed. If firmware
scope is later justified and approved, select checks for the actual changed
contract: existing `test_action_bindings`, `test_widget_common`,
`test_gamepad_widgets` targets and
[test_extension_config_contracts.sh](tests/test_extension_config_contracts.sh)
are nearby candidates, not complete coverage of every parser. Read the current
board preference at that time and build only `./build.sh <preferred-board>`.

## Verification Rules

Start with the cheapest existing behavior-scoped check after each substantive
edit. Repair and rerun that check before expanding the migration. Extend
existing suites for concrete regression risks; do not add a custom harness or
source-spelling checks merely to prove extraction.

Existing portal entry points include:

* [tests/test_portal_pad_dialog_transaction.js](tests/test_portal_pad_dialog_transaction.js)
* [tests/test_portal_pad_dirty.js](tests/test_portal_pad_dirty.js)
* [tests/test_portal_startup.js](tests/test_portal_startup.js)
* [tests/test_portal_action_list.js](tests/test_portal_action_list.js)
* [tests/test_portal_pad_import.js](tests/test_portal_pad_import.js)
* [tests/test_portal_pad_colors.js](tests/test_portal_pad_colors.js)
* [tests/test_portal_asset_gates.js](tests/test_portal_asset_gates.js)

Run applicable JavaScript suites with `node tests/<suite>.js`, following their
setup requirements. Identify the existing asset syntax/manifest/minification
commands in Phase 1 rather than invent a parallel checker. Use the device-free
portal server for browser checks. Start and provide its URL when executing UI
work, not for this plan-only change.

Firmware changes require focused host checks and `./build.sh <preferred-board>`
under repository build policy. Portal-only phases do not require a firmware
build. Additional board coverage requires approval. Do not add dependencies
unless separately justified and approved.

## Final Acceptance Criteria

These criteria cover the approved portal/editor contract, not complete firmware
interaction independence or hardware acceptance. Evidence and limitations are
summarized in Phase 6; the historical phase checkpoints remain unchanged.

* [x] A widget using existing capabilities needs owned code/markup, optional
  styles, local C++/JS registrations, and one manifest entry, not shared ladders
* [x] Shared fields eliminate ordinary plumbing without a schema language
* [x] Retained settings remain visually editable, including restored Waveform
  controls; approved D2/D4/D14 omissions and simplifications still apply
* [x] Specialized portal code stays with its widget; firmware runtime coupling
  remains explicitly outside this acceptance scope
* [x] Action visibility and serialization use one declared policy
* [x] Screen binding targets work without List-specific action-editor code
* [x] Drafts, dirty state, validation focus, tabs, and capability gates pass the
  recorded focused tests and representative browser workflows
* [x] Firmware parsing reuses existing widget ownership
* [x] Only the current contract is supported: no backward-compatibility or
  migration machinery, and no requirement for existing device configs to work
* [x] No new persisted format is introduced; flat keys and type strings remain
* [x] No unapproved device behavior, Extension ABI, or dependency changes exist
* [x] D2 permits unavailable widget settings to be lost without preservation
  machinery; D3 uses available literal screens or explicit bindings without
  missing-ID retention
* [x] Numeric Rocker saves only tap and adjustment actions, with no firmware
  interaction changes (D4)
* [x] Waveform's portal editor is restored with existing bounds and catalog
  availability, without runtime changes, superseding proposed retirement (D32)
* [x] Required focused tests, asset checks, browser checks, and the applicable
  single-board build are recorded; hardware and full-suite limitations are explicit

## Execution Log

| Date | Phase | Changes and decisions | Verification | Remaining work |
| --- | --- | --- | --- | --- |
| 2026-10-15 | 0 | Interview decisions and phased plan captured | Plan-only checks recorded on completion | User approval; Phase 1 |
| 2026-10-15 | 1 | User authorized Phase 1 and shared canvas. Inventoried 13 editors, shared fill/export/preview decisions, lifecycle and action ownership; recommended retaining flat JSON with no firmware changes. | Nine focused behavior suites passed; asset-gates suite passed all three profiles including minification, syntax and manifest checks. Tracker link/whitespace checks passed. | Decision interview completed in the next entry; no production implementation, browser checks, firmware build or flashing performed |
| 2026-10-15 | 1 | User approved D1..D5 through interview: flat JSON; unavailable widget settings may be dropped; missing screen IDs retained and 31-byte targets enforced; Numeric Rocker saves tap plus adjustment only; incremental lifecycle/browser checks adopted. Phase 1 complete. | Decisions grounded in Phase 1 evidence and Numeric Rocker's widget-owned adjustment behavior; production baseline unchanged | Phase 2 awaits authorization; no firmware interaction or ABI changes authorized |
| 2026-10-15 | 1 | User confirmed happy-path/KISS priority. Clarified D2: no preservation of unused fields for future capability re-enablement. Revised D3 to available literal screens or explicit bindings, superseding missing-ID retention in the preceding entry. Retained length validation, draft safety and lifecycle safeguards. | Plan-only consistency, link and whitespace checks; no production changes | Phase 2 awaits authorization; no implementation or firmware changes authorized |
| 2026-10-15 | 2 | User authorized implementation with mock-server-first validation and proportionate tests. Added Screen-owned Binding value mode; removed List injection and refresh hooks. Shared validation skips disabled binding drafts; mode refresh handles input before change. Summaries show dropdown labels, not internal values. | Eight existing suites passed: startup, action-list, pad transaction, dirty-state, Timer binding, binding help, action picker, and asset gates. Mock-browser checks recorded above. | Phase 3 awaits approval. Device runtime navigation not exercised; no firmware changes, build or flashing. |
| 2026-10-15 | 3 planning | Interview recorded D6..D9: reject invalid numbers; retain conditional disclosure through bounded helpers and widget-owned rules; keep Gamepad Button held action in Content; validate retained feature-disabled values and reveal errors. User explicitly selected decisions only for now. | Confirmed clean worktree at Phase 2 commit `7cd3a84`; inspected shared dialog field/action branches, disclosure rules, workspace drafts, and representative markup. Prior production verification remains as recorded, not rerun in this interview. | Phase 3 implementation awaits separate authorization. Only this tracker changed; no production changes, browser checks, firmware build, or flashing. |
| 2026-10-15 | 3 planning | User approved the KISS revision to D7/D9: stable manually collapsible groups, no feature-dependent hiding or automatic disclosure, no reset on disable, and ordinary validation of shared and selected supported widget settings. This supersedes the conditional-disclosure decision in the preceding entry. | Plan-only link, whitespace, and decision consistency checks; no production verification required. | Phase 3 implementation remains unauthorized. No new reset command, production changes, firmware build, or flashing. |
| 2026-10-15 | 3 | User separately authorized implementation. Extracted Table, Gauge, and Gamepad Button into explicit modules with minimal shared mechanics; removed their superseded shared branches and documented the contract. | Focused host and production asset checks passed; mock-browser draft, validation, save/read-back, remount, and responsive checks recorded above. Timer editor fixture failure also reproduces at HEAD. | Phase 4 awaits authorization. No firmware, ABI, dependency, build, flashing, or commit changes. |
| 2026-10-15 | 3a | User approved the retrospective follow-up. Committed Phase 3 as `889698b`; removed Screen's pad-state dependency, moved validation presentation to hosts, extracted representative CSS, and reused mock bundle serving for CSS manifests. Documented the minimal, provisional contract. | All 13 focused CTest entries passed. Mock-browser host error navigation, blocked invalid writes, corrected pad persistence, and desktop/mobile layout checks passed; Boot mock persistence remains unavailable (404). | Commit Phase 3a separately. Phase 4 remains unauthorized; begin with Rocker/Numeric Rocker after approval. No firmware build or flashing. |
| 2026-10-15 | 4 planning | Confirmed Phase 3a committed as `7d25267` and a clean worktree. User approved D10..D18 and all Phase 4 families, retaining incremental validation. Backfill uses entity-as-enable without firmware changes; explicit defaults, strict integer/timing validation, optional adjustment, and guarded Extension catalog saves agreed. | Targeted portal reads and Sparkline parser/catalog evidence; tracker link/whitespace checks recorded on completion. Prior runtime test results were not rerun or claimed as current evidence. | User requested decisions-only handoff this turn. Begin implementation on a subsequent request with Rocker/Numeric Rocker; Phases 5/6 remain unauthorized. No production changes, browser tests, firmware build, flashing, or commit. |
| 2026-10-15 | 4 review | User approved review clarifications: Extension validation must respect whole-pad persistence and D2; backfill explicitly revises D9; explicit defaults require per-field parser evidence. Added provisional-interface and per-family ownership guardrails without a descriptor framework. | Plan-only local-link and whitespace checks; no production or runtime verification. | Phase 4 remains authorized, with implementation deferred under the decisions-only handoff. Phases 5/6 remain unauthorized. |
| 2026-10-15 | 4 | User requested all Phase 4 steps and continuation after interruption. Migrated the remaining ten editors in five family slices, removed superseded shared ownership, extracted chart styles, updated existing guides, and fixed stale asynchronous draft errors on reopening. | Twelve focused portal scripts passed, including all three production asset profiles. Behavioral catalog/workspace regressions, browser save boundaries, desktop/mobile screenshots, editor diagnostics, and whitespace checks passed; checkpoint details above. | Phase 4 complete. Phases 5/6 remain unauthorized; no final acceptance, device runtime validation, firmware build, flashing, ABI/dependency change, commit, or branch change. |

During execution, record each completed slice, exact checks and outcomes,
approved deviations, format decisions, and unresolved blockers here. Keep the
phase tracker synchronized. Remove this temporary file after completion when
the user agrees; preserve the lasting contract in existing developer docs.
