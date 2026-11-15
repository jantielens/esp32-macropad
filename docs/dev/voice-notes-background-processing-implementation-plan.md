---
title: Voice Notes background processing implementation handoff
description: Coding-agent contracts, implementation phases, and verification for recording-first automatic transcription.
---

## Status and authority

Ready for a coding agent when implementation is explicitly requested.
The [background processing PRD](voice-notes-background-processing-prd.md) owns
product scope. This handoff fixes implementation-facing behavior, UX, and
verification decisions; creating it does not authorize firmware changes.

The [original transcription handoff](voice-notes-transcription-implementation-plan.md)
remains relevant for provider requests, transcript storage, and isolation, but
its manual-only scheduling restrictions are superseded by this increment.
Do not reopen approved product choices during coding. If implementation reveals
a concrete conflict with safety or recording priority, explain the blocker and
seek guidance rather than silently weakening the requirement.

## Non-negotiable guidelines

* KISS/DRY: extend the existing transcription worker and operation. Do not add a
  second scheduler task, duplicate request paths, durable jobs, per-note retry
  timers, a workflow engine, or a provider/dependency framework.
* Prefer PSRAM for every eligible allocation, including growth, configuration
  snapshots, temporary catalog JSON, request/response data, and serialization.
  Internal RAM is precious. Do not silently fall back to internal RAM for large
  optional allocations when PSRAM allocation fails.
* Task stacks that perform storage/flash operations must remain internal under
  the existing RTOS contract. Preserve this justified exception rather than
  putting a flash-writing worker stack in PSRAM. Keep internal state small.
* Use only `HAS_VOICE_NOTES` for feature isolation. Guard implementation,
  configuration/NVS handling, actions, bindings, API fields, components, and
  selected web assets. Do not create a new feature flag or disabled-feature
  placeholder service.
* Greenfield: no aliases, migrations, legacy fallbacks, dual configuration
  shapes, or backwards-compatibility branches. Breaking old configurations is
  acceptable when required for a clean contract. Avoid unrelated renaming.
* Recording interruption is normal behavior. Exceptional interactions may
  abandon background work, provided content, resource safety, and truthful
  completion are preserved. Do not prevent every unusual user action or
  reconstruct every interrupted status.
* Preserve existing power/shutdown policy. Background processing does not
  initiate Wi-Fi, keep the device awake, prevent reboot/shutdown indefinitely,
  or add recording steps. Enabled automation rediscovers missing results on a
  subsequent boot.

Before editing code, read the repository contracts for
[configuration](../../.github/instructions/adding-config-settings.instructions.md),
[actions](../../.github/instructions/action-types.instructions.md),
[bindings](../../.github/instructions/binding-system.instructions.md),
[portal](../../.github/instructions/web-portal.instructions.md),
[OTA/storage](../../.github/instructions/ota-activity.instructions.md),
and [feature gates](../../.github/instructions/compile-time-flags.instructions.md).
Follow [verification rules](../../.github/instructions/agent-guidelines.instructions.md).

## Current implementation findings

* [Transcription](../../src/app/voice_notes_transcription.cpp) already owns a
  lazily created, persistent 8192-byte internal-stack worker, a single job slot,
  PSRAM configuration/job snapshots, and latest-attempt status. Extend this
  ownership; do not retain this worker alongside a new scheduler task.
* [Request transport](../../src/app/voice_notes_transcription_request.cpp)
  streams WAV data and stores response data in PSRAM. `HTTPClient` calls can
  block; checkpoints in upload/response callbacks do not establish prompt
  cancellation during every network phase.
* [Recorder](../../src/app/voice_notes.cpp) publishes `session_active` when Start
  is accepted, before notifying its separate worker. It does not wait for
  transcription. Preserve that independent admission path.
* The current transcription checkpoint tests the recorder's current state and
  OTA epoch. A short recording entirely inside a blocking HTTP call can escape
  that current-state check. Latch recording interruption as specified below.
* Recorder catalog methods serialize access with a catalog mutex; WAV readers
  hold leases that already prevent deletion. Transcript writes use the storage
  facade and atomic replacement. Reuse these paths.
* [The component](../../src/app/components/voice_notes_component.cpp) already
  exposes list/status/detail/transcribe/file/delete operations.
  [The feature script](../../src/app/web/portal_voice_notes.js) polls status
  every two seconds and owns its Settings save request. Extend these owners.
* [Configuration POST](../../src/app/web_portal_config.cpp) already stages
  Voice Notes changes in PSRAM, saves before runtime application, and rolls back
  the RAM candidate on save failure.
* `VoiceNotePayload.command` is currently eight bytes. The new `transcribe`
  command needs eleven bytes including its terminator; increase the field to
  sixteen bytes rather than allowing truncation.

## Fixed external contracts

### Configuration

Add `bool auto_transcribe = false` to `VoiceNotesTranscriptionConfig`.
Use `voice_notes_auto_transcribe` consistently for the `/api/config` field,
registered frontend field, and checkbox `id`/`name`. Persist it as NVS boolean
`vn_auto_tx`, default false, under the existing feature gate.

GET returns the actual boolean. POST accepts only JSON booleans; omitted means
unchanged. Include this field in the existing PSRAM candidate/previous
configuration update. Verify persistence and report save/apply failures through
the existing API/logging conventions; do not report runtime application when
it failed. Blank submitted API keys continue to retain the saved key.

Save applies without reboot. A toggle-only change does not clear an
authentication block, ordinary backoff, or service cooldown. An actual change
to endpoint, key, or deployment clears the setup block and ordinary backoff.
It does not bypass an outstanding service-directed rate-limit cooldown.
An in-flight operation uses its accepted configuration snapshot; a concurrent
settings edit need not cancel it or provide special recovery reporting.

### Device action

Persisted payload:

```json
{
  "type": "voice_note",
  "command": "transcribe"
}
```

Add Start transcription to the existing action editor command options and
describe/validation metadata. Accept no `scenario`, note ID, or extra fields
on this command. Preserve the existing five recording commands.

Dispatch this command to coordinator admission, not `voice_notes_command()`.
Return `ACTION_COMPLETE` when a pass is accepted, or when there are no notes
requiring work; this acknowledges initiation, not completed transcription.
Return `ACTION_FAILED` and log the reason for offline, busy, incomplete setup,
service cooldown, initialization, or resource rejection. Do not wait for a pass
or use an action continuation to represent its eventual completion. Existing
recording commands retain their continuation behavior.

A pass request is never remembered when admission fails. Repeated presses while
processing is owned do not extend or enqueue passes. A manual request can
bypass ordinary failure backoff and a prior setup-error block, but cannot
bypass missing credentials, recording/OTA/resource checks, or a service
rate-limit cooldown.

### Portal and bindings

Keep `/api/component/voice-notes/transcribe?id=...` as a single-note operation,
including replacement of an existing transcript. It uses coordinator admission
and the same explicit-retry policy as the device action; it never creates a
batch. Preserve existing HTTP outcomes and add HTTP 429 for an active
service-directed rate-limit cooldown, with a concise retry-later message.
Do not add a batch endpoint.

Keep the existing `recording`, `last_saved`, `transcription`,
`transcription_configured`, and storage fields in the status response.
Add this coordinator object:

```json
{
  "processing": {
    "state": "waiting",
    "reason": "offline"
  }
}
```

`processing.state` is exactly `idle`, `waiting`, `running`, or `blocked`.
`processing.reason` is exactly `none`, `offline`, `recording`, `update`,
`retry`, or `configuration`. Do not add pending counts, job IDs, retry
countdowns, or per-note error maps.

Publish `[voice_note:processing.state]` from the same safe in-memory snapshot.
Keep existing transcription bindings and their since-boot attempt semantics.
Add the key, documentation, authoring metadata, and one example to the existing
`voice_note` scheme; do not register another scheme.

## Coordinator design

### Ownership and initialization

Keep coordinator and operation code in
[voice_notes_transcription.cpp](../../src/app/voice_notes_transcription.cpp)
and its header. Named internal functions are sufficient separation between
policy and execution; new production modules are not required.

Extract the existing per-note worker body into one operation function returning
a typed outcome. The single existing worker invokes it for both passes and
portal single-note requests. Only this worker owns network objects, open
recording readers, response buffers, pass traversal, and result cleanup.

Expose small feature-gated APIs named `voice_notes_processing_init()`,
`voice_notes_processing_start()`, and `voice_notes_processing_snapshot()`.
Retain `voice_notes_transcription_start(id)` as the per-note admission facade.
These are local subsystem APIs, not a generic service interface.

Call processing initialization in `app.ino` only after successful
`voice_notes_init()`. Earlier configuration loading can store settings but must
not start work before storage/recorder initialization. Create the worker lazily
when enabled automation or an accepted explicit request needs it. A failure to
create it must be reported; recording remains usable.

Wake the worker for accepted requests and configuration updates. Use a
notification wait with a one-second maximum eligibility-check interval, not a
busy loop. Refresh the pending-work view at most every five seconds when idle;
an accepted explicit request or configuration change can request an earlier
refresh. Do not perform filesystem scans, network I/O, or long mutex waits from
the Arduino loop, recorder admission path, or binding resolver.

Use the existing subsystem mutex for short copies/admission. Never hold it
across a catalog scan, HTTP call, transcript save, or recording handoff. Accept
at most one explicit request/pass. A pass owns admission until it ends, even
between note operations; competing portal/device requests return busy.

### Pass selection

Add a recorder-owned candidate-selection helper using `g_store.copy_manifest()`
with a bounded PSRAM JSON allocator, not a JSON `String` round trip. Keep
filesystem details under the recorder/storage owner. Return a typed result
distinguishing candidate, no candidate, interruption, and storage/allocation
failure. Defer idle discovery during recording or OTA activity, and check for
interruption before copying the manifest and while traversing its entries.
An interrupted scan retains pending-work knowledge, does not advance the pass
cursor or failure backoff, and can be reconsidered once the device is available.

Order by stored `created_at` ascending, then note ID lexicographically.
Unknown timestamps (`0`) sort first; ID order is a deterministic tie-breaker,
not a claim about actual capture order. No new persisted sequence is needed.

Keep only the last visited `(created_at, id)` cursor. Find the next greater
untranscribed entry, using the existing transcript/backup presence rule.
Advance the cursor when visiting a note, including note-specific failure, so
no note repeats within that pass. Recheck existence and result presence before
automatic/manual-pass execution. Portal single-note requests deliberately
permit retranscription.

Release the catalog mutex and temporary JSON before network processing. Keep
the existing reader lease across operation execution and persistence; release
it on every completion path. A disappearing candidate can be skipped. Failed
catalog access is not an empty catalog or successful pass.

Do not retain a full batch membership list, quarantine notes, change index
format, or parse every saved transcript on each scan. Files added/deleted while
traversing need safe outcomes, not snapshot guarantees.

### Scheduling and retry rules

Use these fixed constants, not runtime settings:

* Idle catalog refresh interval: five seconds
* Eligibility recheck interval: at most one second
* Failed-pass delays: 30, 60, 120, 240, 480, then 900 seconds
* Maximum ordinary backoff: 900 seconds

Use monotonic timing and checked arithmetic for deadlines. A service-directed
cooldown is a minimum delay and may exceed the ordinary backoff cap.

Automatic mode starts a pass when enabled, pending work exists, setup is
complete, recording is inactive, Wi-Fi is connected, OTA/resource admission
allows work, and retry deadlines have elapsed.

A pass with any note failure is unsuccessful for backoff purposes, even if
later notes succeed. A clean completed pass resets ordinary backoff. An empty
scan does not repeatedly escalate it. Recording/OTA interruption ends the pass
without counting as a service failure; retain existing failure backoff rather
than resetting it. Wi-Fi loss during a request is a shared failure.

When automation remains enabled, reconsider work after a pass ends. When
disabled, no automatic continuation exists. Disabling automation ends an
automatic pass after the active operation; an independently requested manual
pass may continue. A manual pass cannot survive reboot, recording interruption,
Wi-Fi loss, or a shared failure.

Explicit requests clear an ordinary retry wait and allow a fresh attempt after
a setup block. They do not clear a service-directed cooldown. If the fresh
attempt again reports setup failure, suspend automatic attempts again.

### Typed operation outcomes

Do not infer scheduling from user-facing error strings. Extend request results
with a small typed classification and optional service cooldown duration.
Use these mappings:

| Outcome | Treatment |
|---------|-----------|
| Success after commit | Keep result; continue pass |
| Recording/OTA interruption | End pass; no failure escalation |
| Note absent or already completed before execution | Skip without failure |
| HTTP 400, 413, 415, 422; no speech; invalid WAV | Note failure; skip and mark pass unsuccessful |
| HTTP 401, 403, 404 | Setup failure; end pass and suspend automation |
| HTTP 429 | Shared failure; end pass and honor rate-limit cooldown |
| Other HTTP rejection, transport/TLS/DNS/read/framing/JSON-response failure | Shared failure; end pass |
| Storage failure, allocation failure, exhausted reader resources | Shared failure; end pass |

This is a coarse policy, not an exhaustive provider taxonomy. Keep concise
existing error text where appropriate. Preserve response text validation and
the existing service contract.

Collect numeric `Retry-After` seconds and `retry-after-ms` headers for 429;
when both are valid, use the larger duration. Support a valid HTTP-date
`Retry-After` when device time is available. If absent, malformed, or unusable
without clock synchronization, use ordinary backoff. Do not add time-sync
requests or provider-specific quota discovery. Ensure HTTP 429 without usable
guidance establishes at least the current ordinary delay as a cooldown, so
repeated explicit presses cannot hammer a rate-limited service.

Record a cooldown even when automation is off. Ordinary manual-pass failures
schedule no continuation, but their shared cooldown/backoff bookkeeping can
inform automation if it is later enabled.

## Recording-first interruption

Add a monotonic recording-generation counter to the recorder snapshot. Increment
it atomically with accepted Start admission inside the existing short recorder
critical section. A job captures that generation together with the OTA epoch.

Every existing transcription checkpoint must also verify the captured recording
generation. This latches interruption even if a recording starts and ends
before a blocked network call returns. Recheck before opening work, during
stream callbacks, after blocking calls, and immediately before persistence.

Do not make Start acquire the transcription/catalog mutex, wait for the worker,
close another task's TLS object, or call an HTTP teardown function. Recording
must proceed through its existing worker without waiting for request completion
or timeout. The operation owner abandons work and releases its resources at
safe checkpoints; an in-flight network call may still unwind independently.
No new operation starts until cleanup has completed.

Do not claim remote cancellation: an already sent request may complete remotely.
If a blocking transport phase retains a resource that actually prevents capture,
fix that concrete contention with owner-safe interruption. Merely adding a flag
or declaring a shorter network timeout does not satisfy recording priority.
Do not build a generic cancellation framework or forcibly kill a task.

Keep catalog locks short and outside network operations. Maintain existing
`StorageMutationGuard` and atomic-write boundaries. A recording arriving during
an admitted atomic save need not roll it back; if commit succeeds, the attempt
is successful and the transcript is retained. Finish safe cleanup, then stop
the pass. Never publish `ready` before commit or undo a completed transcript.

## Snapshot and UX behavior

Coordinator status is cached by the owner; binding/status consumers only copy
it. Resolve conditions in this order:

1. Owned active operation or admitted pass: `running` / `none`.
2. No active work and automation off, or no pending work: `idle` / `none`.
3. Missing settings or latched setup failure: `blocked` / `configuration`.
4. Recording session active: `waiting` / `recording`.
5. OTA/resource admission unavailable: `waiting` / `update`.
6. Wi-Fi disconnected: `waiting` / `offline`.
7. Retry/cooldown deadline outstanding: `waiting` / `retry`.
8. Otherwise eligible work awaiting worker admission: `waiting` / `none`.

The snapshot may lag conditions by one eligibility interval and catalog changes
by one refresh interval. That is acceptable. Do not scan storage to make a
binding exact. Disabling automation during an active operation still reports
`running`; once no explicit pass/operation remains, report `idle`, even if the
latest attempt failed. Attempt errors may remain independently available.

In the existing Settings fragment, add the checkbox Automatically transcribe
saved notes. Register/load it and include its actual checked boolean in the
feature's custom save request. Keep the existing credential controls and Save
button. Use the PRD's help text, including existing notes and recording priority.

In Notes, add one inline `role="status"` text element, visible in catalog and
details, not a new panel. Map reasons to Waiting for Wi-Fi, Waiting for recording
to finish, Waiting for device update or storage availability, Retrying later,
and Check transcription settings. `waiting` / `none` displays Waiting to
transcribe. Hide this text for `idle` and `running`; existing attempt stages
show active processing.

Keep durable row status Not transcribed or Transcript ready, plus active/latest
attempt stages/errors for the matching note. No retained per-note failure
history is required. Render all result/error text with `textContent`.
Do not disable recording, playback, deletion, or unrelated settings to avoid
unusual interactions; existing busy outcomes and safety rules remain valid.

The worker can complete several notes between two portal polls. Therefore
latest-attempt status alone cannot update every catalog result. While a pass
runs and once when it stops, reconcile `has_transcript` from the existing list
endpoint at the existing two-second poll cadence. A newly observed terminal
attempt also triggers reconciliation when a pass finished between polls;
unchanged terminal status must not keep fetching the catalog while idle.
Failed reconciliation remains eligible for retry. Update rows in place without
rebuilding or stopping audio players. If selected details gains a transcript,
fetch the existing detail endpoint once to display it. Preserve existing
fragment disposal and stale-response handling.

## Implementation phases and exit gates

### Phase 1 - Configuration and operation contracts

Update configuration type/load/save/API handling, typed request outcomes, and
rate-limit header handling. Keep the provider URL, authentication, and
multipart contract unchanged. Extract one reusable operation from the existing
worker body without duplicating parsing or persistence.

Exit gate: configuration omission/type/save/apply behavior is explicit; existing
manual transcription and saved-result safety remain covered.

### Phase 2 - Scheduling and recording priority

Implement initialization, shared admission, cursor traversal, worker wake/wait,
automatic policy, explicit passes, shared backoff, and cached snapshots.
Add the recorder generation/checkpoints and preserve reader/mutation ownership.

Exit gate: existing host tests demonstrate sequential passes, failure isolation,
retry timing, no durable manual intent, and recording admission while a fake
request is blocked. A short recording cannot escape latched interruption.

### Phase 3 - Action, bindings, and portal

Grow the action command field, add command dispatch/metadata, and extend binding
schema documentation/authoring. Update the existing feature fragments/script,
component status response, deterministic mock server, and fixtures together.

Exit gate: setting save/load works without reboot; offline action presses
schedule nothing; existing action chains do not await transcription completion;
portal results remain correct when polling misses intermediate completions.

### Phase 4 - Isolation, documentation, and verification

Run focused native/frontend checks, regenerate binding docs, reconcile user/API
documentation, and perform one approved verification-board build. Do not run
all-board builds. Leave hardware/live-service validation explicitly pending for
the product owner.

Exit gate: the final report names commands and outcomes, generated metadata,
documentation owners, remaining hardware checks, and any concrete blocker.
No commit, push, flash, or live service invocation is authorized by this plan.

## Expected file ownership

| Owner | Required changes |
|-------|------------------|
| `src/app/voice_notes_transcription.{h,cpp}` | Configuration boolean, single worker/coordinator, shared admission, operation outcome, snapshots |
| `src/app/voice_notes_transcription_request.{h,cpp}` | Typed failures, rate-limit headers, latched checkpoints |
| `src/app/voice_notes.{h,cpp}` | Candidate selection and recording generation |
| `src/app/app.ino` | Processing initialization after recorder success |
| `src/app/config_manager.{h,cpp}` | Gated setting storage/default/verification |
| `src/app/web_portal_config.cpp` | Boolean GET/POST, persisted-before-apply flow |
| `src/app/pad_config.h`, `src/app/actions/voice_note_action.cpp` | Payload capacity, command parsing/dispatch/catalog metadata |
| `src/app/voice_notes_binding.cpp` | New snapshot key, docs, authoring example |
| `src/app/components/voice_notes_component.cpp` | Processing snapshot and cooldown response |
| `src/app/web/voice-notes*.fragment.html`, `src/app/web/portal_voice_notes.js` | Checkbox, inline status, result reconciliation |
| `tools/portal-dev-server.py`, `tools/mock-data/binding-docs.json` | Deterministic contract fixtures and generated binding reference |
| Existing focused tests and `tests/CMakeLists.txt` where needed | Behavior checks and registration |

Keep existing component/action aggregation and asset gates. No new registration
path, feature flag, board capability, or shared portal bundle field is needed.
Existing `voice_notes*.cpp.o` size ownership covers this design; do not regenerate
compile-time flag docs when no flag changed.

## Focused validation

Extend [the existing recorder/transcription tests](../../tests/test_voice_notes.cpp)
and their host doubles rather than building another platform harness.
Use controllable host time/connectivity/request outcomes for deterministic
timing checks; do not sleep through production retry intervals.

Cover representative contracts:

* Automation off versus enabled; missing results found after fresh initialization
* A/B/C sequential traversal, B failure skipped, and no retry of B in that pass
* Shared failures ending passes; escalating shared delays despite mixed successes
* Setup suspension, changed settings, explicit retry, and rate-limit cooldown
* Manual offline/busy rejection with no later work after connectivity returns
* Disable during an automatic attempt; no next automatic operation
* Capture starts while a request is blocked, including Start then Discard before
  it unblocks; no stale commit and no recording wait for request completion
* Saved-before-interruption success, save failure, and preservation of old results
* Allocation failure, reader cleanup, and continued recording availability

Keep new cases focused. Extend
[action tests](../../tests/test_action_parse.cpp),
[binding profile stubs](../../tests/binding_schema_profiles/stubs.cpp),
[portal tests](../../tests/test_portal_voice_notes.js),
[asset gates](../../tests/test_portal_asset_gates.js), and
[mock-server tests](../../tests/test_portal_dev_server.py) for the changed
contracts. Use a new request-focused test only if existing coverage cannot
exercise the new failure/header contract; justify it and register it in CTest.
The existing `test_voice_transcription_request` targets Voice Assistant and is
not evidence for the standalone Voice Notes request.

Run the smallest relevant selector after each phase. Final focused commands:

```bash
cmake -S . -B build/host-tests
cmake --build build/host-tests --parallel --target \
  test_voice_notes test_voice_notes_sd test_action_parse_voice_notes \
  test_action_parse test_binding_schema_full test_binding_schema_full_no_psram
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_voice_notes(_sd|_no_psram)?|test_action_parse(_voice_notes)?|test_binding_schema_(full|full_no_psram))$'
bash tests/test_action_catalog_completeness.sh
bash tests/test_mcp_scheme_parity.sh
python3 tests/test_config_identifier_guards.py
node --check src/app/web/portal_voice_notes.js
node tests/test_portal_voice_notes.js
node tests/test_portal_asset_gates.js
python3 tests/test_portal_dev_server.py
```

Regenerate final binding documentation with the existing emitter:

```bash
build/host-tests/tests/test_binding_schema_full --emit-json > tools/mock-data/binding-docs.json
```

For enabled/disabled profiles, prove that code/data, the new config field/NVS
handling, action payload/registration, binding key, API fields, and feature web
assets are absent when `HAS_VOICE_NOTES=0`, and present once when enabled.
Reuse existing native-object/catalog/asset checks; extend narrowly where the
new setting or command is otherwise unchecked.

Preview production assets using the existing device-free server:

```bash
python3 tools/portal-dev-server.py --port 8765
```

Open `http://localhost:8765/?profile=reterminal-sticky-interactive&fragment=voice-notes`.
Check desktop/mobile setting round trips, shared waiting guidance, missed
completion reconciliation, details refresh, and navigation away during polling.
Keep fixtures deterministic; no live cloud credentials or requests.

Firmware/native behavior changes require a single-board build during
implementation. Read `/memories/board-preferences.md`; if absent, ask which
verification board to use and save the answer as required by repository
instructions. Then run `./build.sh <approved-board>` and resolve errors and
new relevant warnings. Do not infer a preference from older planning prose,
run bare `./build.sh`, or flash hardware.

## Final documentation and hardware boundary

At implementation's commit-ready checkpoint, reconcile the smallest owners:

* [Audio and Voice Notes](../user/media/audio.md): automatic setting, manual
  action, optional binding, offline behavior, and retry limitations
* [Portal architecture/API](web-portal.md): boolean configuration field,
  coordinator status object, and rate-limit response
* This handoff and the PRD: any verified implementation constraints, without
  claiming hardware behavior from host results

Update generated metadata when its source contract stabilizes, not only at
the final narrative reconciliation. No README feature catalog, board capability
change, new user topic, or CHANGELOG entry is required solely for this plan.

The product owner performs later device validation: recording-start latency
during blocked TLS/upload/response, capture continuity, repeated attempts and
interruptions, internal free/largest/minimum heap, PSRAM usage, worker stack
headroom, actual power loss, reconnect/reboot rediscovery, and OTA coexistence.
Report these as pending until measured. No numeric capture-latency guarantee
is assumed.

Future summary/title operations can consume a saved transcript through the same
scheduling/operation boundary. Replacing a transcript does not regenerate or
invalidate existing summary/title artifacts or resend shared content.
Explicit regeneration, sharing authorization, and destination duplicate
handling belong to later features; implement none of their infrastructure now.
