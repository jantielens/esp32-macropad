---
title: Runtime variables implementation plan
description: Coding-agent handoff for the var binding, Set variable action, catalogs, documentation, and verification.
---

## Goal and approval

Implement device-global, volatile variables using the existing binding and action
systems. The user approved the runtime-only design and the singular binding name
`var`. This is an implementation handoff, not an instruction to implement
persistence or redesign the binding engine.

The primary acceptance scenario is a counter:

```text
Action: Set variable
Name: counter
Value: [expr:[var:counter|0] + 1]

Button label: [var:counter|0]
```

The first activation stores `1`; the next stores `2`. A second button can reset
the counter by setting it to `0`. Neither MQTT nor Home Assistant is required.

Read the repository instructions for bindings, actions, the web portal, and
user documentation before implementing. Preserve existing uncommitted work.
At planning time, the worktree contains unrelated image-fetch and binding
changes, including modifications to binding validation, pad rendering, and
tests. Re-read the touched implementation at execution time instead of applying
patches against an assumed clean baseline.

## Approved behavior

| Concern | Contract |
| --- | --- |
| Read syntax | `[var:name]`, optionally with the existing pipe fallback |
| Naming | Case-sensitive; 1-32 ASCII characters from `A-Z`, `a-z`, `0-9`, `_`, `-`, `.` |
| Name resolution | Literal names only; no nested bindings in a name |
| Scope | One namespace shared across pads and action hosts on the device |
| Lifetime | Survives navigation, pad rebuilds, and configuration reloads; cleared on reboot |
| Creation | First successful assignment creates a variable |
| Values | Strings; expressions interpret numeric values using existing rules |
| Empty values | Valid stored strings, distinct from missing values |
| Missing values | Unavailable status; ordinary `---` or explicit pipe fallback |
| Evaluation | Resolve the action value once when the action executes, then store the result |
| Stored templates | Return stored text literally; never recursively evaluate it |
| Capacity | 32 variables; reject the 33rd distinct name without eviction |
| Updates at capacity | Updating an existing name remains allowed |
| Invalid writes | Leave the previous value and namespace unchanged; return action failure |
| Unchanged writes | Succeed without modifying the value or requesting refresh |
| Formatting | No format parameter in v1; compose with `expr` when needed |
| Deletion | No delete, unset, or clear action in v1 |
| Aliases | Register `var` only, not `vars` |

Use byte capacities that include the terminating NUL. Allocate 33 bytes for a
name. Set the stored-value capacity to 128 bytes, matching the existing
`CONFIG_MQTT_PAYLOAD_MAX_LEN` convention: at most 127 UTF-8 bytes of value.
Keep that limit in a variable-owned constant rather than coupling the store to
MQTT. Use a 192-byte action value-template buffer, matching
`CONFIG_LABEL_MAX_LEN`: at most 191 UTF-8 bytes before evaluation.
Validate both input-template and resolved-value limits without silent truncation.
Do not enlarge shared binding buffers to accommodate this feature. If an
existing resolver imposes a lower limit, retain and document that limit rather
than claiming every expression can produce a 127-byte result.

The limit applies to the complete resolved value, including literal text around
tokens. An explicitly supplied fallback can make an unavailable binding resolve
successfully. A resolver failure without a valid fallback must not store a
placeholder as the new value. Do not infer failure from string contents: literal
values such as `---` remain valid.

## Future persistence boundary

Reserve `[persisted_var:name]` conceptually for a separate persistent namespace.
Do not register it, expose it as supported, or implement it in v1. Do not add
filesystem writes, NVS settings, a scope selector, backend interfaces, or unused
persistence fields.

Keep the runtime store independent of the binding resolver, action payloads,
and pad configuration. A future persistent store can reuse validation and
interaction concepts without changing `[var:...]` lifetime. Future persistence
must use the Storage facade and separately define durability, batching, flash
wear, recovery, and write ordering. Those decisions are outside this task.

## Implementation sequence

### 1. Add the bounded runtime store

Suggested new production files are `src/app/runtime_variables.h` and
`src/app/runtime_variables.cpp`, at sketch root so Arduino compiles them.

* Provide shared name validation, bounded copy-out lookup, and assignment.
  Keep missing, invalid, too-large, full-store, and allocation-failure outcomes
  distinguishable internally.
* Use bounded storage with predictable memory use. A linear search over 32
  entries is sufficient; do not add a general cache or unbounded map.
* Allocate the table on first assignment. On PSRAM builds, use PSRAM only and
  fail without an internal-RAM fallback. On non-PSRAM builds, allocate from the
  heap lazily. Compile no store state when display and button support are off.
* Copy values into caller buffers under synchronization. Never return mutable
  pointers to store-owned memory or hold those pointers across a write.
* Reject allocation failure, if allocation is used, without partial state.
  Report the chosen storage footprint in the implementation handoff.
* Initialize once per boot, not during scheme registration, pad construction,
  or configuration reload. Test isolation must not introduce a public clear
  action or production API solely for the test runner.

Start with the store's boundary behavior and run the cheapest focused host
check before adding the binding or UI integration.

### 2. Register the var binding and its catalog metadata

Suggested new files are `src/app/var_binding.h` and
`src/app/var_binding.cpp`. Use a neighboring local binding as the implementation
pattern, then integrate through
[`binding_builtin_schemes.cpp`](../../src/app/binding_builtin_schemes.cpp).

* Register `var` exactly once through `binding_template_register()` from its
  init function. Add that init call to `binding_builtin_schemes_init()`, not
  directly to `app.ino`.
* Make it available wherever the local binding engine is supported, independent
  of MQTT, HA, filesystem, and PSRAM availability. Retain the existing action
  system boundary of `HAS_DISPLAY || HAS_BUTTON`.
* Require exactly one parameter and validate its name syntax. Unknown names
  are valid authoring input even before a variable exists.
* Reject empty names, invalid characters, nested names, overlong names, and
  extra parameters. Check both firmware and portal authoring validation.
* Use the existing unavailable status for a valid but unset name. Preserve
  empty-string success, checked-resolution overflow behavior, and fallback
  handling. Do not add a private parser for expression nesting or pipe syntax.
* Supply a complete `BindingSchemeSpec` and adjacent `BindingSchemeDoc`:
  category, summary, the name parameter, dynamic key pattern `*`, examples,
  lifetime, bounds, missing-value behavior, and lack of formatting support.
* Use the registry's free-form/dynamic-key capabilities rather than enumerating
  current variables as the set of valid keys. If existing validation metadata
  cannot express the name rule, add the smallest reusable validation contract
  needed and keep firmware and portal behavior consistent.
* Do not classify every variable as numeric. Stored strings have no fixed
  numeric type, unit, or runtime sample. Preserve explicit text/advanced-source
  authoring behavior and do not invent device readings for examples.
* The scheme has no MQTT topics of its own. Nested `var` tokens in expressions
  must continue through existing expression collection/resolution paths.

The binding catalog and live Binding Reference must come from the same registry
metadata. Check the existing consumers in
[`binding_schema.cpp`](../../src/app/binding_schema.cpp),
[`binding_sources.cpp`](../../src/app/binding_sources.cpp), and
[`portal_binding_help.js`](../../src/app/web/portal_binding_help.js).
Do not maintain a second hardcoded scheme list or static reference entry.

### 3. Add the Set variable action

Use the persisted type `set_var`, with string fields `var_name` and `var_value`.
The UI label is **Set variable**; field labels are **Name** and **Value**.
An example persisted action for parser tests is:

```json
{
  "type": "set_var",
  "var_name": "counter",
  "var_value": "[expr:[var:counter|0] + 1]"
}
```

* Add the type constant and bounded payload arm to
  [`pad_config.h`](../../src/app/pad_config.h). Check action size and avoid
  inflating every action unnecessarily.
* Add one self-contained `src/app/actions/set_var_action.cpp` with parse,
  serialize, dispatch, availability, validation, bindable-value handling, and
  `describe()` metadata.
* Call `DEFINE_AND_REGISTER_ACTION_TYPE(...)` once and add the module to
  [`action_modules.inc`](../../src/app/actions/action_modules.inc).
  Do not add action-specific parsing or dispatch ladders to generic files.
* Validate name and value JSON types. Require both fields; an explicitly empty
  `var_value` is valid. Reject malformed or oversized input before copying.
* Only `var_value` is bindable. Keep the name literal. Register the action as
  synchronous and return `ACTION_FAILED` on resolution or assignment failure,
  preserving the existing stop-the-remaining-list behavior.
* Reuse generic text-field action-editor metadata if sufficient. If needed,
  add a dedicated portal action-editor module through the existing extension
  registration and bundle rather than branching in the generic renderer.
* Ensure the action appears through the existing action catalog and MCP
  capabilities. Do not add a new variable-management REST endpoint, MCP tool,
  navigation component, or management panel.

### 4. Make resolve-and-set atomic and network-independent

Inspect the current implementations before selecting the smallest change:

* [`action_dispatch.cpp`](../../src/app/action_dispatch.cpp)
* [`action_dispatch.h`](../../src/app/action_dispatch.h)
* [`action_registry.cpp`](../../src/app/action_registry.cpp)
* [`action_registry.h`](../../src/app/action_registry.h)
* [`action_list.cpp`](../../src/app/action_list.cpp)
* [`binding_template.cpp`](../../src/app/binding_template.cpp)

Binding detection and action resolution currently use `HAS_MQTT || HAS_HA_LIVE`
guards in shared paths. Make local action bindings work with both flags off;
keep network-only topic collection appropriately gated. Audit host source lists
and include guards so this change does not introduce link failures.

The critical section must encompass reading inner variables, evaluating the
template, and committing the result. Current display locking around resolution
alone ends before dispatch and is not sufficient. Two concurrent increments
must not both read `5` and write `6`.

Use the established display/action execution ownership where possible. Binding
resolution uses static buffers and must obey the existing LVGL task or display
lock contract. Also account for physical-button, boot, and other non-LVGL action
hosts, including supported headless builds. Do not assume that every caller is
on the rendering task.

Choose and document one lock order. Do not hold a non-recursive store mutex
while an expression resolver attempts to acquire that same mutex. Do not hold
a spinlock/interrupt-disabled critical section across expression evaluation or
LVGL calls. Avoid broad serialization of network or pausable action work.
Ensure the generic pre-resolution path cannot evaluate this action once outside
the transaction and then evaluate it again inside it. Resolve exactly once.

### 5. Refresh existing binding consumers

Use existing binding polling to make changed values visible without rebuilding
the pad. Do not add an unused change-generation API.
Relevant owners are
[`pad_screen.cpp`](../../src/app/screens/pad_screen.cpp),
[`pad_screen.h`](../../src/app/screens/pad_screen.h), and
[`pad_screen_poll.cpp`](../../src/app/screens/pad_screen_poll.cpp).

* Refresh applicable labels, colors, numeric fields, visibility/interactivity,
  and widget inputs through existing binding paths, including nested expressions
  and named pad bindings that reference variables.
* Keep LVGL mutations on the rendering task. Do not cache pointers to variable
  entries in UI objects or resolve bindings from a store notification callback.
* Respect passive refresh and e-paper waveform limits. A pending change must
  still be observed when refresh becomes eligible; do not force a full waveform
  for every write.
* Coalesce multiple writes, and avoid triggering refresh for identical values.
  Existing polling can satisfy these requirements if it already provides the
  expected cadence; do not add a second event framework.
* Verify device preview through existing binding endpoints. Do not make the
  local portal mock pretend to execute firmware actions or persist variables.

### 6. Update the Binding Reference and fixtures

The live portal Binding Reference renders `GET /api/bindings?docs=1`; the MCP
manifest uses shared registry metadata. The new scheme's documentation is
therefore a runtime implementation deliverable, not deferred prose.

* Extend the production schema profiles in
  [`test_binding_schema_production.cpp`](../../tests/test_binding_schema_production.cpp)
  and their source lists in [`tests/CMakeLists.txt`](../../tests/CMakeLists.txt).
  Include the real binding/store wherever built-in initialization references it.
* Assert presence with MQTT and HA disabled, correct parameter metadata,
  dynamic-name acceptance, examples, and complete documentation.
* Regenerate [`binding-docs.json`](../../tools/mock-data/binding-docs.json) from
  the full-profile executable. Do not edit generated JSON by hand.
* Verify **fx > Binding Reference** can find `var`, displays the name parameter
  and runtime-only semantics, and copies/inserts a valid token.
* Verify the on-device Binding Builder catalog accepts a user-entered name
  that does not yet exist. The source must not depend on live enumeration.
  Check `[var:counter|0]` validates and can be inserted without a numeric sample.
* Extend existing portal binding-help, validation, and action-picker checks
  where behavior changes; avoid tests that assert incidental source spelling.

### 7. Reconcile narrative documentation

Use existing authoritative topics; no new user manual is needed.

| Owner | Required update |
| --- | --- |
| [User binding guide](../user/pads/bindings.md) | Runtime namespace, literal names, lifetime, limits, empty/missing distinction, and fallback |
| [User action guide](../user/pads/actions.md) | Set variable fields, resolve-on-execution behavior, failure behavior, and links to binding syntax |
| [User expression guide](../user/pads/expressions.md) | Counter, reset, and toggle examples using the implemented expression grammar |
| [User data sources](../user/pads/data-sources.md) | Add or link the runtime-variable source if its existing organization requires it |
| [Developer portal reference](web-portal.md) | Explain shared metadata/catalog integration only if the implementation changes its documented contract |
| [Changelog](../../CHANGELOG.md) | Add one scoped unreleased feature entry following the existing convention |

Keep the counter workflow complete: configure Name and Value, save the pad,
activate the button, observe the label, then reset. Explain that saved action
configuration is persistent but the variable's current value is not. Mention
reboot clears values and that a configuration reload without reboot does not.
Add a short toggle expression such as `[expr:[var:enabled|0] == 0 ? 1 : 0]`
only after validating it with the actual expression parser.

Distinguish mutable global variables from pad-local named binding definitions.
Do not advertise future persistence or supported `persisted_var` syntax in user
documentation. Do not expand the AI-assistant guide into a tool catalog;
existing action/scheme discovery supplies this feature without a new tool.

## Focused verification

Extend existing tests first. A new small host target is justified only if the
store or atomic dispatch cannot be exercised through existing interfaces.
Register any permanent test in the normal CMake/CTest suite. Do not create a
custom source-concatenation harness.

| Area | Required observations |
| --- | --- |
| Store | Missing versus empty; replacement; case sensitivity; 32-character name; invalid/overlong name; value boundary; full-store rejection; update while full |
| Binding | Literal return; missing fallback; no recursive evaluation; invalid parameters; checked output overflow |
| Action | Parse/serialize round trip; required string fields; empty assignment; only value bindable; failure preserves old value and stops the list |
| Counter | Undefined to `1` to `2`; reset; two actions in order see the preceding write |
| Atomicity | Competing read-modify-write actions do not lose increments; use the production transaction path |
| Independence | Local expressions and assignment work with `HAS_MQTT=0` and `HAS_HA_LIVE=0`; preserve supported headless action compilation |
| Lifecycle | Pad switches/rebuilds and reloads do not clear state; boot starts empty |
| Presentation | Changed labels and expression-driven state refresh; unchanged writes do not invalidate; throttled refresh eventually observes changes |
| Catalogs | Action discoverable; var scheme available, documented, and accepts arbitrary valid names before assignment |

Existing candidate checks include `test_binding_template`, `test_expr_binding`,
`test_binding_value`, `test_action_parse`, `test_action_bindings`,
`test_action_registry_value_field`, `test_action_sizes`, the binding-schema
profiles, and portal binding-help/action-picker tests. Inspect actual source
ownership: a test that mocks dispatch does not prove atomic production dispatch.
Add only the missing coverage needed for that contract.

Configure and build the touched targets before running their focused CTest
selection. Clear cached warning suppressions if they would mask new warnings.
The following commands identify required gates; include any new focused targets
added during implementation:

```bash
cmake -S . -B build/host-tests
cmake --build build/host-tests --target test_binding_template test_expr_binding test_binding_value test_action_parse test_action_bindings test_action_registry_value_field test_action_sizes --parallel 4
cmake --build build/host-tests --target test_binding_schema_baseline test_binding_schema_display_only test_binding_schema_full --parallel 4
build/host-tests/tests/test_binding_schema_full --emit-json > tools/mock-data/binding-docs.json
bash tests/test_action_catalog_completeness.sh
bash tests/test_mcp_scheme_parity.sh
```

Build and run every existing `test_binding_schema_*` profile affected by shared
built-in registration, not only the three targets listed above. Run
`test_binding_docs_fixture` after regenerating the fixture. Run portal tests
through CTest so the action picker gets its `ACTION_CATALOG_BINARY` environment.
Use `ctest --test-dir build/host-tests -N` to confirm exact names before selecting
tests; do not report an empty regex selection as a pass.

For browser verification, use the existing local portal server or start:

```bash
python3 tools/portal-dev-server.py --port 8765
```

Open the pad editor with a display-capable mock profile. Verify action editing,
save/load round trip, Binding Reference, and Binding Builder at desktop and
mobile widths. Mock metadata proves authoring and rendering, not execution of
firmware state. Verify real counter execution through host production paths
and leave actual device interaction explicitly marked if hardware is unavailable.

This task changes firmware. Consult the stored board preference and build only
the approved verification board:

```bash
./build.sh esp32-p4-lcd4b
git diff --check
```

Do not flash, build all boards, commit, or push. A single display-board build
does not prove headless or e-paper behavior; cover compile gates with focused
host profiles and disclose any unverified hardware behavior.

## Completion criteria and handoff

* The counter example works through the real action/binding path, with no
  integration configured and no lost updates under competing assignment.
* Bounds and errors preserve prior state, including when the store is full.
* The action catalog, binding catalog, live Binding Reference, local fixture,
  and existing MCP metadata all agree on the supported runtime feature.
* Existing user topics explain configuration, save/reboot behavior, and the
  difference between runtime values and named pad definitions.
* Focused behavior/schema/portal checks and the approved single-board build
  pass, or blockers are clearly reported without weakening checks.
* Review the complete task-scoped diff, including new files, and reconcile
  narrative documentation once against that final implementation.
* Report changed owners, memory footprint, synchronization/lock order,
  validation results, pre-existing failures, and remaining hardware checks.

Stop at this boundary. Persistence and any new management surface require a
separate design and approval.