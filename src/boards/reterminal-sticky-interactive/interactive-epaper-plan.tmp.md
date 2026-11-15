---
title: Interactive e-paper responsiveness implementation plan
description: Temporary phased coding-agent handoff with acceptance gates and progress tracking.
ms.date: 2026-11-12
---

## Goal and approved scope

Make hardware-button focus/navigation and touch-driven pad, screen and dialog
navigation feel immediate. Prioritize B&W responsiveness during active use, allow
temporary ghosting within qualified limits, and save power when idle.

Build a shared interactive e-paper policy, using Sticky as the first implementation.
Keep LVGL, the existing display HAL and the asynchronous presentation worker.
Do not replace them with CrossPoint's framework.

Milestone 1 software is implemented and host-verified. Device stage captures are
available; controlled latency distributions, visible onset and panel quality
qualification remain pending. This is not hardware acceptance.

After the diagnostics capture, the user approved a separate follow-up:
investigate and optimize render/conversion first, then packing/upload (phase 5).
This does not approve phase 3 policy changes, phase 4 controller caching,
waveform/clock changes or phase 6 rollout.

* Include the interactive Sticky, CrowPanel, IT8951 and Inkplate paths.
* Preserve LCD and dedicated e-paper frame-device behavior.
* Do not expand the first milestone into sliders, local-control redesign,
  optimistic MQTT/Home Assistant state, or faster passive binding polling.
* Preserve tap-on-release, hold thresholds, debounce, input cancellation,
  confirmation safety and action ordering.
* Do not change LUTs, controller geometry, temperature policy or SPI clocks.
* Do not add a new public API, NVS setting, dependency or worker framework unless
  a concrete requirement emerges and the user approves that expansion.

The user can test all repository boards. That does not establish access to both
Sticky controller revisions or authorize an all-board build.

## Evidence and starting point

The [earlier audit](displayimprovements.md) remains the controller-protocol and
vendor-reference baseline. Its packing-first priority is superseded here by the
approved architecture-first sequence; its safety boundaries still apply.

Current code establishes the following:

* [Sticky defaults](board_overrides.h) select 150 ms settling, a 500 ms minimum
  presentation interval, a 100 ms LVGL refresh period and 1 s passive binding
  polling. Persisted configuration can override the presentation interval.
* [Sticky presentation](drivers/display/sticky_epaper_driver.cpp) measures its
  cooldown from refresh completion, takes the maximum of cooldown and settling,
  caps settling at 500 ms, and snapshots drawing into an immutable sending buffer.
* [Panel refresh](drivers/display/sticky_epaper/panel.cpp) initializes every time,
  streams whole comparison frames, then waits for the controller.
* [Display task](../../app/display_task.cpp) gives the presentation semaphore
  after LVGL produces draw data. The driver still receives individual flushes;
  quiet time is not a formal complete-frame publication boundary.
* [Navigation](../../app/display_screen_nav.cpp) queues screen changes and routes
  focus navigation on the LVGL task.
  [Button navigation actions](../../app/actions/button_navigation_action.cpp)
  use the existing deferred display dispatch slot.
* [Pad input](../../app/screens/pad_screen_events.cpp) handles focus and touch
  actions; [dialogs](../../app/device_dialog.cpp) own confirmation generations,
  selection, replacement and cancellation.
* [IT8951](../../app/drivers/it8951_epaper_driver.cpp),
  [CrowPanel](../crowpanel-4.2-interactive/drivers/display/crowpanel42_epaper_driver.cpp)
  and [Inkplate](../inkplate6flick-interactive/drivers/inkplate6flick_epaper_driver.cpp)
  repeat similar scheduling. IT8951 already tracks dirty regions and detailed
  presentation diagnostics. Some driver waits sleep for the entire computed
  interval, so a new interaction must be able to interrupt policy waiting.

The user's working Sticky logs select SSD1677 through BUSY-transition fallback.
This identifies the active software path, not a positive chip-ID read. Preserve
runtime detection and the SSD2677 path; do not hardcode the user's controller.

CrossPoint reference points, inspected before this plan:

* [Render task notification](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/ActivityManager.cpp#L475)
* [Pinned SDK SSD1677 implementation](https://github.com/Free-Ink/freeink-sdk/blob/6a208c0c46cce99bafa334bc4f171f5e558c5195/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp)
* [Pinned SDK SSD2677 implementation](https://github.com/Free-Ink/freeink-sdk/blob/6a208c0c46cce99bafa334bc4f171f5e558c5195/libs/display/FreeInkDisplay/src/driver/Ssd2677Driver.cpp)

These are behavioral comparisons, not permission to copy code or proof of timing
on this hardware. No additional broad CrossPoint research is a prerequisite.

## Coding-agent execution contract

Read the current repository instructions before each phase, especially
[display/touch](../../../.github/instructions/display-touch.instructions.md),
[OTA activity](../../../.github/instructions/ota-activity.instructions.md) and
[agent guidelines](../../../.github/instructions/agent-guidelines.instructions.md).
Read action, configuration, binding and compile-time flag instructions if touching
those contracts.

Implement one phase at a time. Phases 1 and 2 form the first milestone. Record
their measurements and request a checkpoint decision before phases 3 through 6.
Do not turn this plan into an instruction to commit, flash, push or release.

Use the smallest shared mechanism that serves the existing drivers. A small,
header-only e-paper scheduling helper is a reasonable extraction; a generic
priority queue or new scheduler service is not. Any new source must participate
in the existing Arduino compilation path.

### Required invariants

1. LVGL remains owned by its rendering task. External tasks publish requests;
   they do not call render APIs from input ISRs or panel workers.
2. Interaction intent belongs to a successful UI change and its committed frame.
   Do not use a global "everything is urgent for N milliseconds" shortcut.
   Rejected/no-op input must not make unrelated future binding updates urgent.
3. A panel worker may snapshot only a completed render generation. It must not
   copy drawing midway through a multi-flush LVGL refresh, even if an older
   semaphore signal is pending.
4. Preserve driver-specific drawing, in-flight and completed-frame ownership.
   No buffer mutation during transfer; publish a completed snapshot only after
   success. Interrupted/failed presentations require the existing recovery path.
5. Execute all accepted actions in order. Coalesce obsolete visual frames only;
   never coalesce action side effects or confirmation decisions.
6. Permit one in-flight presentation and bounded latest-state pending work.
   A newer interaction survives completion of an older presentation. Preserve
   manual full-refresh requests and newer generations across races.
7. Interactive priority bypasses software policy waits, not BUSY, required power
   transitions, physical minimum intervals, recovery or mandatory full refreshes.
8. No LVGL lock or spinlock is held over panel I/O or a policy sleep. Synchronize
   shared metadata; avoid reversed LVGL/framebuffer lock ordering.
9. Keep OTA safe checkpoints, resource cleanup, timeout reporting, screenshot
   validity and no-op suppression. Do not retry failed hardware indefinitely.

## Phase overview and progress

Order reflects expected benefit, not measured speedup. Later software optimization
can proceed without enabling an unqualified controller experiment.

| Phase | Improvement                        | Prerequisite | State            | Evidence or blocker       |
|-------|------------------------------------|--------------|------------------|---------------------------|
| 1     | Interactive presentation priority  | Baseline     | Hardware pending | Host suite passes         |
| 2     | Prompt coherent LVGL rendering     | Phase 1      | Hardware pending | Host suite passes         |
| 3     | Cleanup outside interaction bursts | Milestone 1  | Not started      | Hardware gate             |
| 4     | Reuse controller preparation       | Milestone 1  | Not started      | Hardware gate             |
| 5     | Faster packing and transfers       | Milestone 1  | Hardware pending | Sticky bytes host-verified |
| 6     | Cross-board rollout and tuning     | Milestone 1  | Not started      | Per-board gates           |

Use `Not started`, `In progress`, `Host verified`, `Hardware pending`,
`Accepted`, `Blocked` or `Deferred`. Host verification is not hardware acceptance.
Phases 3, 4 and 5 are independent after milestone 1; keep their A/B results separate.
Phase 6 can qualify the shared milestone before later optimizations are enabled.

## Baseline and timing contract

Add lightweight stage timing alongside phase 1, capturing an unchanged-firmware
baseline before changing its scheduling policy. Reuse
[display diagnostics](../../app/display_perf.h) and existing logging. Keep detailed
traces bounded and gated by `HAS_DISPLAY_DIAGNOSTICS`; do not log per pixel or
perform I/O while holding a spinlock. Keep production correctness independent
of whether diagnostics are compiled in.

Record input recognition, action dispatch, committed-frame readiness, policy
wait, snapshot/copy, controller preparation, packing/upload, waveform trigger,
waveform completion and completed-frame publication. Carry a request/frame
sequence through coalescing so timings cannot pair unrelated events.

Separate these durations:

* Dispatch to coherent frame ready
* Policy-only wait after a coherent frame is eligible
* Remaining time behind an already in-flight waveform
* Controller preparation and upload before waveform trigger
* Physical waveform and completed-frame publication

Do not call the existing aggregate `present_us` SPI time, or equate a waveform
trigger with visible feedback. Use video or a logic analyzer when measuring
visible onset; BUSY completion is a separate endpoint.

For each measured scenario, collect at least 30 comparable samples and report
median, p95 and maximum. Record firmware revision, board/controller identification,
persisted settings, mode, rotation, temperature, supply, background load and
screenshot polling. Separate first-full, ordinary partial and cleanup samples.

Initial engineering targets, to verify rather than claim:

* Interactive policy delay is zero when the panel is available and the frame is
  committed. No 150 ms settle or configured background cooldown is charged.
* Aim for p95 dispatch-to-ready at or below 50 ms for warm, simple focus changes.
  Report complex pad construction and font misses separately.
* Target p95 wake/re-evaluation overhead at or below 20 ms after an interactive
  frame becomes eligible, excluding active hardware work and RTOS tick rounding.
* Hardware burst tests end on the latest requested focus/screen with no visual
  backlog, lost action or extra activation.

Missed targets require stage evidence and a checkpoint decision, not weaker tests
or an unexplained claim that e-paper is inherently slow.

## Phase 1 - Separate interactive and background presentation

### Implementation

* [x] Capture available baseline and current focused-test results (hardware pending).
* [x] Introduce a small explicit `Background`/`Interactive` intent and committed
  generation contract through
  [the HAL](../../app/display_driver.h),
  [manager](../../app/display_manager.h) and
  [task coordination](../../app/display_task.cpp). Default behavior remains
  unchanged for drivers that do not opt in.
* [x] Establish a synchronized render-start/render-complete publication boundary
  for opted-in buffered e-paper drivers. Use LVGL refresh lifecycle events or
  equivalent production hooks, not diagnostics-only callbacks. Completion must
  follow all flush conversions for that refresh.
* [x] Associate successful focus changes, touch-driven navigation and dialog
  mutations with the next corresponding committed frame. Instrument the
  navigation and dialog owners, not every action type or every pointer event.
  Clear intent for a no-change generation.
* [x] Apply the shared scheduling decision to Sticky first. Keep background
  configuration semantics unchanged in this phase. Interactive frames bypass
  settling and discretionary cooldown; manual/recovery full refreshes retain
  their existing semantics.
* [x] Make policy waits interruptible by upgraded intent, new committed frames,
  OTA and shutdown. Recheck predicates under synchronization after wakeup.
  Signals may coalesce; state/generation remains authoritative.
* [x] Keep existing immutable sending/completed buffers. A worker finishing an
  older frame must not clear newer pending work or its interactive intent.
  Do not allocate a new full-screen buffer merely to transport priority.

### Acceptance and tests

Extend [Sticky driver tests](../../../tests/test_sticky_epaper_driver.cpp) for
background versus interactive waits, background-wait preemption, no-op intent
consumption, and updates arriving during streaming/completion.

Test a multi-flush render with a worker wake between flushes: no snapshot or
physical refresh may contain an incomplete generation. Assert observable frames
and dispatch counts, not private names or log wording.

Existing driver doubles do not run the LVGL render loop. If they cannot cover
the publication boundary, extend existing host-LVGL coverage or add one small
registered integration test. Explain that specific gap before adding a new test
file; do not build another miniature platform or source-extraction harness.

* [x] Background cadence, reverted-change suppression, rotations and Gray4 remain
  covered and unchanged.
* [x] Interactive work overtakes a background policy wait without bypassing
  active panel work.
* [x] OTA/failure still invalidates interrupted snapshots and requires recovery.
* [x] No changes to LCD, frame-device scheduling or tap/hold semantics.

## Phase 2 - Render interaction results promptly

### Implementation

* [x] Wake the LVGL task after publishing display work rather than waiting only
  for its periodic sleep. Preserve the existing
  [deferred slot](../../app/deferred_dispatch_slot.h) ownership, timeout and
  abandoned-context cleanup; do not add a parallel action queue.
* [x] Drain accepted display jobs and apply pending screen/dialog/focus changes
  on the LVGL task. Request a refresh for a changed interactive generation
  without waiting for the normal 100 ms refresh period.
* [x] Refresh only at a safe outer task boundary. Never call a forced refresh
  recursively from a flush or LVGL input callback.
* [x] Ensure changes produced during input processing or
  `currentScreen->update()` receive the prompt render pass. A wakeup alone does
  not make the display refresh timer due.
* [x] Reuse phase 1's completed-generation handoff. Preserve initial binding
  resolution before the first pad frame and existing screensaver wake/input
  suppression.
* [x] Keep idle timer scheduling and tap-flash suppression. Do not permanently
  reduce every timer period or reintroduce transient press-feedback frames.

### Acceptance and milestone 1 gate

Extend [navigation/wake tests](../../../tests/test_navigation_wake.cpp) and the
phase 1 render-boundary coverage for wake-only input, failed navigation, dialog
selection/dismissal, new screen construction and work arriving around task sleep.
Keep deferred-slot and confirmation tests passing.

* [ ] Measure warm focus navigation, touch pad changes, and dialog open/select/
  dismiss, both idle and during an existing refresh.
* [x] Demonstrate in host tests that UI changes no longer wait for the background
  refresh timer, settle delay or cooldown when the panel is free. Physical
  distributions remain pending.
* [x] Demonstrate unchanged action count/order and no stale dialog activation in
  focused host coverage. Physical burst validation remains pending.
* [ ] Record timing distributions and remaining preparation/waveform costs.
* [x] Stop for user review of milestone 1 before later optimization.

## Phase 3 - Schedule discretionary cleanup during idle

### Implementation

* [ ] Separate full-refresh reasons: manual request, first baseline, recovery,
  mode/controller requirement, and periodic ghosting cleanup.
* [ ] Defer only periodic cleanup during an interaction burst. Keep mandatory
  reasons immediate and do not interrupt a waveform that has already started.
* [ ] Record cleanup debt and trigger a worker wake when idle cleanup becomes
  due, even if no new LVGL flush arrives. Clean the latest committed frame,
  including when pixels otherwise compare equal.
* [ ] Bound deferral by both elapsed time and a board-qualified maximum partial
  count. Timer/count arithmetic must tolerate rollover and saturate safely.
* [ ] Preserve threshold `0` as disabling scheduled periodic cleanup, not
  disabling recovery or manual full refresh.

Start hardware experiments with 750 ms of interaction idle and a 3 s maximum
cleanup-debt age. These are candidate policy constants, not validated panel limits.
Initially retain the existing partial threshold as the hard count cap. Increasing
that cap requires a recorded per-board quality result and user acceptance; do not
invent a universal safe count. Without qualification, retain the conservative
count limit and report the resulting responsiveness limitation.

Changing the existing threshold from an exact trigger to a soft cleanup request
is an intentional user-visible change. Update its help/documentation in the same
phase; do not silently reinterpret persisted settings or add a new settings page.

### Acceptance

* [ ] Periodic cleanup occurs after idle without requiring another input/flush.
* [ ] Continuous navigation cannot postpone cleanup past qualified bounds.
* [ ] Manual/recovery refresh, OTA interruption and snapshot invalidation remain
  correct while cleanup is pending.
* [ ] Run at least 50 small B&W changes with unchanged-region photos, alternating
  focus edges and full-screen navigation; record ghosting and contrast.
* [ ] Test policy timing/count boundaries deterministically in existing driver
  coverage. Hardware quality acceptance is separate.

## Phase 4 - Reuse controller preparation while active

### Implementation

* [ ] Begin with consecutive successful Sticky SSD1677 B&W partials only.
  Retain preparation instead of running hardware/software reset each time.
* [ ] Track prepared state in the board-local panel implementation. Invalidate
  it on failure, cancellation, reset, power loss, sleep and incompatible mode/
  refresh transitions. Keep full, Gray4 and uncertain-state paths conservative.
* [ ] Preserve RAM addressing, previous/current comparison, BUSY completion and
  existing waveform selection. A warm controller is not a valid image baseline.
* [ ] Measure preparation savings independently of packing and scheduling.
* [ ] Leave SSD2677 behavior unchanged unless separately qualified. Its retained
  waveform/drive-power lifecycle is a distinct follow-up, not a consequence of
  SSD1677 success.

Any keep-powered active/idle policy must have a bounded idle transition that
actually runs without another flush. Preserve safe OTA/deinit ownership and
power cleanup; do not leave drive rails active indefinitely. If existing hardware
sequencing cannot support that safely, defer this power-policy extension.

### Acceptance

* [ ] Host tests cover cold/full/partial/mode changes and invalidation on errors,
  OTA, cancellation and reset. The next uncertain-state update is full.
* [ ] Hardware A/B tests show retained preparation improves timing and preserves
  unchanged pixels across repeated partials, restart and recovery.
* [ ] SSD2677 tests remain passing without assuming its hardware was tested.
* [ ] Do not enable an unqualified shortcut by default; record it as deferred
  rather than adding an experimental public toggle.

## Phase 5 - Reduce packing and transfer overhead

### Implementation

* [x] Profile conversion, packing, transfer and deliberate yields separately.
  Device stage measurements are recorded in the second capture below; controlled
  distributions and physical-visible timing remain pending.
* [x] Replace Sticky per-pixel controller packing with byte-oriented operations
  where equivalent, then evaluate bounded multi-row transfer chunks.
  Keep single-row transactions for now: current measurements combine packing,
  SPI and yields and do not justify weakening checkpoint granularity or adding
  a larger buffer. Re-evaluate only after the new separated device capture.
* [x] Preserve bit order, native mirroring, old/new transitions, Gray4 inversion,
  CS/transaction semantics, OTA checks and watchdog/scheduler fairness.
* [ ] Reuse IT8951's existing regional transfer path rather than adding another
  generic dirty-region engine.
* [ ] Consider other regional transfers only after the above results justify
  them. SSD2677 remains whole-frame unless independently supported and qualified;
  SSD1677 windowing is a separate hardware-gated experiment.

### Acceptance

* [x] Extend existing driver tests with byte-equivalent representative old/new
  patterns, unaligned edges, rotations and Gray4 planes.
* [x] Keep cancellation granularity bounded during larger transfers.
  Transfers remain one row; OTA injected on the first row stops before row two.
* [ ] Report CPU/transfer savings separately from unchanged physical waveform
  time. Check internal RAM, PSRAM and stack cost.
* [x] Skip screenshot optimization unless measurement shows screenshot polling
  materially blocks interaction; it is not a prerequisite for this milestone.

## Phase 6 - Roll out the shared policy across interactive boards

### Implementation

* [ ] Adopt the shared intent/publication/scheduling contract in CrowPanel,
  both IT8951 board profiles and Inkplate, without copying another policy loop.
* [ ] Preserve each driver's buffers, real hardware timing constraints, dirty
  alignment, waveforms and grayscale behavior. Do not normalize these to Sticky.
* [ ] Ensure long policy waits can be interrupted and idle maintenance can run
  without a new render on every opted-in driver.
* [ ] Qualify each board independently; enable only accepted controller-specific
  optimizations. Keep unsupported optimizations conservative.
* [ ] Reconcile final documentation and generated metadata only where changed.
  Record LCD/frame-device non-regression evidence.

### Board qualification checklist

* [ ] Sticky SSD1677, the user's currently working path
* [ ] Sticky SSD2677, only if a distinct unit/revision is available
* [ ] CrowPanel 4.2 interactive, SSD1683
* [ ] ReTerminal E1003 interactive, IT8951
* [ ] Waveshare 7.8 interactive, IT8951
* [ ] Inkplate 6 Flick interactive

For each board, record status as host-only, hardware-pending or hardware-accepted.
Do not infer grayscale partial support or physical quality from another board.

## Verification commands and documentation ownership

Run the smallest phase-relevant subset of existing checks first. Configure when
needed, then build only selected test targets:

```bash
cmake -S . -B build/host-tests
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_navigation_wake test_deferred_dispatch_slot
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_sticky_epaper_driver|test_navigation_wake|test_deferred_dispatch_slot)$'
```

Additional existing coverage to select when affected, registered in
[tests/CMakeLists.txt](../../../tests/CMakeLists.txt):

* `test_crowpanel_epaper_driver`
* `test_it8951_reterminal-e1003-interactive` and its `_diagnostics` variant
* `test_it8951_waveshare-7.8-interactive` and its `_diagnostics` variant
* `test_display_perf`, `test_display_perf_diagnostics`, `test_display_perf_api`
* `test_pad_cycle`, `test_pad_confirmation`, `test_pad_overlay_input`
* `test_pad_refresh_schedule`, `test_gt911_touch`, `test_touch_manager_snapshots`
* `test_epaper_macro_contract`, `test_portal_epaper_config`

Do not claim these existing checks cover the new publication/scheduler contract
until relevant behavioral assertions have been added. There is no dedicated
Inkplate driver target in the inspected registration; justify focused additional
coverage if required, and retain hardware validation.

Firmware/native HAL changes require a firmware build. Follow the board-preference
workflow in the agent guidelines; no preference file was present when drafting.
Ask which verification board to use and save that preference before the first
build. Sticky is the recommended initial candidate. Run only the chosen board:

```bash
./build.sh reterminal-sticky-interactive
```

The command above is a proposed verification command, not an executed build or
authorization to flash. Ask before expanding firmware builds to additional boards.
Never use bare `./build.sh` without an explicit all-board request.

Before handing off an implementation as commit-ready, reconcile affected owners:

* [Display/touch architecture](../../../docs/dev/display-touch-architecture.md)
  for intent, task wakeup and frame publication
* [Interactive e-paper user topic](../../../docs/user/specialized/interactive-epaper.md)
  for changed background/interactive cadence and cleanup behavior
* [Dialogs and alerts](../../../docs/user/pads/dialogs-and-alerts.md) only if its
  documented behavior changes
* The relevant board README for qualified power/controller limitations
* Portal help and configuration tests if a setting's meaning changes; API docs
  only if the API contract changes

Use compile-time flag and generated-driver-table workflows if flags or board
selection actually change. Do not add flags or regenerate unrelated catalogs
merely to satisfy a checklist. Leave the historical audit unchanged unless a
later user request explicitly includes reconciling it.

## Progress and outcome tracking options

Use this file as the default single source of implementation progress. Select
additional tracking only when useful:

* Check the phase items and update the overview for lightweight tracking.
* Add an outcome entry below after each phase or blocked attempt.
* For multi-session work, carry the current phase, last passing command and next
  gate in the handoff; optionally mirror phase IDs in the coding agent's task
  tracker. Avoid a second conflicting plan.
* If the user requests durable backlog tracking, mirror accepted phases into
  GitHub issues. Do not create issues, commits or PRs automatically.

### Phase 1 outcome, 2026-11-12

* Baseline: `ba6672ee`; worktree initially contained only this untracked plan.
* Unchanged baseline command:
  `cmake --build build/host-tests --parallel 2 --target test_sticky_epaper_driver test_navigation_wake test_deferred_dispatch_slot`
  passed. Then
  `ctest --test-dir build/host-tests --output-on-failure -R '^(test_sticky_epaper_driver|test_navigation_wake|test_deferred_dispatch_slot)$'`
  passed 3/3 (0.47 s total, not a device latency measurement).
* Phase 1: `cmake -S . -B build/host-tests` passed; the same build and
  CTest commands passed 3/3 after extension (0.63 s total).
* Added opt-in HAL lifecycle hooks, shared scheduling/frame publication helpers,
  Sticky wakeable policy waits, UI-owner intent, bounded diagnostics and graceful
  presentation-worker shutdown. No controller detection or waveform changes.
* Extended the existing Sticky test with real host LVGL rather than adding a
  separate platform/harness. Physical snapshots remain immutable and are exposed
  only after successful hardware completion.
* Host scenarios cover both controller paths, background cadence, interaction
  preemption, multi-flush stale wakes, no-op/reverted intent, work/manual refresh
  arriving during transfer, OTA, shutdown, failures, rotations and Gray4.
* Physical baseline and stage distributions: pending, zero hardware samples.
  No serial capture, analyzer trace, temperature/supply measurements or physical
  controller access is available in this session. The user's SSD1677 BUSY
  fallback identification is preserved, not reinterpreted as a chip-ID read.
* Verification preference was absent at literal `/memories/board-preferences.md`.
  The existing durable file at `~/.copilot/memories/board-preferences.md` was found
  and updated to the user's explicit selection, `reterminal-sticky-interactive`.
  Firmware build follows phase 2; no flash is authorized.

### Phase 2 and milestone 1 outcome, 2026-11-12

Status: Hardware pending. Phases 1 and 2 are implemented; phases 3-6 remain
unstarted. The later cross-board host checks are compatibility checks, not a
phase 6 rollout.

Changed owners:

* HAL/task coordination: `display_driver.h`, `display_manager.{h,cpp}`,
  `display_task.cpp`, `display_frame.h`, `display_frame_publisher.h`,
  `deferred_dispatch_slot.h`
* UI intent: `display_screen_nav.cpp`, `screens/pad_screen_events.cpp`,
  `device_dialog.cpp`
* Scheduling and diagnostics: `drivers/epaper_schedule.h`, Sticky's driver and
  panel implementation, `display_perf.h`
* Focused existing tests and their CMake registration; no new test source or
  standalone platform harness
* Display/touch architecture, interactive e-paper user topic, Sticky README,
  presentation-setting help, and the generated flag-use reference

Deferred-slot publication now notifies the opted-in LVGL consumer after the
accepted job becomes visible. Existing context ownership, completion, timeout,
abandoned cleanup and action ordering remain in that slot. Task notifications
survive publication around task sleep; idle deadlines remain unchanged.

The outer render boundary requests `lv_refr_now()` for a changed interactive
generation after input timers and screen updates. Queued navigation attaches
intent only when its target is applied, preserving initial binding construction.
Dialog selection, dismissal, failed replacement, stale callback and continuation
safety remain covered. No raw pointer press, wake-only input or rejected
navigation is promoted to an interactive generation.

Coherent completion wakes manual work even if a refresh converted no pixels.
This closes the stale-wake/no-flush race without another queue or framebuffer.
Graceful teardown drains accepted dispatch work, wakes policy waiting and
finishes the driver's owned bounded power cleanup before worker deletion.

#### Exact validation commands and results

Configuration passed:

```bash
cmake -S . -B build/host-tests
```

The phase 2 build passed:

```bash
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics \
  test_navigation_wake test_deferred_dispatch_slot test_stopwatch_round_ui \
  test_pad_confirmation test_pad_overlay_input test_display_perf \
  test_display_perf_diagnostics
```

The corresponding phase 2 CTest selection passed 9/9:

```bash
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_sticky_epaper_driver(_diagnostics)?|test_navigation_wake|test_deferred_dispatch_slot|test_stopwatch_round_ui|test_pad_confirmation|test_pad_overlay_input|test_display_perf(_diagnostics)?)$'
```

Additional host targets built successfully:

```bash
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics \
  test_stopwatch_round_ui test_crowpanel_epaper_driver \
  test_it8951_reterminal-e1003-interactive \
  test_it8951_reterminal-e1003-interactive_diagnostics \
  test_it8951_waveshare-7.8-interactive \
  test_it8951_waveshare-7.8-interactive_diagnostics \
  test_epaper_frame_battery test_epaper_frame_media_conformance
cmake --build build/host-tests --parallel 2 --target test_pad_cycle
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics \
  test_pad_resolve_request_lifetime
```

The final rebuild and combined selection passed 25/25 (22.76 s total host test
runtime, not panel latency):

```bash
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_sticky_epaper_driver(_diagnostics)?|test_navigation_wake|test_deferred_dispatch_slot|test_pad_resolve_request_lifetime|test_stopwatch_round_ui|test_pad_confirmation|test_pad_overlay_input|test_display_perf(_diagnostics|_api)?|test_pad_cycle|test_pad_refresh_schedule|test_epaper_macro_contract|test_portal_epaper_config|test_crowpanel_epaper_driver|test_it8951_.*|test_epaper_frame_battery|test_epaper_frame_media_conformance|test_gt911_touch|test_touch_manager_snapshots|test_display_safe_telemetry)$'
```

Only the selected firmware board was built:

```bash
./build.sh reterminal-sticky-interactive
```

The first attempt failed on duplicate header definitions across Arduino's
mirrored sketch/repository include paths. Explicit include guards fixed it.
Subsequent builds passed. The final rebuild after diagnostics byte accounting
also passed: 3,177,926-byte sketch, 80,572 bytes of globals, no compiler warnings
or errors in the captured output. The generated flag reference was unchanged
on this final run.

```bash
git diff --check
```

Passed. No existing Markdown linter or link-check command was found; narrative
edits retain existing link destinations. No dependency was installed for prose.

#### Corrections and deviations

* The expanded macro-contract test initially rejected an `EPAPER_*` include
  guard as an unscoped configuration macro. Renaming the guard fixed the test;
  no feature flag or board selector changed.
* The first combined 25-test run failed the two Sticky variants on an idle
  timer assertion. The fixture had inherited LVGL's startup-ready timer origin;
  resetting that origin before the warm 99/100 ms boundary test fixed the
  fixture. The assertion was retained, not weakened. Final result: 25/25 pass.
* A real host-LVGL multi-flush check extends the existing Sticky test and uses
  the same publication/prompt-render helpers as production. It is not a
  substitute for the real FreeRTOS loop, touch timing or glass validation.
* Timing starts at UI-owner entry/mutation, not an input ISR. Request/frame
  sequences correlate committed readiness, eligibility, snapshot, preparation,
  packing/upload, waveform trigger, BUSY completion and publication. Raw input
  recognition and visible onset still require correlated hardware traces.
* `eligible = max(ready, preceding hardware completion)` separates waiting
  behind older hardware from subsequent policy/wakeup time. `upload_us`
  includes packing, SPI and cooperative yields, not wire-only time.
* The generated compile-time reference adds the four new Sticky diagnostics
  use sites. No flag defaults, report classification, driver table, public API,
  NVS schema, controller detection, cache or waveform was changed.

#### Measurements and remaining hardware gate

No measured physical latency improvement is claimed. Hardware samples: **0**.
Median, p95 and maximum for dispatch-to-ready, eligible-to-snapshot,
preparation/upload, waveform, publication and input-to-visible onset are all
**pending**. Temperature, supply, background load and screenshot-polling
conditions are likewise unrecorded.

Deterministic host contracts pass: interactive policy time is zero when free,
background cadence still applies, policy waiting is preemptible, prompt
rendering does not wait for the 100 ms timer, and idle/background work still
respects the warm timer boundary. Host-clock assertions do not establish the
50 ms dispatch or 20 ms RTOS wake p95 targets.

Remaining bottlenecks are intentionally unchanged: whole-frame comparison/copy,
controller reset/preparation on every update, 96,000 payload bytes plus packing
and cooperative yields, physical BUSY/waveform duration and mandatory full
cleanup. Screenshot copying can still hold the driver mutex; no screenshot
optimization was started.

Before hardware acceptance:

* Capture unchanged/revised firmware A/B traces under matching conditions, at
  least 30 samples per focus, touch navigation and dialog open/select/dismiss
  scenario, separately idle and behind an active waveform
* Verify latest-state burst completion, action count/order, wake-only
  suppression, confirmation replacement/cancellation and screenshot correctness
* Exercise OTA during policy waits, upload and BUSY; verify full recovery and
  SSD2677 bounded drive cleanup on an identified unit
* Run repeated partial-quality checks and distinguish ordinary partials,
  initial full and periodic/manual cleanup
* Preserve the working SSD1677 BUSY-fallback path; qualify SSD2677 separately
  if a distinct unit is available

No flash, all-board build, commit or push was performed. Stop here before any
phase 3 cleanup-policy or phase 4 controller-preparation experiment.

### Diagnostics follow-up, 2026-11-12

The user reports working firmware but no noticeable responsiveness improvement.
This is functional feedback, not physical latency or image-quality acceptance.
At the user's request, Sticky now defaults `HAS_DISPLAY_DIAGNOSTICS` to true;
explicit test/build overrides can still disable it. Other board defaults are
unchanged. Stage logs are split by frame sequence to fit the logger's 192-byte
message buffer instead of truncating the completion timestamps.

Checks:

* `cmake --build build/host-tests --parallel 2 --target test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics`
  passed
* `ctest --test-dir build/host-tests --output-on-failure -R '^test_sticky_epaper_driver(_diagnostics)?$'`
  passed 2/2
* `python3 tests/test_firmware_size_report.py` passed 28/28
* After splitting the log,
  `cmake --build build/host-tests --parallel 2 --target test_sticky_epaper_driver_diagnostics`
  and `ctest --test-dir build/host-tests --output-on-failure -R '^test_sticky_epaper_driver_diagnostics$'`
  passed
* `git diff --check` passed
* `./build.sh reterminal-sticky-interactive` passed, then passed again after
  the log split to verify the final artifact: diagnostics active, 3,185,410-byte
  sketch, 81,876 bytes of globals. No compiler warnings or errors were reported.
  The generated flag reference now shows Sticky diagnostics enabled.

No flash performed. Physical stage measurements were pending at this checkpoint.

### Approved render and packing follow-up, 2026-11-12

Status: Host verified; hardware pending. This separately approved work follows
milestone 1; it does not start cleanup policy, controller caching or cross-board
rollout. Runtime SPI-ID/BUSY controller selection, SSD2677 compatibility,
waveforms, clocks, input/action semantics, frame publication and screenshot
ownership are preserved.

#### Device baseline supplied by the user

The serial capture identifies ID `0xff` followed by the SSD1677 BUSY fallback.
Settings: B&W, Bayer (`bw_conversion=1`), rotation 1 (480 x 800 logical),
500 ms background minimum, 1000 ms binding interval, full threshold 200.
Wi-Fi is active; MQTT/HA are disabled in configuration. Temperature, supply,
visible onset and image-quality measurements are unavailable.

Six correlated requests give the following durations, in milliseconds.
Scenarios are mixed, not a statistically qualified latency distribution.

| Request/frame | Request to ready | Eligible to snapshot | Preparation | Packing/upload | BUSY | Request to publication |
|---------------|------------------|----------------------|-------------|----------------|------|------------------------|
| 1/68 | 628.405 | 0.419 | 22.445 | 439.802 | 611.961 | 1709.089 |
| 2/70 | 498.120 | 0.104 | 22.630 | 448.244 | 611.057 | 1586.387 |
| 3/71 | 608.784 | 0.116 | 21.985 | 441.797 | 609.974 | 1687.847 |
| 4/73 | 486.233 | 0.109 | 22.221 | 448.190 | 610.959 | 1573.786 |
| 5/74 | 1908.105 | 0.105 | 23.417 | 443.300 | 611.474 | 2993.966 |
| 6/76 | 496.043 | 14.008 | 23.454 | 446.829 | 611.910 | 1600.063 |

Requests 1/3 open dialogs; 2/4 dismiss notifications; 5 constructs an uncached
pad; 6 returns to a cached pad behind older hardware work. Dialog font loading
adds approximately 49-55 ms plus 13 ms cache creation. Touch-task gaps during
rendering confirm that rendering remains blocking. Existing telemetry cannot
separate LVGL drawing/glyph work from conversion, or packing from SPI/yields.
Preparation caching could save only about 22 ms in these samples. The ideal
96,000-byte payload at 10 MHz is 76.8 ms of wire time, not measured attainable
upload time. This evidence motivates render/conversion first, then packing.

#### Implementation and bounded deviations

* [x] Capture the current worktree and passing Sticky diagnostics-on/off baseline
* [x] Traverse packed native bytes during conversion, loading/merging/writing
  each byte once, including partial edges, rather than repeatedly touching
  rotated PSRAM pixels
* [x] Reuse IT8951's exact weighted RGB565 channel table through the existing
  monochrome helper; no luminance, quantization or dithering changes
* [x] Validate conversion before changing panel packing: diagnostics on/off pass
  scalar-reference screenshots and wire bytes for both controllers, all
  rotations, threshold/Bayer/blue-noise, swapped/padded input, clipping and Gray4
* [x] Replace controller per-pixel lookups with mono byte reversal, SSD2677 bit
  spreading/old-new interleave and Gray4 level/plane extraction
* [x] Keep the same 200-byte maximum row buffer, SPI transactions, every-row
  cancellation and every-16-row yields; do not introduce multi-row batching
* [x] Add frame-correlated render/flush/glyph, conversion/framebuffer wait, and
  packing/SPI/yield timing using existing diagnostics
* [x] Capture post-change ESP32 stage timings
* [ ] Complete controlled physical A/B acceptance, visible onset and quality

The existing Sticky test now compares whole screenshots and complete controller
payloads against scalar conversion/packing references. Its large patch visits
every RGB565 value; subsequent clipped patches exercise byte/nibble boundaries
and byte swapping. Two generations exercise previous/current transitions, and
threshold-zero all-white generations exercise no-op suppression. Existing OTA,
failure and teardown scenarios remain, with an explicit one-row OTA cancellation
assertion. No new test file, platform harness, dependency, API or setting was
added.

The existing test executable also accepts an opt-in `--benchmark` mode, not a
timing assertion in CTest. One warm-up and 30 measured runs use the same host
build settings before/after. CMake build type and extra C++ flags are empty
(unoptimized host build), diagnostics off; hardware delays/SPI are mocked.
Conversion uses complete B&W threshold frames in 10-row strips. Mock-panel
timings include the test's SPI command capture and exclude real wire/BUSY/yield
delays, so they are not controller upload timings.

| Host operation | Before median/p95/max (ms) | After median/p95/max (ms) |
|----------------|---------------------------|--------------------------|
| Conversion, rotation 0 | 11.862 / 12.084 / 12.184 | 4.725 / 4.834 / 4.847 |
| Conversion, rotation 1 | 12.139 / 12.347 / 12.358 | 4.950 / 5.048 / 5.065 |
| SSD1677 mono mock-panel | 12.457 / 12.672 / 13.209 | 2.159 / 2.218 / 2.223 |
| SSD1677 Gray4 mock-panel | 7.259 / 7.665 / 7.839 | 2.897 / 2.986 / 2.997 |
| SSD2677 mono mock-panel | 7.230 / 7.328 / 7.805 | 2.379 / 2.463 / 2.627 |
| SSD2677 Gray4 mock-panel | 4.395 / 4.629 / 5.072 | 1.914 / 1.982 / 2.014 |

These are host CPU trends only, not measured firmware latency improvements.
Native framebuffer allocations and payload sizes are unchanged. The shared
luminance table is 256 read-only bytes; diagnostics add small fixed counters
and a frame refresh trace, not another framebuffer or task. Stack high-water,
heap behavior, power and real-time fairness still require hardware validation.

#### Follow-up validation

The baseline, conversion-only, scalar-reference and final packing checks used:

```bash
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics
ctest --test-dir build/host-tests --output-on-failure \
  -R '^test_sticky_epaper_driver(_diagnostics)?$'
build/host-tests/tests/test_sticky_epaper_driver --benchmark
```

Final targeted result: 2/2 pass. Benchmarks completed successfully. The first
reference run expected a second transfer for a threshold-zero all-white frame;
the implementation correctly suppressed it. The fixture now checks unchanged
output and no transfer, rather than requiring redundant panel work.

The combined regression build initially included the command-only
`test_pad_refresh_schedule` as a build target and stopped with "No rule to make
target". The corrected command builds executable targets and lets CTest run
command-only checks:

```bash
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics \
  test_navigation_wake test_deferred_dispatch_slot test_pad_resolve_request_lifetime \
  test_stopwatch_round_ui test_pad_confirmation test_pad_overlay_input \
  test_display_perf test_display_perf_diagnostics test_pad_cycle \
  test_crowpanel_epaper_driver test_it8951_reterminal-e1003-interactive \
  test_it8951_reterminal-e1003-interactive_diagnostics \
  test_it8951_waveshare-7.8-interactive \
  test_it8951_waveshare-7.8-interactive_diagnostics \
  test_epaper_frame_battery test_epaper_frame_media_conformance
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_sticky_epaper_driver(_diagnostics)?|test_navigation_wake|test_deferred_dispatch_slot|test_pad_resolve_request_lifetime|test_stopwatch_round_ui|test_pad_confirmation|test_pad_overlay_input|test_display_perf(_diagnostics|_api)?|test_pad_cycle|test_pad_refresh_schedule|test_epaper_macro_contract|test_portal_epaper_config|test_crowpanel_epaper_driver|test_it8951_.*|test_epaper_frame_battery|test_epaper_frame_media_conformance|test_gt911_touch|test_touch_manager_snapshots|test_display_safe_telemetry)$'
python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md
python3 tests/test_firmware_size_report.py
```

Build passed; CTest passed 25/25; flag snapshot generation passed; size-report
tests passed 28/28. IT8951 regional transport, LCD behavior and dedicated frame
paths were not redesigned. These compatibility checks are not a phase 6 rollout.

Preferred board remains `reterminal-sticky-interactive`, read from the existing
`~/.copilot/memories/board-preferences.md` (literal `/memories` is unavailable).
The final firmware command was:

```bash
./build.sh reterminal-sticky-interactive
git diff --check
```

Both passed. The build enabled diagnostics, produced
`build/reterminal-sticky-interactive/app.ino.bin`, and reported a 3,186,994-byte
sketch and 81,876 bytes of globals. A scan of the captured build output found
no `warning:` or `error:` diagnostics. Compared with the previous diagnostics
firmware, flash increased by 1,584 bytes and reported globals are unchanged;
this is not a measurement of peak heap/stack use. The generated flag report
was unchanged by the build.

After adding the explicit one-row OTA cancellation assertion, the final
two-target build and Sticky CTest commands above passed again (2/2). No
production source changed after the successful firmware build. Whitespace
validation passed. Existing documentation links are retained; no Markdown
linter or link checker was installed. No flash, all-board build, commit or
push was performed.

#### Remaining device verification

Repeat the captured dialog open/dismiss and cached/uncached pad scenarios with
the same settings. Correlate frame IDs across `Display` refresh/render/flush/
glyph records and `Sticky` conversion/wait, preparation/upload, packing/SPI/
yield and completion records. Render includes flush and glyph work; these
durations overlap and must not be added together. SPI includes transaction/
GPIO overhead. Existing request timestamps remain UI-owner timestamps, not raw
touch onset.

At the software-build checkpoint, post-change request-to-ready, upload,
publication and physical-visible latency were pending; no device speedup was
claimed. Font loading/LVGL drawing, uncached
pad construction, unchanged full-frame wire volume, scheduler yields and the
approximately 611 ms partial waveform remain possible bottlenecks. Verify OTA/
recovery, orientation/unchanged pixels, screenshots, action ordering and
confirmation safety on glass. Qualify SSD2677 separately if a unit is available.

### Post-optimization device capture, 2026-11-12

Status: Device stage improvement measured; hardware acceptance still pending.
The second user-supplied capture contains firmware ELF prefix `0f755a7b6`,
runtime SSD1677 selection through ID `0xff`/BUSY fallback, the same B&W Bayer
settings and rotation 1, and ten complete correlated interaction presentations.
All ten report `ok=1` and 96,000 payload bytes. There is a boot-log history gap;
do not treat this as a complete stress/recovery trace. No source was changed,
firmware built or device flashed during this analysis.

The two read-only user captures were parsed locally with Node.js. Numeric
`key=value` records were joined by frame ID, then filtered for nonzero
`request_seq` and successful publication. Durations use microsecond event
timestamps, not the rounded log prefix. Before/after requests were grouped by
scenario, not request number. Medians below describe these small captures only;
they are not controlled p95/max acceptance distributions.

| Scenario | Before/after samples | Request to ready, before/after (ms) | Request to publication, before/after (ms) |
|----------|----------------------|------------------------------------|-----------------------------------------|
| Dialog open | 2 / 3 | 618.5945 / 394.039 | 1698.468 / 1222.843 |
| Notification dismiss | 2 / 3 | 492.1765 / 283.347 | 1580.0865 / 1115.157 |
| Return to cached pad 0 | 1 / 2 | 496.043 / 285.912 | 1600.063 / 1110.752 |
| First uncached pad 15 | 1 / 1 | 1908.105 / 1600.020 | 2993.966 / 2431.723 |

Old dialog-open requests 1/3 match new 3/5/9; old dismiss 2/4 match new 4/6/10;
old cached-return 6 matches new 2/8; old uncached-pad 5 matches new 1.
The old cached-return sample included older presentation activity, so it is
not a perfectly matched idle A/B sample.

Dialog open/dismiss improve request-to-publication by approximately 465-476 ms
(28-29%) in these captures. These are measured device pipeline improvements,
not physical input-to-visible-onset measurements. The old capture did not
separate conversion from LVGL work, so it cannot establish a conversion-only
before/after speedup.

Across the ten new interaction frames:

| Stage | Median (ms) | Observed range (ms) |
|-------|-------------|---------------------|
| RGB565 conversion | 215.232 | 212.730-217.204 |
| Ready/eligible to snapshot | 0.394 | 0.383-0.465 |
| Controller preparation | 22.3855 | 21.924-24.244 |
| Packing | 24.0375 | 23.448-26.025 |
| SPI transactions | 95.5145 | 94.090-96.696 |
| Deliberate yields | 67.3855 | 62.049-72.301 |
| Whole upload | 190.292 | 183.716-194.239 |
| Trigger to BUSY done | 611.6965 | 610.416-612.034 |

Whole upload was 439.802-448.244 ms in the first capture; its mixed-scenario
median falls from 445.0645 to 190.292 ms (about 57%). The separated new stages
show that packing is no longer the dominant upload cost. SPI and yields now
dominate; clocks, row transactions and yield policy were deliberately unchanged.
Boot-time contention includes a 599.831 ms upload with 249.895 ms in yields;
it is excluded from the steady interaction table, not silently discarded as an
interaction outlier.

Ready-to-snapshot remains below 0.5 ms, confirming no discretionary wait in
these ten samples. Every interaction still converts all 384,000 logical pixels
in 50 flushes. Ordinary return/dismiss render time is approximately 270-275 ms,
including about 215 ms of conversion and 53-56 ms of other LVGL render work.
Dialog opens additionally reload the 53,504-byte font source in 50 ms, build
its cache in 14 ms and reach the changed-UI marker about 90 ms after owner entry;
their frame refresh is approximately 302-304 ms.

Pad 15 remains a distinct slow path. Its first request takes 1,600.020 ms to a
coherent frame, including construction/application and a 649.413 ms refresh.
A subsequent cached visit (request 7/frame 80) still takes 1,026.636 ms to ready
and 1,849.500 ms to publication. It spends 277.621 ms in glyph misses inside a
664.392 ms render, plus approximately 355 ms from enqueue to screen switch.
The first visit records 265.916 ms in glyph misses. Caching the screen therefore
does not eliminate glyph-miss cost in this capture; the cause requires tracing,
not an assumption that all cached navigation is warm.

Next optimization candidates are the remaining full-frame conversion cost,
unnecessary redraws if confirmed, and pad 15's glyph/screen-application path.
Controller caching would save only about 22 ms here; it remains unstarted.
The approximately 611 ms waveform remains the largest fixed presentation
stage, unchanged and outside this software-only optimization.

Still unverified: visible onset, ghosting/orientation/unchanged-pixel quality,
physical screenshot consistency, action ordering and confirmation stress,
OTA/recovery stress, peak memory/stack and power, controlled idle/burst p95,
and SSD2677 hardware. No additional implementation scope is inferred from this
capture.

Validation: `git diff --check` and the untracked plan's trailing-whitespace
check passed. A second local Node.js calculation verified all reported
post-change stage medians/ranges. No firmware tests/build are needed for this
measurement-only plan update.

### Approved local-variable interaction follow-up

Status: Host verified; preferred-board firmware build passed; hardware pending.
Baseline: clean worktree at `fcf2b09a`. The existing counter capture has
`request_seq=0`: a changed `set_var` waits for passive binding polling, then
background settling/cadence. At the portal's 250 ms presentation minimum,
release-to-ready is approximately 21-100 ms, upload approximately 186-197 ms
and trigger-to-BUSY approximately 611 ms. These are pre-follow-up device
observations, not measurements of this implementation.

The approved correction reuses dispatch, continuation, binding polling and the
coherent publication path; it does not restart the architecture or begin
controller caching, waveform changes or phases 3-6.

* [x] Carry explicit user-interaction origin from pad/widget, swipe and hardware
  input; retain it across pending continuations without changing action order.
* [x] Queue binding invalidation only after a changed, successful user `set_var`.
  Background, unchanged and failed writes retain their previous behavior.
* [x] Coalesce requests and retain the first request timestamp. At the safe LVGL
  owner boundary, force existing current-pad binding resolution and request the
  existing prompt coherent render. Retain requests through sleep/OTA/navigation
  gates; consume them on non-pad screens without future urgency.
* [x] Preserve normal passive intervals, no-change behavior, LCD/dedicated-frame
  paths, buffer ownership, screenshot publication and hardware checkpoints.
* [x] Extend existing tests for changed/unchanged/failed/background mutations,
  origin restoration and continuation ownership, coalescing/deferred requests,
  prompt/no-change presentation, and forced polling with a 1000 ms interval
  without altering the following idle boundary. No new harness or test file.
* [x] Reconcile the architecture, owning user topic and board summary. No
  action schema/catalog metadata, API, setting or feature flag changed, so
  generated catalogs and flag references need no refresh.
* [x] Build only the saved verification preference,
  `reterminal-sticky-interactive`, with diagnostics enabled.
* [ ] Measure counter taps with passive 1000 ms / presentation 500 ms settings.
  Expect nonzero interaction request sequences and no discretionary settling/
  cadence wait when the panel is free. Record request-to-ready, eligible-to-
  snapshot, upload, waveform and publication separately.
* [ ] Complete physical visible-onset, burst/action-order/confirmation,
  sleep/OTA/recovery/screenshot and SSD2677 acceptance.

Validation commands and results:

```bash
cmake --build build/host-tests --parallel 2 --target test_runtime_variable_actions_display test_runtime_variable_actions_headless test_action_continuation test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics
```

Passed. The initial focused CTest selection passed 5/5.

```bash
cmake --build build/host-tests --parallel 2 --target test_alarm_manager test_gamepad_widgets test_timer_format test_timer_commands test_timer_binding_target test_toggle_widget test_slider_widget test_stopwatch_round_ui test_pad_confirmation test_pad_overlay_input
cmake --build build/host-tests --parallel 2 --target test_ota_activity test_binding_value test_runtime_variables test_runtime_variables_no_psram test_crowpanel_epaper_driver test_epaper_frame_battery test_epaper_frame_media_conformance
ctest --test-dir build/host-tests --output-on-failure -R '^(test_sticky_epaper_driver(_diagnostics)?|test_crowpanel_epaper_driver|test_ota_activity|test_binding_value|test_runtime_variables(_no_psram)?|check_runtime_variables_disabled|test_runtime_variable_actions_(display|headless)|test_action_(continuation|parse(_alarm_disabled)?|bindings)|test_pad_(confirmation|overlay_input|refresh_schedule)|test_epaper_(frame_battery|frame_media_conformance|frame_portal_contract|macro_contract)|test_asset_variant_matrix|test_portal_epaper_config|test_alarm_manager(_allocation_failure)?|test_timer_(format|commands.*|binding_target)|test_stopwatch_round_ui)$'
build/host-tests/tests/test_toggle_widget --gtest_brief=1
build/host-tests/tests/test_slider_widget --gtest_brief=1
bash tests/test_action_catalog_completeness.sh
```

Passed: compilation; 32/32 selected CTest checks; toggle 9/9; slider/option
selector 49/49; action completeness (30 built-in, 9 device-class types).
Initial dependent builds exposed missing test-only context/refresh stubs;
their signatures and lightweight bridges were updated, then builds passed.

```bash
build/host-tests/tests/test_gamepad_widgets --gtest_brief=1
build/host-tests/tests/test_gamepad_widgets --gtest_brief=1 --gtest_filter=-GamepadWidgetRegistry.SupportsFullFeaturedBoards
```

The full gamepad binary reports 68/69 passed: the existing
`GamepadWidgetRegistry.SupportsFullFeaturedBoards` assertion expects 16 widgets
but the registry has 17. That assertion and registry are unchanged; this file's
only change is its dispatch-stub signature. Left this unrelated mismatch
untouched. The explicit remaining selection passed 68/68.

```bash
./build.sh reterminal-sticky-interactive
```

Passed: sketch 3,187,398 bytes, globals 81,876 bytes; diagnostics enabled.
Firmware output: `build/reterminal-sticky-interactive/app.ino.bin`.
Compared with the preceding measured build: +404 sketch bytes, unchanged
reported global allocation. Build log:
`sticky-variable-refresh-build.log` in the session's persistent files.

```bash
git diff --check
```

Passed. The firmware log contains no compiler warnings/errors. Existing
documentation links were unchanged; no Markdown lint executable is installed.

No new physical latency improvement is claimed. The previously measured
approximately 22 ms preparation, 190 ms full upload and 611 ms waveform remain
the expected hardware-path bottlenecks, untouched by this correction.
No flash, commit, push or all-board build was performed for this follow-up.

### Counter capture after local-variable notification

Status: Device pipeline measured; user reports the counter now starts to feel
snappy. Physical visible-onset and broader hardware acceptance remain pending.
Source: user-provided capture on 2026-11-12, log times 8.511-58.719 seconds.
The capture does not restate controller identification or saved passive/
presentation settings; do not assume the proposed 1000/500 ms settings were
applied.

Requests 1-10 correlate with successful presentations of frames 69-78, each
with 5,694 converted pixels, one flush and a 96,000-byte upload. Request 11
renders frame 79 but has no completed presentation in this excerpt; exclude it
from completion statistics. Boot frames 66/68 are not counter samples.

| Stage | Samples | Median (ms) | Range (ms) |
|-------|---------|-------------|------------|
| Request to coherent ready | 10 | 9.437 | 8.910-11.620 |
| Release log to ready | 10 | 21.893 | 21.332-24.080 |
| Prior waveform wait (ready to eligible) | 10 | 10.819 | 0-671.931 |
| Eligible to snapshot | 10 | 25.539 | 0.667-37.033 |
| Preparation | 10 | 22.465 | 21.843-23.374 |
| Upload | 10 | 189.262 | 183.249-195.801 |
| Trigger to BUSY done | 10 | 610.978 | 610.404-611.989 |
| Request to successful publication | 10 | 879.660 | 833.524-1535.644 |
| Request to publication, panel already eligible | 5 | 835.441 | 833.524-875.582 |

Every completed counter frame now has a nonzero interaction sequence. Four
uncontended snapshots start within 0.667-0.718 ms of readiness. Small worker
turnaround remains during adjacent presentations: approximately 25-37 ms after
eligibility, not the former 150 ms settling or configured background cadence.
The cause of that turnaround is not established by these timestamps alone.
Request 6 waits 671.931 ms behind an earlier waveform, explaining its 1.536 s
request-to-publication time without attributing it to passive binding polling.

Release-log-to-ready is now 21-24 ms, versus the previous 250 ms cadence capture's
approximately 21-100 ms. These are small, differently paced samples, not a
controlled A/B latency distribution or physical input-to-visible measurement.
The successful change is explicit interaction classification and prompt binding
resolution; the unchanged preparation/upload/waveform path still takes about
0.83 seconds to complete when free.

The touch warning occurs during startup and reports a 468 ms maximum sampling
gap with zero errors; the same startup includes a 418.950 ms full-frame refresh.
This capture alone does not establish a steady-state touch problem or prove
the cause of that gap.

Validation: a local Node.js calculation of the ten correlated records produced
the table, separating prior-waveform wait from post-eligibility turnaround.
`git diff --check` passed. No implementation change, firmware build, flash,
commit or push accompanies this measurement-only update.

### Cached pad-navigation investigation, 2026-11-12

Status: Investigation complete within available evidence; implementation approval
pending. Baseline is clean `a4db178d`, with `fcf2b09a` immediately before it.
No new device capture is available. The earlier capture's request 7/frame 80
is the cached-navigation sample, not the later counter capture.

#### Font lifetime and working-set evidence

* [PadScreen](../../app/screens/pad_screen.cpp) retains its objects and fonts on
  ordinary hide/show. `update()` rebuilds only when tiles are absent or the pad
  configuration generation changes. `clearTiles()` destroys referencing objects
  before clearing the pad-owned font cache.
* [DisplayManager](../../app/display_manager.cpp) evicts the least-recent pad at
  `SCREEN_HISTORY_MAX` residency, not on every visit.
  [The render task](../../app/display_task.cpp) also evicts pad caches after a
  font-store generation change; only successful font upload/delete advances
  [that generation](../../app/font_store.cpp).
* [Font resolution](../../app/pad_layout.h) sends inherited typography to the
  device-lifetime cache, shared across screens at matching size/fallback/metrics/
  rasterizer. Explicit per-style fonts use their pad's cache. The device cache's
  32-instance limit is not a glyph limit. Cached font/source ownership does not
  imply cached glyph residency.
* [The monochrome rasterizer](../../app/freetype_mono_font.cpp) retains only
  **16 glyphs per font instance**, using reference-aware LRU replacement.
  Descriptor queries call the same rasterizing lookup as bitmap queries; a
  layout/width query can auto-hint, rasterize and expand an A8 bitmap. Kerning
  queries look up the next glyph too. LVGL repeats descriptor work across text
  measurement and partial-buffer rendering.
* The supplied capture logs no pad allocation/build, source load, instance
  creation, generation invalidation or font-cache teardown around the cached
  pad-15 visit. Its earlier pad-15 construction also creates no explicit font
  instance; inherited shared typography is consistent with that trace. Dialog
  loads/clears belong to a different cache owner and do not clear pad fonts.
  The rasterizer can silently recreate its FreeType face after allocation
  failure; the capture lacks failure counters, so that path is not ruled out.

A temporary opt-in probe extended the existing `test_pad_font_cache.cpp` target,
using the existing BebasNeue fixture and real host LVGL. It retained the same
font and screen, navigated to a blank screen and back twice, and compared the
complete returned RGB565 frame with its initial frame. Display: 480 x 800,
24 px font, 8,000-pixel partial draw buffer, matching Sticky's buffer geometry.
Each alphabet prefix used 16, 17 or 26 distinct glyphs at one size. These are
synthetic working sets, not an export of the user's pad.

| Distinct glyphs | Original 16-entry cache misses per cached render | Temporary 32-entry trial |
|-----------------|--------------------------------------------------|--------------------------|
| 16              | 0                                                | 0                        |
| 17              | 34                                               | 0                        |
| 26              | 52                                               | 0                        |

Both cached visits reproduced each result. Original-cache evictions equalled
misses; every probe had zero failed misses, zero source reloads, a retained font
pointer and identical cold/returned pixels. In the 26-glyph case, initial
show/layout incurred 130 misses with 16 entries versus 26 with 32 entries.
A standalone text measurement incurred 52 versus zero misses. Re-measuring and
setting the unchanged label followed by layout incurred 130 versus zero.
This confirms capacity thrashing without font invalidation, including work
outside render callbacks. Probe `micros()` is the test's synthetic clock:
**no probe duration is a real latency measurement**.

Host LVGL monitored use after the same operations increased from 64,336 to
67,360 bytes in the 26-glyph case (3,024 bytes), and from 64,016 to 65,424 bytes
for 16 glyphs (1,408 bytes). These are fixture-specific total allocator snapshots,
not ESP32 peak memory or a general per-font bitmap budget. Larger fonts and
many resident instances can cost substantially more.

#### Screen application, rendering and panel waiting are distinct

For the existing request 7/frame 80, rechecked against the original attachment:

* Request to coherent ready: 1,026.636 ms
* Enqueue log to switched log: approximately 355 ms; no pad rebuild is logged
* Refresh: 669.074 ms; render within it: 664.392 ms
* Flush within render: 220.245 ms, including 217.147 ms RGB565 conversion
* Glyph-miss handling within render: 277.621 ms
* Prior-waveform wait: zero; ready and eligible timestamps are identical
* Eligible to snapshot: 0.394 ms
* Preparation: 22.388 ms; upload: 183.716 ms; waveform: 611.973 ms
* Request to successful publication: 1,849.500 ms

The approximately 355 ms is application-side work before the switched marker,
not a 355 ms panel-policy sleep. Current diagnostics do not divide that interval
among `show()`, forced binding polling, layout and widget work. In particular,
`show()` clears binding text deduplication state even on a cached visit, then
resets the passive slot to force resolution. The subsequent poll measures and
sets bound labels even if their resolved value did not change. This is a
confirmed redundant path, but its contribution to this sample is unmeasured.
Do not attribute the entire interval to glyph misses or screen construction.
Render includes flush and glyph time; these figures are not additive stages.

#### Full-screen redraw finding

The manager already uses `LV_DISPLAY_RENDER_MODE_PARTIAL`. Sticky neither
forces full invalidation nor overrides LVGL configuration to select full mode.
`PadScreen::show()` calls `lv_screen_load()`. The installed LVGL immediate screen
load invalidates the newly active root; both original and 32-entry probes
therefore flush 384,000 pixels in 50 strips on every return.

Cached objects are not a saved screen bitmap. When switching between arbitrary
different pads, the existing framebuffer contains the other screen, so full
reconstruction is correct under this architecture. Skipping root invalidation
would leave stale pixels; a selective cross-screen diff or screen bitmap cache
would be a separate design, not a justified small fix. This does not mean every
local change needs a full redraw: the counter already demonstrates one small
flush. Leave conversion and framebuffer/controller ownership unchanged for this
follow-up.

#### Proposed smallest follow-up and approval boundary

The evidenced mechanism is a 16-glyph residency cliff amplified by eager
descriptor rasterization and repeated text passes, not loss of the cached pad.
The exact pad-15 per-font working set and any allocation failures remain unknown;
the host probe does not establish that 32 entries cover it or promise removal
of all 277.621 ms.

The user challenged 32 as insufficient for upper/lowercase Latin. Correct:
26 lowercase plus 26 uppercase already requires 52 distinct glyphs, before
digits, punctuation or accents, at each font instance/size. The 32-entry value
was the smallest comparative probe covering its synthetic 26-glyph text, not
an evidenced production working-set size. Do not select the shipping limit from
that probe. Measure the actual per-instance working set and memory first;
64 covers letters plus digits with little punctuation headroom, while 96 can
cover printable ASCII but not arbitrary Unicode or every shared-screen workload.
No implementation approval was received.

A bounded 32-entry monochrome-cache trial remains an experimental comparison,
not the recommended shipping capacity. Any approved capacity change should retain
the current LRU,
pin/release, allocation recovery and font lifetimes. Extend the existing font
test with warm text beyond 16 distinct glyphs and retain meaningful pinned
eviction/OOM coverage for the new limit. Update the two existing developer
font-cache descriptions. No new cache framework, new test harness, setting,
dependency, controller caching or waveform change is needed.

Before calling the device cause fully isolated, obtain the pad-15 configuration
and correlated glyph hit/miss/eviction/failure deltas around screen application
and rendering, keyed by font instance/size. Existing aggregate diagnostics can
show total failures; bounded diagnostics would be needed for per-font attribution.
Approval can instead select diagnostic-only evidence collection first.
Preserving label deduplication on cached `show()` is a separate small candidate;
do not bundle it into a cache-only A/B test. It must still resolve changed hidden
values promptly on return and preserve DOT-mode source metrics.

#### Exact checks, experiments and remaining gate

Before the probe, these existing commands passed (six CTest registrations):

```bash
cmake --build build/host-tests --parallel 2 --target test_pad_font_cache test_pad_font_cache_freetype test_pad_font_cache_freetype_diagnostics
ctest --test-dir build/host-tests --output-on-failure -R '^(test_pad_font_cache(_freetype(_diagnostics)?)?|test_freetype_disabled_.*)$'
```

The temporary probe command passed with the original 16-entry cache, the
32-entry trial, and again with the original cache plus the unchanged-label
measurement:

```bash
cmake --build build/host-tests --parallel 2 --target test_pad_font_cache_freetype_diagnostics
build/host-tests/tests/test_pad_font_cache_freetype_diagnostics --probe-navigation
```

The first probe compilation failed on two `printf` specifiers for `size_t`;
changing them to `%zu` fixed it. No production failure was hidden. The temporary
probe and capacity trial were removed after measurement; production and test
source return to baseline. The three-target build and six-check CTest selection
above passed again after restoration (6/6, 0.32 s). `git diff --check` passed;
`git diff --quiet -- src/app/freetype_mono_font.cpp tests/test_pad_font_cache.cpp`
returned zero, and `git status --short` showed only this plan modified. A local
Node.js calculation independently checked the reported frame-80 timestamp
differences and host memory deltas. No Markdown linter is installed.

Only this existing plan is intended to remain modified. No firmware build is
needed for investigation-only results; any approved firmware change must follow
the saved `reterminal-sticky-interactive` preference and its single-board build.
No flash, commit, push or all-board build is authorized. All prior physical
acceptance items remain pending, including SSD2677 hardware; preserve runtime
SSD1677 BUSY-fallback detection and both controller paths.

### Approved 96-glyph cache follow-up, 2026-11-12

Status: Host verified; preferred-board firmware build passed; hardware
pending. The user explicitly selected 96 entries after challenging the
synthetic 32-entry trial and requested font-memory estimates. This approves
the bounded cache-size follow-up, not a byte-budget framework, binding-dedup
change, new diagnostics phase or controller/policy experiment.

Changed [the monochrome adapter](../../app/freetype_mono_font.cpp) from 16 to
96 entries per font instance. Bitmap allocation remains on demand; no 96-glyph
pixel atlas is allocated up front. Reference-aware LRU, immutable pinned
bitmaps, allocation recovery, source/instance ownership and OTA guards remain
unchanged. LCD/built-in fonts and grayscale/STB are unchanged.

Extended the existing [font-cache test](../../../tests/test_pad_font_cache.cpp),
without a new harness, to discover 97 distinct drawable fixture glyphs, warm
96 and compare their pixels over two revisits with zero diagnostic misses or
evictions. The pin test now fills all 96 slots, rejects an additional glyph
without corruption, releases one slot and verifies successful replacement
and the remaining pinned pixels. The allocation-failure retry test now drives
97 distinct glyphs while a bitmap is pinned, retaining actual eviction pressure
instead of the former alphabet-only loop that would fit the larger cache.

#### Memory estimate and risk

A temporary `--font-memory` mode in that same test used the installed
`/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` and the production adapter,
loading printable ASCII U+0020 through U+007E (95 glyphs). Each font size used
a new instance; LVGL allocator state was checked to return to its pre-instance
baseline after destruction. Source input occupied ordinary host storage, not
the monitored LVGL heap. Host allocator snapshots include the retained FreeType
library/face, caches, bitmap buffers and allocator overhead, but not transient
peak workspaces, the font file or display allocations.

| Pixel size | Original 16-entry retained bytes | New 96-entry retained bytes | A8 payload for all 95 glyphs |
|------------|----------------------------------|-----------------------------|-----------------------------|
| 24         | 70,712                           | 97,400                      | 17,345                      |
| 32         | 72,664                           | 110,048                     | 29,837                      |
| 48         | 78,472                           | 149,200                     | 68,821                      |
| 64         | 86,616                           | 201,912                     | 121,492                     |
| 96         | 109,776                          | 353,112                     | 272,719                     |
| 128        | 142,608                          | 564,568                     | 484,161                     |

The original cache retains only its final 16 entries; the payload column
describes the complete alphabet, not simultaneous original-cache residency.
Empty-instance monitored allocation was 8,656 bytes before versus 15,696 after
on this 64-bit host, a 7,040-byte fixed increase. These sizes are not target ABI
measurements. The input file is 759,720 bytes, additional to the table and
shared only within one owning `PadFontCache` asset, not between explicit-font
pad owners. The user's Atkinson source was only 53,504 bytes in the earlier
capture; its glyph-memory shape has not been measured.

The dominant large-size term is `sum(aligned_A8_stride * glyph_height)`,
roughly proportional to pixel size squared for the same characters. B/W
coverage is expanded to one byte per pixel, not retained as packed 1-bit data.
Multiple sizes/metrics/backends create distinct instances; inherited typography
shares device-lifetime instances, while explicit pad-owned caches and hidden
resident pads can multiply memory use. Sparse numeric labels allocate fewer
bitmaps. The 96 limit is not a global byte budget, and PSRAM-first allocation
falls back to internal RAM. Replacement briefly retains the victim and new
bitmap together; FreeType has additional temporary workspaces. Large fonts,
many resident instances, fragmentation and OTA/background allocations can still
exhaust memory. No guarantee of graceful global OOM recovery is inferred from
the focused allocation-failure tests.

Documented the new limit and sizing caveats in
[Interactive E-Paper](../../../docs/dev/interactive-epaper.md) and
[display/touch architecture](../../../docs/dev/display-touch-architecture.md).
No setting, schema, action/binding metadata, feature flag or controller changed.

#### Validation and next device gate

The memory experiment used these commands at 96 entries, then temporarily at
the original 16 entries for comparison; both passed:

```bash
cmake --build build/host-tests --parallel 2 --target test_pad_font_cache_freetype_diagnostics
build/host-tests/tests/test_pad_font_cache_freetype_diagnostics --font-memory /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

The temporary memory mode was removed and the production limit restored to 96.
The first final-source selection passed 8/8 (4.22 s):

```bash
cmake --build build/host-tests --parallel 2 --target test_pad_font_cache test_pad_font_cache_freetype test_pad_font_cache_freetype_diagnostics test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics
ctest --test-dir build/host-tests --output-on-failure -R '^(test_pad_font_cache(_freetype(_diagnostics)?)?|test_freetype_disabled_.*|test_sticky_epaper_driver(_diagnostics)?)$'
git diff --check
```

After the final pinned-pixel and OOM eviction-pressure extensions, the same
build/CTest selection passed again: 8/8, 4.60 s. `git diff --check` passed.
The saved verification preference was read from
`~/.copilot/memories/board-preferences.md`; literal `/memories` is unavailable.
Only `./build.sh reterminal-sticky-interactive` was run and passed. Its output is
captured in the current session's `files/sticky-96-glyph-cache-build.log`.
Diagnostics are enabled; sketch size is 3,187,410 bytes (+12), globals 81,876
bytes (unchanged). These static sizes do not account for the larger heap-resident
cache. No compiler warnings or errors appear in the captured output. The
generated flag reference is unchanged. Firmware output:
`build/reterminal-sticky-interactive/app.ino.bin`.
No production source changed after that firmware build; only tests and prose
were finalized. The worktree contains exactly the adapter, existing font test,
two developer descriptions and this plan. No Markdown linter is installed.

Repeat the actual cached pad-15 scenario on device and collect coherent-ready,
glyph, conversion and application timings along with PSRAM/internal heap
headroom. No new device latency or quality improvement is yet claimed. The
exact pad working set, visible onset, physical action-order/confirmation stress,
OTA/recovery/screenshot stress and SSD2677 hardware remain pending. Preserve
runtime controller detection. No flash, commit, push or all-board build.

### Device capture after the 96-glyph change, 2026-11-12

Status: Device stage improvement measured; physical acceptance remains pending.
User-supplied log identifies ELF prefix `fa9b56d2d`, ESP32-S3 at 240 MHz,
runtime SSD1677 selection through ID `0xff`/BUSY-transition fallback, B/W Bayer,
rotation 1 (480 x 800 logical), passive binding 1000 ms, presentation minimum
500 ms and full threshold 200. Always-on, screen sleep disabled, Wi-Fi active,
MQTT/HA disabled. Temperature, supply and screenshot polling are unrecorded.
These settings are confirmed in this capture, unlike the preceding counter
excerpt. No new firmware source, build or flash accompanies this analysis.

All 16 completed interaction requests (1-16) correlate with successful
presentations (`ok=1`) and 96,000-byte uploads. Their render records all show
`glyph=0`. This is zero measured miss time **inside rendering**, not proof of
zero font work during screen construction/application or zero misses across
the whole session. The cache's per-font working set and failure counters are
still unavailable.

#### Cached pad-15 comparison

New request 10/frame 84 is the cached visit; request 8/frame 81 is first pad-15
construction. Compare the cached visit with the previous 16-entry request
7/frame 80, not with a counter or a first visit:

| Cached visit stage | Previous 16-entry capture (ms) | New 96-entry capture (ms) |
|--------------------|--------------------------------|--------------------------|
| Request to coherent ready | 1,026.636 | 444.168 |
| Enqueue log to switched log | approximately 355 | approximately 67 |
| Refresh | 669.074 | 375.826 |
| Render within refresh | 664.392 | 374.227 |
| Glyph misses within render | 277.621 | 0 |
| RGB565 conversion within flush/render | 217.147 | 216.176 |
| Prior-waveform wait, ready to eligible | 0 | 0 |
| Eligible to snapshot | 0.394 | 0.397 |
| Preparation | 22.388 | 22.793 |
| Upload | 183.716 | 191.824 |
| Waveform, trigger to BUSY done | 611.973 | 610.346 |
| Request to successful publication | 1,849.500 | 1,275.345 |

This cached sample reaches a coherent frame 582.468 ms sooner (56.7%) and
completed publication 574.155 ms sooner (31.0%). Render improves 290.165 ms,
with the formerly measured 277.621 ms glyph-miss cost absent. Conversion is
essentially unchanged; both still convert all 384,000 pixels in 50 flushes.
Application-side time also falls, but the approximately 67/355 ms intervals
do not isolate layout, binding reapplication and other work. Do not assign
their entire difference to one operation. Render, flush/conversion and glyph
measurements overlap; do not sum them as additional independent stages.

The new first visit takes 649.429 ms request-to-ready and 1,481.863 ms to
publication, with 376.251 ms refresh and no measured render glyph misses.
Construction still occurs and can warm glyphs before rendering. Subsequent
navigation to pad 0 takes 319.343 and 315.047 ms to ready. The current pad 0
has four buttons and a preloaded icon, whereas the older navigation capture
had two buttons and no cached icon; these are not controlled matched A/B
distributions. Pad 15 still logs four buttons and the same gauge geometry,
but its complete configuration is not available for equality checking.
There is only one matched-kind cached pad-15 sample per capture, not the
required 30-sample median/p95/max qualification.

#### Counter and remaining hardware path

Eight counter requests (1-5, 14-16) still convert 5,694 pixels in one flush:

* Request-to-ready median 9.9405 ms, range 7.208-31.345 ms
* Request-to-publication median 861.0555 ms, range 834.802-1,183.562 ms
* Five requests with the panel already eligible: publication median 856.619 ms,
  range 834.802-863.679 ms
* Three overlapping requests wait 314.499, 66.232 and 141.226 ms behind an
  earlier waveform, then about 25.5-26.1 ms from eligibility to snapshot

No discretionary 150 ms settling or configured 500 ms cadence appears on
these interactive snapshots. Across all 16 interactions, preparation median
is 22.7345 ms (22.048-25.237), upload 192.312 ms (185.787-199.297), and waveform
611.3955 ms (610.346-612.000). The cached pad's post-ready physical path is
831.177 ms. It remains largely unchanged; neither the cache fix nor BUSY
completion establishes physical visible onset.

#### Memory evidence and pending acceptance

At 36.831 seconds, after cached navigation and before creating another dialog
font instance, the log reports 6,276,060 bytes free PSRAM (about 5.99 MiB),
largest PSRAM block 6,160,372 bytes (about 5.88 MiB), and 92,760 bytes free
internal RAM (about 90.6 KiB), largest internal block 49,140 bytes (about
48.0 KiB). This snapshot has substantial PSRAM headroom for the captured
19/24/32 px typography, not proof that large-font stress cannot exhaust memory.

Do not interpret the free-memory difference from the earlier dialog snapshot
as a font leak: intervening pad construction, retained screen objects and glyph
warming also allocate. No post-navigation stack watermark, steady-state memory
plateau or repeated large-font/eviction/OTA stress is captured. Touch sampling
warnings include long render/startup gaps with zero reported sampling errors;
the cache change has not eliminated all blocking UI work.

Validation: a local Node.js parser read the supplied session message, joined
`Display`/`Sticky` numeric records by frame ID and selected nonzero request
sequences with successful publication. It verified all counts, per-request
timestamp differences, medians/ranges and cached before/after percentages.
`git diff --check` passed before and after the measurement-only update.
No host tests or firmware build are needed for this prose
update. No dependency, flash, commit or push was performed.

Next gate: retain the 96-entry change as measured software improvement and
collect controlled cached navigation and memory/stress acceptance. Continue
to defer label-dedup changes, conversion redesign, controller caching,
waveform/clock changes and phases 3/4/6 unless separately approved.

### Commit checkpoint and deferred memory work, 2026-11-12

User authorized committing the 96-entry change. Reviewed the aggregate five-file
change set, including tests and the two developer documentation owners.
No new user control, schema, action/binding metadata or board capability requires
a user-guide or generated-reference update. The focused CTest command recorded
above passed again: 8/8, 4.61 s. The earlier warning-free Sticky firmware build
still covers the unchanged production source. No Markdown linter or existing
focused documentation check was found; documentation was reviewed directly.

Memory-aware font/glyph management is deferred to
[issue #151](https://github.com/jantielens/esp32-macropad/issues/151).
Current PSRAM headroom is sufficient for the captured configuration; arbitrary
large-font safety is not established. The issue requests measurement and a
smallest-fit policy proposal, not an immediate cache architecture rewrite.
No new optimization phase, flash, push or all-board build is authorized.

### Full-frame conversion investigation, 2026-11-12

Status: Investigation complete; implementation awaits new approval. The worktree
was clean at `46f1b16c`, after `fcf2b09a` and `a4db178d`. This outcome changes
only the existing plan. No production source, test, firmware or device changed.

#### Baseline and timing attribution

Re-read the latest capture from the preceding local session, turn 4, and joined
the numeric records for request 10/frame 84. The existing firmware ELF has
SHA256 `fa9b56d2d15e94766c489b187d948ccd6321ae214fcd0f3b05521c722a24af34`,
matching the capture's prefix. Its compile database selects `-Os` and the S3
`qio_opi` SDK profile. SSD1677 remains selected through ID `0xff` and BUSY
fallback, not positive chip identification.

| Frame 84 measurement                         | Time (ms) |
|----------------------------------------------|-----------|
| Request to coherent ready                    | 444.168   |
| Refresh, containing render                   | 375.826   |
| Render, containing flush                     | 374.227   |
| Flush callbacks, containing conversion/wait   | 219.422   |
| Conversion inside framebuffer mutex          | 216.176   |
| Framebuffer mutex acquisition                | 0.257     |
| Flush less conversion                        | 3.246     |
| Flush less conversion and mutex acquisition   | 2.989     |

Conversion accounts for about 57.8% of render. It processes 384,000 pixels in
50 flushes, about 0.563 us/pixel. At 240 MHz that is 135.11 elapsed-cycle
equivalents/pixel, not measured exclusive CPU cycles. Interrupts, preemption
and memory stalls remain included.

The callback remainder averages about 59.8 us/flush after subtracting the
separately measured mutex wait. It includes callback/setup/bookkeeping and
other elapsed work; it is not an exclusive measurement of setup. Timer/counter
updates inside the conversion boundary are not separately timed. These records
rule out external callback overhead or mutex contention as the main explanation
for this sample, but do not alone partition the conversion loop.

Other recent full-frame records are approximately 213-216 ms, with a 231.021 ms
dialog sample and a 356.164 ms startup sample. They are different scenarios,
not a controlled distribution. The existing one-flush 5,694-pixel records take
about 3.05 ms, approximately 0.535 us/pixel. This is supporting evidence for
pixel-proportional work, not authorization to optimize the counter or a fitted
per-flush cost model.

#### Call path, geometry and ownership

The LVGL owner runs the partial renderer and synchronously calls
`DisplayManager::flushCallback()` in `display_task.cpp`. It supplies the actual
RGB565 byte stride, sets the logical window, and calls Sticky's `pushColors()`
with byte swapping disabled for this buffered driver. Flush completion occurs
after conversion, so the single LVGL source buffer cannot be reused midway.

The capture confirms a 16,000-byte, single PSRAM draw buffer. At logical
480 x 800, full-screen rendering uses 50 strips of 16 rows (7,680 pixels,
15,360 active source bytes), not 800-pixel-wide ten-row strips. Full redraw on
pad changes is necessary under the existing root-screen model.

`pushColors()` already clips once, transforms opposite corners once, and walks
packed native bytes. For rotation 1, each native row has constant logical X;
successive native pixels read logical Y in reverse, with source step `-480`
RGB565 elements for these full-width strips. Each flush visits two adjacent
destination bytes per native row, across 480 native rows. Across the frame this
is 384,000 RGB565 loads and 48,000 drawing-byte reads, with stores only when
packed bytes change. Do not revert to per-pixel destination read/modify/write.

The drawing mutex covers conversion and pending/change metadata. `beginFrame()`
and `commitFrame()` bracket the coherent render generation independently of
individual flushes. The panel worker cannot snapshot a rendering generation;
after commitment it copies drawing into immutable sending under the mutex,
then releases it before panel I/O. Only successful presentation updates the
completed buffer. Screenshot copies read that completed buffer. None of these
ownership or publication boundaries needs to change.

#### Evidenced avoidable work and unresolved cost

Disassembled the existing captured S3 ELF, rather than extrapolating from host
timings. In `StickyEpaperDriver::pushColors()`:

* Address `0x42022e9f` executes signed `quos` for native byte indexing once per
  packed byte, about 48,000 times for this frame.
* Address `0x42022f87` executes signed `rems` for the intra-byte bit position
  once per pixel, 384,000 times. Its divisor is runtime `pixelsPerByte`, although
  the only values are 8 for B/W and 2 for Gray4.
* The loop retains runtime byte-swap, grayscale and dither-mode decisions,
  variable shifts/masks, and stack spills. Existing RGB565 luminance is already
  three channel-table loads plus additions/shift; there is no remaining
  per-channel division to remove.
* Bayer uses logical `x & 3`, `y & 3`, a threshold-table read and the existing
  `>=` comparison. Rotation is already reduced to source increments within the
  pixel loop. Dither phase must not be changed to native coordinates.

Thus generic packed-position arithmetic demonstrably leaves avoidable divider
instructions in the hot loop. This is a confirmed inefficiency, not proof that
division explains all or most of 216 ms. Exclusive arithmetic, cache-miss and
preemption costs are not available in the captures.

Both source and destination are PSRAM allocations. The selected SDK has a
32 KiB, eight-way data cache with 32-byte lines and 80 MHz PSRAM. Rotated source
gathers are strided; destination writes are packed but span native rows.
Cache locality/conflicts and scheduling may contribute, but there are no miss
counters or controlled internal-RAM/PSRAM comparisons. Do not describe the
216 ms as raw PSRAM bandwidth or introduce a transpose buffer on this evidence.

There are no deliberate yields, panel operations, or OTA polls inside the
conversion loop. The every-16-row `vTaskDelay(1)` belongs to later panel upload;
the screenshot copy has its own separate yields. They do not explain this
conversion sample. Existing OTA safe points are the owner loop/prompt-render
guard, presentation entry/policy waits, and panel row/BUSY checkpoints. Do not
add waits under the conversion mutex, remove transfer checkpoints, or silently
drop a flush when OTA begins midway. The proposed arithmetic-only change keeps
these existing safety boundaries unchanged.

#### Smallest proposed fix and realistic gain boundary

Request approval for packed-position arithmetic only in the board-local
`sticky_epaper_driver.cpp`:

* Select a log2 pixel-group size once per flush (3 for B/W, 1 for Gray4).
* Replace native-X division by right shift and remainder by a group-size mask.
  Use the same power-of-two identity for the next byte boundary.
* Retain the current traversal, scalar luminance/dither helper, clipping,
  source stride, Gray4 quantization, byte merge/change detection and locks.

Clipped native X is nonnegative and below 800, so these identities preserve
signed arithmetic results. A temporary read-only Node.js calculation checked
all 640,800 `(mode, nativeX, nativeRight)` combinations with
`0 <= nativeX < nativeRight <= 800`: old/new byte indices, clipped byte ends
and bit shifts matched exactly. This is an arithmetic sanity check, not a
replacement for production output-equivalence tests or physical acceptance.

The fix removes approximately 432,000 dynamic divide/remainder executions per
full B/W frame without allocating memory or skipping pixels. No defensible
ESP32 speedup percentage follows from instruction counts alone. As sensitivity,
each net cycle saved per pixel is 1.6 ms/frame at 240 MHz; ten is 16 ms.
An incremental tens-of-ms gain is a reasonable experiment objective, not a
measured forecast. Saving 25% of conversion would save 54.044 ms, only about
4.24% of the current 1,275.345 ms request-to-publication sample. Even eliminating
conversion entirely could save no more than 216.176 ms (16.95%) in this sample.
The approximately 831 ms post-ready path is unaffected.

Affected implementation owners would be the existing Sticky driver, this plan,
and the existing conversion description in the display/touch developer guide
if needed. Reuse the current Sticky scalar-reference screenshot/wire tests for
both controllers, all rotations, all conversion modes, swapped/padded input,
clipped edges and Gray4. Extend that same test only if aligned strip output
needs coverage; do not add another harness. Build the preferred Sticky board
after an approved native change and inspect its new disassembly to confirm
the divider instructions are gone.

Do not bundle mode-specialized kernels, precomputed color/pattern caches,
internal-RAM staging, yield-policy changes or label-dedup work into this fix.
If the arithmetic-only result is immaterial, stop and obtain separately approved
device CPU/cache attribution rather than assuming a broader rewrite is needed.

#### Exact investigation checks and remaining gate

The existing focused targets rebuilt successfully:

```bash
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics \
  test_display_perf test_display_perf_diagnostics
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_sticky_epaper_driver(_diagnostics)?|test_display_perf(_diagnostics)?)$'
```

Result: 4/4 passed, 3.92 s after rebuild. The same selection also passed before
rebuild (3.93 s). Existing benchmark command:

```bash
build/host-tests/tests/test_sticky_epaper_driver --benchmark
```

Result: passed. Rotation-0 conversion median/p95/max was
4.775/6.327/7.064 ms; rotation 1 was 4.996/5.124/5.136 ms, 30 samples each.
This unchanged host benchmark uses threshold mode, ten-row strips and mocked
platform calls. It does not measure captured Bayer/16-row ESP32 performance.
Both mock controller/Gray4 benchmark paths also completed; no new speedup
is claimed.

Existing firmware inspection command, using the installed S3 toolchain:

```bash
~/.arduino15/packages/esp32/tools/esp-x32/2601/bin/xtensa-esp32s3-elf-objdump \
  -d --disassemble=_ZN18StickyEpaperDriver10pushColorsEPtmb \
  build/reterminal-sticky-interactive/app.ino.elf
sha256sum build/reterminal-sticky-interactive/app.ino.elf
```

Result: matching captured ELF prefix and the divider instructions described
above. Explicit start/stop-address reads resolved disassembler alignment at
the source-load and Bayer branch targets; misleading linear decoding there
was not used as evidence. Node.js arithmetic/timing calculations passed.

No firmware build is warranted for this investigation-only plan update.
No new dependency, flash, commit, push or all-board build occurred.
Device cycle/cache attribution, controlled 30-sample navigation distributions,
memory/stack peaks, physical visible onset/quality, confirmation/action-order,
OTA/recovery/screenshot stress and separate SSD2677 hardware acceptance remain
unverified. Approval for this arithmetic-only experiment is the next gate.

### Approved packed-position arithmetic follow-up, 2026-11-12

Status: Host verified; hardware performance and acceptance pending.
After the investigation, the user explicitly selected
"Approve arithmetic-only implementation and focused validation."
This authorizes only the narrow experiment above, not any other optimization
candidate or phase.

Changed `sticky_epaper_driver.cpp` to select the pixel-group shift once per
flush, reuse the shifted native byte index for addressing/boundaries, and mask
the intra-byte position. The nonnegative clipped native-X invariant makes these
identities exact for both B/W and Gray4. No pixel work was skipped; buffer
allocation, stride, rotation, dither/luminance, edge merging, locks, diagnostics,
frame publication, controller detection, transfers and OTA checkpoints are
unchanged. No new helper, buffer, test file or permanent test was needed.

Existing scalar-reference coverage exercises complete screenshots and controller
wire payloads across every RGB565 value, all rotations, conversion modes,
both controllers, Gray4, padded/swapped sources and clipped byte/nibble edges.
It is sufficient for this arithmetic-only change. Existing failure, OTA,
coherent-frame and screenshot scenarios also remain passing. No claim is made
that these host checks establish physical behavior.

#### Validation and generated S3 code

Exact host build and CTest commands:

```bash
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics \
  test_display_perf test_display_perf_diagnostics
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_sticky_epaper_driver(_diagnostics)?|test_display_perf(_diagnostics)?)$'
build/host-tests/tests/test_sticky_epaper_driver --benchmark
```

Result: build passed; 4/4 CTest checks passed in 3.98 s; benchmark completed.
Conversion median/p95/max for 30 host samples:

| Rotation | Before (ms)           | After (ms)            |
|----------|-----------------------|-----------------------|
| 0        | 4.775 / 6.327 / 7.064 | 4.930 / 5.153 / 5.213 |
| 1        | 4.996 / 5.124 / 5.136 | 5.268 / 5.355 / 5.472 |

These runs do not demonstrate a host speedup; both post-change medians are
slightly higher. The after run overlapped the firmware build, and the host
benchmark is unoptimized, threshold-only, ten-row/mock-platform code rather
than the captured S3 Bayer/16-row workload. Do not extrapolate a device
regression or gain from this uncontrolled comparison.

Firmware verification followed the existing preference at
`~/.copilot/memories/board-preferences.md`; literal
`/memories/board-preferences.md` remains absent. Exact build:

```bash
./build.sh reterminal-sticky-interactive
```

Passed: sketch 3,187,398 bytes (12 bytes below the preceding build), globals
81,876 bytes (unchanged). Build-log scan found zero compiler warning/error
lines; generated compile-time flag report was unchanged. Output is retained
in the session's `files/sticky-conversion-arithmetic-build.log`.
New ELF SHA256:
`db818d5771b1956b09402263eb544cb2e06650ce68d4d8d23c58bb639e613bcb`.

Repeated the investigation's exact `objdump` and `sha256sum` commands against
the new ELF. The conversion symbol is present with zero `quos`, `quou`, `rems`
or `remu` instructions. Address `0x42022e9d` now uses `sra` for byte indexing;
`0x42022f82` uses `and` for the intra-byte position. This verifies the actual
S3 code-generation objective, not merely a source spelling change.

`git diff --check` passed during implementation. Only the board-local driver
and this existing plan are changed. The developer guides' packed-byte
conversion, logical dithering, ownership and safety descriptions remain
accurate; no external behavior or user/API/metadata contract changed, so no
narrative guide or generated catalog update is needed.

#### Remaining device gate

The compiled inefficiency is removed, but no new S3 timing capture is available.
The approximately 216 ms conversion baseline must not be reported as improved
until measured. Collect at least 30 comparable cached-navigation samples on
the existing configuration, separating conversion, framebuffer wait, other
render work and post-ready stages; record firmware hash, controller-selection
method, settings, temperature, supply and background/screenshot load.

Retain the prior physical onset, image-quality/ghosting, memory/stack,
confirmation/action-order, OTA/recovery/screenshot and separate SSD2677 hardware
gates. No flash, commit, push or all-board build occurred. Label deduplication,
mode-specialized kernels, memory staging, controller caching and phases 3/4/6
remain unapproved. If the device gain is immaterial, stop at this checkpoint
and request a new evidence/implementation decision.

### Device capture after packed-position arithmetic, 2026-11-12

Status: No material device speedup demonstrated; physical acceptance pending.
The new 514-line user capture identifies ELF prefix `db818d577`, matching the
arithmetic-only build. It confirms S3/240 MHz, B/W Bayer, rotation 1,
480 x 800 logical, passive binding 1000 ms, presentation minimum 500 ms,
full threshold 200, always-on and screen sleep disabled. Runtime controller
selection remains ID `0xff` followed by SSD1677 BUSY-transition fallback.
Temperature, supply and screenshot polling remain unrecorded.

A read-only local Node.js parser joined Sticky conversion/presentation and
Display render records by frame ID, then selected nonzero request sequences
with completed publication. All 13 interaction presentations succeeded,
transferred 96,000 bytes and reported zero measured render glyph-miss time.
Nine interaction frames converted all 384,000 pixels in 50 flushes; their
conversion median is 214.904 ms, range 211.075-221.600 ms. This mixed set
contains dialogs, first navigation and cached navigation, not a controlled
cached-navigation distribution.

The closest same-input/scenario comparison is the cached touch visit to pad 15:
previous request 10/frame 84 versus new request 9/frame 82.

| Cached touch pad-15 stage | Before arithmetic (ms) | After arithmetic (ms) |
|---------------------------|------------------------|-----------------------|
| Conversion                | 216.176                | 216.872               |
| Refresh                   | 375.826                | 404.892               |
| Render within refresh     | 374.227                | 404.442               |
| Request to coherent ready | 444.168                | 425.978               |
| Request to publication    | 1,275.345              | 1,265.699             |

Conversion is 0.696 ms slower (0.32%), effectively unchanged on this evidence.
Readiness is 18.190 ms earlier (4.10%) and publication 9.646 ms earlier (0.76%),
but these differences cannot be attributed to the arithmetic change.
Render is 30.215 ms longer; other application-side elapsed work is shorter.
These nested stages must not be added as independent costs.

The configuration is not controlled: pad 15 now builds five buttons, including
a bar chart, rather than the previous four. Home Assistant live connects one
entity and hydrates history, whereas the preceding capture had HA disabled.
Full pad configurations are unavailable. Additional background updates convert
47,216 pixels. Do not call this a proven regression or a successful speedup.

Two later hardware-button cached pad-15 visits take 490.153 and 499.269 ms to
ready, with 216.692 and 221.600 ms conversion. Across these and the cached touch
visit, conversion median is 216.872 ms. Mixing input origins does not produce
the required comparable 30-sample acceptance distribution.

Across all 13 completed interactions, preparation median is 22.603 ms, upload
190.778 ms, and trigger-to-BUSY-completion 611.026 ms. Post-ready publication
median is 831.905 ms. The post-ready path is therefore essentially unchanged.
Only request 3 waits behind an earlier waveform (1.186 ms); the cached pad-15
visits have no prior-waveform wait. Touch sampling gaps still occur during
blocking render work; zero reported sampling errors does not qualify input
responsiveness or physical display behavior.

Conclusion: output/code-generation verification established that the dynamic
divider instructions were removed, but this capture does not establish a
meaningful reduction of the approximately 216 ms conversion cost. The removed
arithmetic is not demonstrated to dominate elapsed conversion. Stop the
optimization experiment here rather than expanding it into specialized kernels
or a memory-layout rewrite. Further work needs a separately approved, bounded
CPU-cycle/cache/scheduling attribution investigation; label application/layout
remains a separate candidate, not an established explanation for all latency.

Validation: the Node.js frame join verified counts, per-request timestamp
differences, stage medians/ranges and the before/after deltas above.
`git diff --check` passed before this measurement-only update. No production
source, tests or firmware changed after the passing checks/build above; no
additional host run or firmware build is warranted for this prose update.
No flash, commit or push was performed by the agent. Physical visible onset,
image quality/ghosting, memory/stack and OTA/recovery/confirmation/screenshot
stress, controlled distributions and SSD2677 hardware acceptance remain pending.

### Arithmetic experiment reverted, 2026-11-12

Status: Reverted by explicit user decision. The measured result did not justify
keeping lower-level indexing complexity without demonstrated responsiveness
benefit. Restored only the uncommitted shift/mask changes in
`sticky_epaper_driver.cpp`; the driver now exactly matches `HEAD` (`46f1b16c`).
The earlier frame prioritization, conversion/upload optimizations, prompt local
binding refresh and 96-glyph cache remain intact. Preserve the experiment and
capture entries above as evidence, not as the current implementation.

Verification:

```bash
git diff --exit-code -- src/boards/reterminal-sticky-interactive/drivers/display/sticky_epaper_driver.cpp
cmake --build build/host-tests --parallel 2 --target \
  test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics \
  test_display_perf test_display_perf_diagnostics
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_sticky_epaper_driver(_diagnostics)?|test_display_perf(_diagnostics)?)$'
./build.sh reterminal-sticky-interactive
sha256sum build/reterminal-sticky-interactive/app.ino.elf
git diff --check
```

Results: driver comparison passed with no differences; host targets rebuilt
and 4/4 checks passed in 4.13 s. The preferred single-board firmware build
passed with zero compiler warning/error lines, sketch 3,187,410 bytes and
81,876 bytes of globals. Generated flag report unchanged. Build output is in
the session's `files/sticky-conversion-revert-build.log`.

The regenerated ELF SHA256 is
`fa9b56d2d15e94766c489b187d948ccd6321ae214fcd0f3b05521c722a24af34`,
exactly matching the pre-experiment firmware. Local build artifacts therefore
no longer contain the experiment. No device flash was performed: the latest
supplied device capture still identifies experimental `db818d577`, so source/
artifact restoration must not be mistaken for changing the running device.
Only this existing plan remains modified. No commit or push occurred.

Suggested next work, not implementation approval: a bounded diagnostics-only
conversion attribution investigation, preceded by a fixed pad configuration
and unchanged HA/background load. Collect at least 30 comparable cached touch
visits and separate elapsed conversion, task execution/preemption and
memory/cache-stall evidence where the hardware instrumentation permits.
Check pixel scaling and flush geometry without changing output, storage,
ownership or yield policy. Existing wall-clock timings cannot isolate those
costs; do not propose another arithmetic or traversal rewrite from instruction
counts alone.

A separate subsequent candidate is screen application/layout and remaining
LVGL rendering. The latest cached touch visit spends about 183.729 ms in
render outside flush callbacks (`404.442 - 220.713`), in addition to conversion.
That is not all label remeasurement; label deduplication remains an unapproved
hypothesis requiring its own timing. Do not bundle this candidate with
conversion diagnostics or relax any physical/OTA/controller acceptance gate.

### Portal presentation-knob assessment, 2026-11-12

Status: Read-only behavior/default assessment; no settings, portal or firmware
changes approved or implemented. The user reports using full-refresh threshold
100 without visible ghosting. This is new qualitative evidence for their
working unit; earlier numeric captures used 200. Do not relabel those captures
as threshold-100 tests or infer SSD2677/other-panel qualification.

Traced `epaper-refresh.fragment.html`, `portal_epaper_refresh.js`, configuration
defaults/validation/persistence/API, `PadScreen::pollLiveData()`, Sticky's
`EpaperSchedule` and other interactive driver presentation paths.

* Passive binding interval remains applicable to idle data. Sticky's default
  is 1000 ms, with a permitted 100-60000 ms range. Successful changed
  user-origin local-variable actions and initial screen binding resolution use
  the prompt path instead; ordinary background HA/MQTT data still uses passive
  resolution and background presentation scheduling.
* Minimum presentation interval remains applicable to background presentations.
  Sticky's default is 500 ms; both HTML and backend enforce 250-60000 ms.
  Its elapsed interval starts after successful refresh completion, not at
  waveform start. Under continuously pending background work, an approximately
  831 ms physical path plus a 500 ms cooldown can yield about 1331 ms between
  completed updates. Interaction-priority frames bypass that discretionary
  interval and the 150 ms settling delay, but not active hardware work or
  required full refreshes. Other interactive drivers have not adopted this
  priority path, so their interval still gates user-driven changes too.
* Full-refresh threshold remains applicable to successful B/W partial-waveform
  presentations, including interactive ones. Sticky inherits default 10;
  its board does not override the shared default. The next changed presentation
  after the configured partial count uses a full waveform. Counts are physical
  successful presentations, not flushes, actions, pixels or seconds. Zero
  disables count-triggered cleanup only; startup/manual/recovery full refreshes
  remain. Sticky grayscale always uses full waveforms, making this counter
  irrelevant in that mode.
* Rendering mode, B/W conversion and threshold remain applicable and boot-only.
  The threshold level affects only threshold conversion, not Bayer/blue noise.
  Sticky defaults to B/W and threshold 128; dithering is an image-quality choice,
  not a new scheduling requirement. Minute-boundary refresh remains useful and
  defaults on, but requests binding evaluation rather than guaranteeing a
  physical update at the exact minute.
* VCOM is stored/exposed generically but is not consumed by Sticky's driver.
  Its zero selection should remain unchanged on this board. It remains relevant
  for consuming IT8951 drivers; do not remove it from their portal path.

Recommendations, not implementation authorization:

1. Keep the 250 ms permitted minimum for now. Reducing it cannot improve Sticky
   navigation/dialog/local-variable latency because those already bypass it.
   Try the existing 250 ms setting only if passive-data freshness is a problem,
   before expanding the shared range. The lower bound is software policy,
   not evidence of a universal controller safety requirement.
2. Keep Sticky's 1000 ms passive interval, 500 ms background interval, B/W,
   threshold 128 and minute-boundary default. No evidence requires more frequent
   background work or a different universal visual conversion default.
3. Threshold 100 is a reasonable user-selected setting for the reported working
   unit. Shared default 10 is conservative and may cause unnecessarily frequent
   flashing there; 100 is a candidate Sticky-specific default after a targeted
   contrasting-content/cleanup check and an explicit decision about the
   unqualified SSD2677 path. Do not increase every board's default from this
   report or introduce idle-cleanup/phase-3 policy changes. A default change
   must preserve explicitly saved settings.
4. Clarify the portal's interval label as background-only on priority-capable
   devices; retain the caveat for other drivers. Replace "regional updates" in
   full-refresh help with "partial-waveform presentations": Sticky transfers
   whole-panel frames even for partial waveforms. Explain that Sticky grayscale
   ignores the threshold and that VCOM is unused on Sticky, without implying
   that grayscale/VCOM are obsolete on other boards.

Validation command:

```bash
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_portal_epaper_config|test_sticky_epaper_driver(_diagnostics)?)$'
```

Result: 3/3 passed in 4.01 s. These existing checks establish the inspected
portal/presentation baseline, not suitability of new limits/defaults or physical
ghosting acceptance. No implementation or firmware build was needed.

### Approved generic portal wording follow-up, 2026-11-12

Status: Implemented and focused checks passed. User approved wording/notes only
and explicitly required the fragment to remain generic across e-paper devices.
No defaults, limits, saved settings, API fields, driver behavior or rollout
policy changed.

Updated `src/app/web/epaper-refresh.fragment.html` and the matching explanation
in `docs/user/specialized/interactive-epaper.md`:

* Explain passive binding checks and supported prompt local-variable updates.
* Keep the generic minimum-presentation label: notes explain that the interval
  follows presentation completion, priority-capable devices can bypass it for
  interactions, and other devices still apply it to interactive updates.
* Describe the full-refresh threshold as successful partial updates, which can
  mean regional updates or panel-wide partial waveforms depending on the driver.
  Do not call all of them partial-waveform presentations: IT8951 regional
  grayscale updates use GC16 and also contribute to its cleanup count.
* Explain that zero disables count-based cleanup, not startup/recovery/manual
  full refreshes, and that always-full modes ignore the threshold.
* Describe minute-boundary evaluation without promising immediate on-glass
  feedback.

Removed the board-specific grayscale example from the shared fragment. Existing
capability-based VCOM and rendering/conversion notes remain unchanged; no
Sticky-specific text was added. The generic user guide stays consistent without
adding board names or duplicating hardware references. No new test is justified
for copy-only changes; existing portal/feature contracts cover the retained
structure, with production asset generation validating the normal build path.

Exact checks:

```bash
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_portal_epaper_config|test_epaper_macro_contract)$'
bash tools/minify-web-assets.sh esp32-macropad 'ESP32 Macropad'
git diff --check
```

Results: 2/2 focused checks passed (0.13 s); production asset generation passed,
including its existing editor/JavaScript checks; whitespace check passed.
Asset-generation output is retained in the session's
`files/epaper-portal-wording-assets.log`. No links or heading anchors changed,
and no Markdown checker was added. Firmware was not rebuilt because only portal
copy and prose changed, not the firmware contract or native behavior.
No flash, commit or push occurred.

### Outcome entry template

```text
Phase / date / implementer:
Status: Host verified | Hardware pending | Accepted | Blocked | Deferred
Revision or scoped change reference:
Files changed:
Behavior implemented and intentional deviations:
Exact checks and results:
Board / selected controller / identification method:
Configuration / mode / rotation / temperature / supply / background load:
Scenarios and sample counts:
Median / p95 / max for dispatch-to-ready:
Median / p95 / max for policy wait and prior-waveform wait, separately:
Preparation / packing-upload / waveform / end-to-end results:
Ghosting / unchanged pixels / recovery / OTA / screenshot results:
RAM / stack / idle-power observations, where relevant:
Documentation and metadata updated:
Unverified behavior or blocker:
Decision: accept | revise | defer
Next phase or user approval needed:
```

### Current handoff

* Implementation: phases 1-2 host-verified; separately approved Sticky render/
  packing and local-variable binding follow-ups host-verified, single-board
  firmware builds passed
* Hardware evidence: functioning SSD1677 BUSY-selected unit, six correlated
  pre-optimization and ten post-optimization requests; device stage improvements
  measured; ten completed local-variable counter requests now show prompt
  coherent readiness. Physical-visible timing/quality pending
* Investigation: cached navigation retains fonts; a host probe confirms the
  16-glyph residency cliff and shows why layout also incurs rasterization.
  Full root redraw on a different pad is correct under the existing LVGL path
* Approved follow-up: user selected 96 glyphs per monochrome instance; production
  limit, existing tests and developer descriptions updated. Temporary probes
  removed; font-memory host estimates recorded above. Eight focused checks and
  the Sticky firmware build pass; no compiler warnings/errors
* New device evidence: cached pad-15 coherent readiness improves from
  1,026.636 to 444.168 ms and publication from 1,849.500 to 1,275.345 ms in
  single samples; measured render glyph-miss time falls from 277.621 ms to zero.
  Late snapshot shows about 5.99 MiB PSRAM and 90.6 KiB internal RAM free;
  peak/stress memory remains unqualified
* Conversion investigation: captured S3 ELF confirms a runtime remainder per
  pixel and division per packed byte despite power-of-two group sizes.
  Conversion is 216.176 ms; external callback work and framebuffer wait are
  small. CPU/cache/preemption shares remain unmeasured
* Arithmetic experiment rejected and reverted: new capture showed no material
  gain (cached touch conversion 216.872 ms versus 216.176 ms), with pad/HA
  differences preventing controlled attribution. Driver exactly matches HEAD;
  4/4 restored-source checks and warning-free Sticky rebuild pass. Regenerated
  ELF matches pre-experiment `fa9b56d2d`; the driver has no local edits.
  Running device firmware has not been changed by the agent
* Approved portal wording follow-up: generic capability-based notes and matching
  user guide updated. Defaults/ranges/behavior unchanged; 2/2 focused portal/
  e-paper checks and production asset generation pass. Current edits also
  include the shared fragment and owning user topic
* Next gate: separate approval for bounded conversion CPU/cache/scheduling
  diagnostics on a fixed workload, not another speculative optimization.
  Screen application/layout is a separate later candidate. Controlled cached
  navigation distributions and device memory/stress acceptance remain pending.
  No flash, commit or push authorized.
  Do not bundle label-dedup changes or start phases 3, 4 or 6. Complete physical
  visible-onset/quality acceptance separately; the latest capture confirms
  passive 1000/presentation 500 ms
* Deferred research: controller-specific questions arising from failed A/B tests,
  not another broad CrossPoint architecture audit
* Final narrative owners: display/touch architecture, Interactive E-Paper user
  topic and board summary. No new control or setting; the historical audit
  stays intact.
