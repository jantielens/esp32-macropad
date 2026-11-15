---
title: Roll Out Interactive E-Paper Responsiveness
description: Coding-agent handover for coherent, interaction-priority presentation on CrowPanel, IT8951 and Inkplate.
agent: agent
ms.date: 2026-11-13
---

## Objective and approval boundary

Roll out Sticky's shared responsiveness contract to the other interactive
e-paper drivers. Make accepted focus, pad navigation, dialog and changed
user-origin local-variable updates render promptly and overtake discretionary
background presentation waits.

Preserve LVGL, the existing display HAL, asynchronous presentation task,
controller protocols and configuration semantics. This is a driver integration
task, not another graphics framework or a panel-speed experiment.

Prepare the scoped implementation proposal first. Follow repository approval
rules before significant source changes; obtain approval for each driver slice
and its verification boards. Once a slice is approved, implement and verify it
end-to-end before proposing the next. Creating or invoking this handover does
not authorize commits, pushes, releases, automatic flashing or all-board builds.

Targets, in order:

1. `crowpanel-4.2-interactive`
2. `reterminal-e1003-interactive` and `waveshare-7.8-interactive`, through their
   shared IT8951 driver
3. `inkplate6flick-interactive`

Do not modify LCD responsiveness. Track that separate measurement-driven work
in [issue #153](https://github.com/jantielens/esp32-macropad/issues/153).
Ignore idle cleanup for this task: no cleanup debt, idle-triggered refresh,
threshold deferral, active/idle power policy or maintenance timer.

## Current baseline and authoritative context

The preparation baseline at handover is commit
`355c624f8aced743244fc47b315904522efea347`. Inspect current HEAD and local edits;
do not reset, revert or overwrite subsequent work to recreate that revision.

Sticky already implements interaction intent, coherent render generations,
prompt task waking/rendering, prompt changed user-variable binding resolution,
interruptible policy waits and immutable presentation ownership. Its newer
board-local changes also include BUSY safeguards, byte-oriented packing,
20 MHz writes, bounded batching and SSD1677/SSD2677 preparation caching.
Preserve those changes; do not copy them to unrelated controllers.

The [historical plan](interactive-epaper-plan.tmp.md) contains useful evidence,
but its phase table, single-row transfer notes, approval restrictions and
current-handoff section predate the later Sticky work. It is not an instruction
to restart that work or implement its deferred phases. This handover's bounded
rollout scope takes precedence over its candidate-work list.

Read [repository guidance](../../../.github/copilot-instructions.md),
[agent guidelines](../../../.github/instructions/agent-guidelines.instructions.md),
[display/touch](../../../.github/instructions/display-touch.instructions.md)
and [OTA activity](../../../.github/instructions/ota-activity.instructions.md)
before editing. Read configuration, action, binding and compile-time contracts
only if the approved implementation actually touches those domains.

### Shared mechanisms to reuse

| Owner | Responsibility |
| --- | --- |
| [DisplayFrame](../../app/display_frame.h) | Accepted interaction origin, request and completed-frame identity |
| [DisplayFramePublisher](../../app/display_frame_publisher.h) | LVGL-owned render lifecycle and safe prompt-render decision |
| [EpaperSchedule](../../app/drivers/epaper_schedule.h) | Framebuffer-mutex-owned generation and presentation policy |
| [DisplayDriver](../../app/display_driver.h) | Existing `coherentFrames`, `beginFrame`, `commitFrame`, `stopPresentation` hooks |
| [Display task](../../app/display_task.cpp) | Dispatch wake, prompt binding/render handling, presentation-task ownership |
| [Display manager](../../app/display_manager.cpp) | Driver setup, lifecycle and cooperative opted-in shutdown |
| [Sticky driver](drivers/display/sticky_epaper_driver.cpp) and [header](drivers/display/sticky_epaper_driver.h) | Working integration reference, not a universal controller implementation |

The existing navigation, dialog and user-origin action paths already publish
appropriate requests. Trace them when verifying integration; do not duplicate
input handlers, change action origin propagation or add another binding-refresh
mechanism merely to integrate a driver.

### Driver owners

* CrowPanel: [implementation](../crowpanel-4.2-interactive/drivers/display/crowpanel42_epaper_driver.cpp)
  and [header](../crowpanel-4.2-interactive/drivers/display/crowpanel42_epaper_driver.h).
* IT8951: [implementation](../../app/drivers/it8951_epaper_driver.cpp)
  and [header](../../app/drivers/it8951_epaper_driver.h).
* Inkplate: [implementation](../inkplate6flick-interactive/drivers/inkplate6flick_epaper_driver.cpp)
  and [header](../inkplate6flick-interactive/drivers/inkplate6flick_epaper_driver.h).

## Required behavioral contract

1. Only the LVGL task performs widget changes and LVGL rendering. External
   tasks publish requests through existing synchronized paths. Never force
   rendering recursively from an input callback, flush callback or ISR.
2. Interaction priority belongs to a successfully changed UI and its committed
   frame. Rejected/no-op input must not make later background work urgent.
   Preserve tap/hold, debounce, cancellation, confirmation generations and
   accepted action count/order.
3. Snapshot only outside a render generation, after the complete multi-flush
   frame has been committed. An older presentation semaphore signal must not
   permit snapshotting halfway through a newer render.
4. Keep one in-flight presentation and bounded latest-frame pending state.
   Never mutate its upload buffer. Completion of an older frame must not clear
   newer pixels, dirty regions, interaction intent or manual full requests.
5. Only successful physical completion validates the presented snapshot.
   During owned hardware work, failed upload, cancellation or uncertain
   completion, snapshot APIs must refuse to report a completed frame.
   Recovery must use a full presentation where the existing baseline is invalid.
6. Interactive frames bypass discretionary cadence and settling, not physical
   BUSY/HRDY waits, required power transitions, controller guard delays,
   startup/recovery/manual full refreshes or periodic cleanup at the existing
   threshold. Do not shorten hardware delays under the name of responsiveness.
7. Release LVGL/framebuffer locks before policy waits and hardware I/O.
   Shared driver state belongs to its existing framebuffer mutex; use minimal
   synchronized copies for cross-task flags. Avoid reversed lock ordering.
8. Policy waiting must wake for committed interactive work and stop requests.
   Do not retain a whole-interval `vTaskDelay` that cannot be interrupted.
   Reuse Sticky's driver-owned wake pattern or an equally small existing path.
   Keep timeout arithmetic rollover-safe and avoid busy polling.
9. Preserve background behavior, configured limits/defaults and no-op skips.
   `EpaperSchedule::wait()` currently embeds Sticky's 500 ms coalescing cap:
   do not silently impose that cap on IT8951 or Inkplate. If a small shared-helper
   extension is needed, preserve Sticky's behavior and each driver's existing
   background policy, including any uncapped settling.
10. Stop/OTA before activation must not start new hardware work. Once activated,
    finish bounded owned completion and required cleanup before freeing buffers,
    deleting tasks or resetting hardware. Do not publish cancelled frames, kill
    an active presentation task or reuse another subsystem's suspension flag.
11. Enable `coherentFrames()` only after the driver fulfills the complete
    publication, waking and cooperative shutdown contract. It is not merely
    a performance flag.

Reuse existing buffers when ownership permits. Do not mandate Sticky's three
full-frame allocations for every driver. Any required additional allocation
needs a concrete ownership justification, PSRAM-first placement where suitable,
allocation-failure handling, cleanup and an explicit memory estimate.

## Required steps

### Step 1: Establish a scoped baseline and obtain approval

Inspect the shared paths, target driver, board overrides and relevant existing
tests. Record actual synchronous/asynchronous upload and physical-completion
behavior, mutex ownership, pending/full/dirty metadata and initialization/deinit.

Run the smallest relevant existing host baseline. Do not alter firmware yet.
Describe the proposed first slice, affected files, expected UX, hardware risks,
memory impact and exact verification commands. Ask for approval and the
verification board(s), then wait.

Distinguish implementation gaps from existing unrelated failures. In particular,
the optional Sticky hardware target has a previously observed BLE-disabled
assertion conflicting with committed `HAS_BLE=true`; verify current state before
diagnosing it, and do not change BLE to make a display check pass.

### Step 2: Integrate and verify CrowPanel

Use the existing HAL hooks, publisher and scheduling helper. Bracket all flush
conversion as one completed generation, wake policy waiting after commit and
retain interactive intent across intervening background commits.

Preserve its drawing/snapshot ownership, 400 x 300 addressing, packed white-bit
semantics, rotations, padding, conversion, full/partial commands and controller
delays. Audit command/refresh BUSY handling before adding stop behavior; do not
copy Sticky's polarity, assertion window, reset sequence or cache eligibility.

Handle cancellation/failure and manual requests through the same ownership
path. Extend the existing CrowPanel test/doubles with coherent-frame, policy
interruption, pending-work and shutdown cases. Validate against Sticky/shared
regressions, build only the approved board, then request device verification.
Report host-only status until physical results are available.

### Step 3: Integrate the shared IT8951 driver

Obtain approval for this slice and its board builds. Cover both interactive
profiles, including diagnostics on/off.

Preserve dirty-region accumulation, native coordinate conversion, alignment,
the existing regional upload path, controller geometry, VCOM and configured
render mode. B/W regional DU and grayscale regional GC16 behavior already exist:
do not replace them with Sticky's whole-frame encoding or grayscale policy.

Retain dirty bounds created while another region is being uploaded/refreshed.
Do not clear or consume newer work after publishing the older snapshot.
Preserve full-request synchronization and distinguish invalid-baseline full
recovery from an ordinary regional update.

Audit HRDY and display-engine completion, cancellation, upload transactions and
deinit. Add the same coherent/policy/ownership regressions to the existing
IT8951 driver tests. Run all four board/diagnostic profiles after changes to
the shared source; physical testing of one profile does not qualify the other.

### Step 4: Integrate Inkplate with a library ownership audit

Obtain approval for this slice and build. Inspect the installed/pinned library
only as needed to establish framebuffer lifetime, transfer completion and safe
stop boundaries.

Preserve library framebuffer bindings (`_partial`, `DMemory4Bit`), mono bit
polarity, grayscale packing, previous-frame bookkeeping, frontlight behavior
and full/partial selection. Do not allow the library to read a drawing buffer
that LVGL can mutate during presentation.

The current wrapper derives success from `einkOn()` while `display()` and
`partialUpdate()` are library calls. Establish what actual completion/error
evidence exists; power-on success alone is not proof of successful presentation.
Never invent success-shaped fallbacks where the library cannot report status.

There is no dedicated Inkplate host driver target at this handover. Explain the
concrete ownership/lifecycle regression and why existing tests cannot cover it
before adding the smallest focused production-path test and necessary doubles.
Register permanent coverage in the normal suite; do not source-strip/concatenate
production code into a separate synthetic harness or build a miniature platform.

If the library cannot support bounded safe ownership/completion without a larger
unapproved change, stop this slice with the precise blocker. Do not enable the
capability prematurely, silently weaken shutdown or claim another driver's
tests prove Inkplate behavior.

### Step 5: Verify shared integration and reconcile documentation

Review all completed, scoped changes, including shared helpers and tests.
Verify Sticky remains behaviorally unchanged; LCD and dedicated E-Paper Frame
drivers must remain opted out of this rollout.

Update the smallest affected owners:

* [Display/touch architecture](../../../docs/dev/display-touch-architecture.md):
  coherent-driver participation, scheduling and lifecycle.
* [Interactive E-Paper user topic](../../../docs/user/specialized/interactive-epaper.md):
  generic priority/cadence behavior only where it changes for users.
* Each affected board README: actual behavior, safety and qualification limits.

No new API, setting, capability flag or generated catalog is expected. If one
becomes necessary, obtain approval and follow its subsystem contract. Do not
bulk-rewrite the historical plan or append a validation diary to user guides.

## Verification commands and test contracts

Run commands from the repository root. Inspect
[test registration](../../../tests/CMakeLists.txt) before selecting targets.
Configure only if needed:

```bash
cmake -S . -B build/host-tests
```

CrowPanel plus the retained Sticky reference:

```bash
cmake --build build/host-tests --parallel 2 --target \
  test_crowpanel_epaper_driver \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_crowpanel_epaper_driver|test_sticky_epaper_driver(_diagnostics)?)$'
```

IT8951, after the shared driver changes:

```bash
cmake --build build/host-tests --parallel 2 --target \
  test_it8951_reterminal-e1003-interactive \
  test_it8951_reterminal-e1003-interactive_diagnostics \
  test_it8951_waveshare-7.8-interactive \
  test_it8951_waveshare-7.8-interactive_diagnostics
ctest --test-dir build/host-tests --output-on-failure \
  -R '^test_it8951_(reterminal-e1003-interactive|waveshare-7\.8-interactive)(_diagnostics)?$'
```

For shared input/publication/lifecycle changes, select the existing relevant
targets together: `test_navigation_wake`, `test_deferred_dispatch_slot`,
`test_pad_resolve_request_lifetime`, `test_pad_confirmation`,
`test_pad_overlay_input`, `test_display_perf` and
`test_display_perf_diagnostics`. Run the relevant existing LCD/input and
frame-device non-regression checks if those shared call paths change.
Add Inkplate's approved coverage to its normal target; do not refer to a
nonexistent pre-existing target.

Focused regressions must exercise observable behavior:

* A pending presentation signal cannot snapshot a partially rendered generation.
* Interaction arriving during policy sleep wakes it promptly, while background
  frames retain the prior board-specific cadence and settling.
* A newer commit, dirty region or manual full request survives an older frame's
  waveform and publication.
* Rejected/no-op/reverted input produces no extra physical update or persistent
  urgency; accepted side effects and confirmations retain order.
* OTA/stop during policy waiting, upload and activated work closes only owned
  transactions, preserves buffer lifetime and keeps snapshots invalid on failure.
* Next presentation after uncertainty performs full recovery where required.
* Initialization failure, allocation failure, restart, rotations, stride and
  grayscale retain their existing behavior.

Use hooks from existing fixtures; do not add tests that assert identifier
spelling, incidental logs or private structure layout. Do not install tooling
unless a chosen check reports a missing dependency or an approved manifest
change requires it.

Firmware changes require the approved single-board build. Follow the repository
board-preference protocol; the previous Sticky preference does not prove the
new drivers compile. Available target commands are:

```bash
./build.sh crowpanel-4.2-interactive
./build.sh reterminal-e1003-interactive
./build.sh waveshare-7.8-interactive
./build.sh inkplate6flick-interactive
```

These are per-slice choices, not authorization to run them all. Ask before
expanding verification beyond the approved board(s). Never run bare `./build.sh`.
Report warnings/errors and fix tightly coupled failures before handing off.
Preserve the prior firmware and new build identity before normal outputs are
overwritten. The user flashes devices; do not erase or flash automatically.

## Physical acceptance and final response

For each available target, capture comparable baseline/candidate workloads:
warm focus movement, touch pad navigation, dialog selection/confirmation and
changed user-variable actions under identical settings and background load.
Use at least 30 eligible samples per measured workload; report median/p95 for
input/request, coherent readiness, policy wait, upload/controller completion
and publication. Measure visible onset separately where possible.

Test updates arriving during a waveform, unchanged frames, existing periodic
cleanup, manual/recovery full refresh, grayscale where supported, all rotations,
OTA/stop recovery and screenshot validity. Keep controller-specific image-quality
and hardware limits separate from host scheduling evidence. Do not manufacture
responsiveness by disabling cleanup or changing persisted defaults.

Final response must state:

1. Which drivers fulfill the complete contract and which are blocked/unverified.
2. Exact focused test and firmware commands with results.
3. Memory/ownership changes and lifecycle failure behavior.
4. Measured stage gains versus expectations, with sample counts and conditions.
5. Updated documentation owners and why other owners needed no changes.
6. Remaining physical qualification and confirmation that LCD waking, idle
   cleanup, clocks/waveforms/caching, commits and releases were not bundled.

---

Begin at Step 1. Prepare and baseline the first driver slice, then obtain its
implementation/build approval before editing firmware.
