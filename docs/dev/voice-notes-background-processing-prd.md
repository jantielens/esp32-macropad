---
title: Voice Notes background processing PRD
description: Automatic transcription, a best-effort manual trigger, and recording-first background coordination.
---

## Status and scope

Agreed product scope for the next Voice Notes increment. This PRD authorizes
documentation, not firmware implementation. Hardware validation will be
performed separately by the product owner after implementation.

The [implementation handoff](voice-notes-background-processing-implementation-plan.md)
fixes coding-agent contracts, implementation phases, and focused validation.

The [transcription PRD](voice-notes-transcription-prd.md) and its
[implementation handoff](voice-notes-transcription-implementation-plan.md)
define the existing transcription operation. This increment replaces their
exclusions of automatic transcription, automatic retries, reboot rediscovery,
and device-initiated transcription. It also strengthens recording priority:
waiting for a network operation's next checkpoint is insufficient if that
checkpoint is delayed by a blocking request.

Other existing contracts remain applicable, including standalone Voice Notes
ownership, provider configuration, safe transcript persistence, compile-time
isolation, storage and OTA safety, credential handling, and PSRAM-first resource
usage. No provider or deployment change is required.

## Product goal

Record now and transcribe automatically when possible. The device may remain
offline between recordings. Once connectivity and resources are available,
background processing finds saved notes without transcripts and processes them
one at a time, independently of any browser.

The device interaction stays Start recording, speak, Save recording. Background
work adds no mandatory control, confirmation, connectivity step, or waiting
screen. Recording always takes priority.

### Happy-path priority

Recording interruption is normal behavior and requires first-class support.
Exceptional interactions may fail or abandon background work. They must
preserve recordings, committed results, resource safety, and truthful
completion, but need not provide seamless continuation, exhaustive prevention,
or perfect status reconstruction.

Settings edits, repeated presses, and deletion races need safe outcomes, not
coordinated recovery workflows. Do not add guards merely to prevent unusual
user actions when safe failure or abandonment is sufficient.

## Greenfield policy

No backwards compatibility is required. Configuration, internal APIs, and
behavior may change when that produces a cleaner design. Do not add migrations,
aliases, legacy fallbacks, or compatibility machinery.

Use the action and binding names below as the target contract. Existing
transcription bindings are retained because their attempt-level meaning remains
useful, not as a compatibility obligation. Update directly dependent metadata,
schemas, fixtures, examples, and callers together during implementation.
Unrelated renaming and refactoring remain out of scope.

## Included feature set

* One Voice Notes background coordinator
* An opt-in automatic transcription setting, off by default
* Discovery of existing and newly saved notes without saved transcripts
* Sequential transcription with temporary-failure backoff and reboot rediscovery
* A non-durable device action to start a best-effort manual processing pass
* Recording-first interruption and safe cleanup
* Portal settings, saved-result status, and active/latest attempt information
* One coordinator-state binding alongside existing attempt bindings

## Automatic processing

### Enablement and eligibility

The portal's Voice Notes Settings owns the persistent automatic transcription
setting. Enabling it includes existing untranscribed notes as well as future
saved notes. Disabling it stops scheduling automatic operations; an operation
already running may finish. No immediate cancellation protocol is required for
this exceptional interaction.

An automatic attempt may start only when:

* Automation is enabled.
* A saved note exists without a saved transcript.
* Transcription connection settings are complete.
* Wi-Fi is connected.
* No recording session is active, including a paused session.
* No other Voice Notes processing operation is active.
* Existing OTA, storage, and resource-safety rules permit the operation.
* The coordinator's shared retry delay permits another pass.

Wi-Fi connectivity is a prerequisite, not proof of service reachability. Request
failures still require normal error handling. Voice Notes does not initiate a
Wi-Fi connection.

Visit untranscribed notes oldest first, at most once per pass. Skip a
note-specific failure and continue to later notes. A saved transcript is the
completion marker; automatic processing never replaces an existing transcript.

### Recovery and retries

Persist the setting and successful results, not intermediate work stages.
After reboot, enabled automation rediscovers missing transcripts. There is no
persisted running flag, queue, batch membership, or manual-request intent to
repair.

Automatic and manual processing use the same pass selection and operation
execution path. Their difference is scheduling: automation can wait and start
another pass; a manual pass ends without scheduling its own continuation.

Use one shared retry delay, not per-note retry schedules. A pass attempts each
note at most once. Note-specific failures are logged and skipped. A shared
problem, such as lost connectivity, service unavailability, rate limiting, or
storage failure, ends the pass.

With automation enabled, unsuccessful passes are followed by an increasing,
capped delay before another pass. A pass with skipped failures is unsuccessful
for this purpose even if other notes succeeded; an individual success must not
reset the delay and create a tight loop around a bad note. A successful pass
clears the failure backoff. Honor service-directed retry guidance. Exact
timing is an implementation choice, not a configurable product feature.

An invalid or unsupported recording may be retried in later passes. Accept
occasional unsuccessful requests rather than adding quarantine management or
a failed-note registry. Such a note must not prevent later notes from being
attempted in the same pass.

If a remote transcription succeeds but local persistence fails, report failure
and end the pass. A later pass may upload the recording again and incur another
charge. Do not add retained unsaved-result state or a retry-save workflow.

Authentication and configuration failures suspend automatic attempts until
relevant settings change or an explicit manual request permits a fresh attempt.
Show corrective guidance. A manual request may bypass ordinary failure backoff,
but not a service-directed rate-limit cooldown or recording/resource checks.
Do not require an exhaustive provider-error taxonomy.

Recording interruption is not a service failure. Abandon the attempt safely
and reconsider the note after recording ends if automation remains enabled.
Do not resume an upload from a saved position.

Only a current-pass cursor, shared retry bookkeeping, and active/latest attempt
information are needed. Do not require a fixed batch-membership snapshot,
per-note timers, or retained errors for every note. Retry timing, latest errors,
and configuration-blocked status may reset on reboot. Reboot must not be used
to imply success, but does not require preservation of retry history or an
exact retry deadline.

## Manual start action

Extend the existing `voice_note` action with the command `transcribe`:

```json
{
  "type": "voice_note",
  "command": "transcribe"
}
```

The device-facing label is Start transcription. This command nudges the same
coordinator; it does not select a note, create a durable batch, or enable
automation. It accepts no note ID or scenario parameter.

If Wi-Fi is unavailable, recording is active, processing is already running,
or a service-directed rate-limit cooldown applies, the command starts no new
work and records no deferred request. Incomplete settings also prevent a start.
Otherwise an explicit request may try again after a prior authentication or
configuration failure, without requiring a settings edit. Use existing
diagnostics for the reason; do not add a blocking device dialog or change the
recording screen.

When conditions permit, start a best-effort pass through eligible untranscribed
notes, oldest first, with at most one operation active. The action works when
automation is disabled. Repeated presses cannot create parallel operations,
extend a pass, or enqueue additional passes.

### Failure behavior within a manual pass

Each note is attempted at most once in that manual pass, using the same pass
traversal as automation. Nothing about the pass is persisted.

For example, with notes A, B, and C:

1. A succeeds and its transcript is saved.
2. B fails with a note-specific problem. Report that error and skip B.
3. C is attempted.
4. The pass ends without retrying B.

Failures shared by all notes do not justify continuing the pass. Lost Wi-Fi,
service unavailability, rate limiting, storage failure, invalid
credentials/configuration, or a new recording session ends it. OTA or other
resource conflicts must also be handled safely rather than repeatedly starting
conflicting work.

With automation disabled, unfinished notes wait for another explicit request.
A new manual pass permits another attempt at previously failed notes and may
bypass ordinary failure backoff or a previous setup block, but not a
service-directed rate-limit cooldown. With automation enabled, the coordinator
can later retry under its automatic policy.

Reboot ends a manual pass. Connectivity returning or recording ending does not
resume that pass unless automation is enabled. No cancellation, pause/resume,
or batch-management UI is required.

## Recording priority and resource safety

Starting a recording must not wait for a transcription request to finish or for
its network timeout. Background operations must support prompt, safe
interruption and resource release wherever they could otherwise delay capture.

Do not forcibly delete a task holding resources or interrupt a filesystem
mutation unsafely. A short safety handoff may be necessary, but must not become
a network-length wait or require another user action. Keep the mechanism
proportional to the actual conflict; no generalized cancellation framework is
required.

Preserve recordings and previously saved transcripts on every failure path.
Only report completion after the transcript is safely persisted. Interrupted
writes must not expose partial results as completed transcripts. A deleted
note must not be recreated by background processing.

If persistence has already committed when recording starts, retain the saved
transcript and count that attempt as successful. Do not discard completed work
merely to label it interrupted.

One-operation ownership applies to portal requests, automatic work, and the
device action together. Preserve manual per-note transcription and
retranscription in the portal, using the same operation and busy/resource
checks. No portal request may create a second active operation.

Hardware handoff latency and repeated-interruption behavior remain unverified
until tested on the selected device. The product owner owns that subsequent
validation. Record observed recording-start latency and confirm it is not
coupled to service response or timeout duration; do not claim a numeric
guarantee from source inspection or host tests.

## Portal experience

### Settings

Under Voice Notes, Settings, Transcription, add the toggle Automatically
transcribe saved notes, off by default.

Supporting text:

> Transcribe notes without a transcript when Wi-Fi is available. Includes
> existing notes. Recording always takes priority.

Retain transcription connection settings and existing credential protection.
Explain missing setup and link to Settings without blocking recording or
playback. Turning automation off leaves manual transcription available.

### Notes and details

Keep each note's durable status simple: Not transcribed or Transcript ready.
Overlay existing coarse stages and error information on the relevant note
when it is the active/latest attempt. Retaining separate status and errors for
every attempted note is not required.

Waiting information can be coordinator-wide within the existing Notes or
Settings view, rather than repeated for every note. Derive it from current
conditions. Concise descriptions such as Waiting for Wi-Fi, Waiting for
recording to finish, Retrying later, or Check transcription settings are
sufficient; do not add a persisted display-state machine or a separate
activity panel.

Show corrective guidance where applicable. When automation is off and a manual
pass has ended, do not claim a retry is scheduled. A note-specific error is not
a global configuration block. Losing older error details after another attempt
or reboot is acceptable; losing content or falsely reporting saved success is
not.

Retain Transcribe, Retry transcription, View transcript, and existing playback
and deletion behavior. A saved older transcript remains accessible when manual
retranscription fails. No queue screen, separate activity panel, percentage
progress, retry countdown, or workflow editor is required.

## Device bindings

Keep the existing attempt-level contract:

| Binding                               | Meaning                                |
|---------------------------------------|----------------------------------------|
| `[voice_note:transcription.state]`     | Active/latest attempt state since boot |
| `[voice_note:transcription.note_id]`   | Note associated with that attempt      |
| `[voice_note:transcription.error]`     | Latest attempt error                   |

Attempt states remain `idle`, `preparing`, `transcribing`, `saving`, `ready`,
and `error`. They describe the attempt, not whether automation is enabled or
whether more notes need processing.

Add `[voice_note:processing.state]` as a coordinator snapshot:

| Value     | Meaning                                                          |
|-----------|------------------------------------------------------------------|
| `idle`    | No operation is running and no processing is currently scheduled. |
| `waiting` | Enabled automation has unfinished work awaiting safe eligibility. |
| `running` | A Voice Notes background operation is active.                     |
| `blocked` | Requested processing requires configuration/settings correction. |

`waiting` covers connectivity, recording, OTA/resource availability, and retry
delays. `blocked` represents a shared setup problem preventing currently requested
processing, not an individual failed note. Derive state from current demand:
with automation off and no active operation or manual pass, the state is `idle`,
even if untranscribed notes or a previous setup error exist. The latest attempt
error remains separately available. An active operation is `running` even if
automation was disabled after it started.

The binding reads a safe in-memory snapshot, never storage or network I/O.
An optional pad label can use:

```text
Transcription: [voice_note:processing.state]
```

No default device screen changes are required. Defer remaining-note counts,
per-note lookup bindings, and extra coordinator bindings.

## Architecture and future operations

Separate scheduling policy from operation execution. The coordinator decides
whether and when work is eligible; the transcription operation owns its
request, response handling, and safe result persistence. Manual portal
transcription, the device action, and automation share that operation.

Future summary and title operations can depend on a saved transcript without
depending on each other. Future delivery can depend on the selected artifacts;
audio delivery need not wait for transcription. A downstream failure must not
invalidate completed upstream results.

Replacing a transcript does not automatically regenerate or invalidate an
existing summary/title, or resend shared artifacts. Those results may remain
based on the previous transcript. Future features may offer explicit
regeneration or resending; dependency-version tracking and automatic
invalidation are not required.

Preserve these boundaries, but implement no future handlers, dependency
registry, generic job schema, provider framework, or configurable pipeline.
One active operation across the coordinator is sufficient for this increment.

Sharing remains explicitly authorized, not an implicit consequence of
transcription. Destination-specific duplicate prevention and delivery receipts
belong to a future delivery design.

## Explicit exclusions

* Durable queues, manual requests, batch membership, and intermediate stages
* Persisted retry history, exact retry-deadline restoration, and error history
* Per-note retry schedules, quarantine management, and retained unsaved results
* Resumable uploads and exactly-once remote execution
* Feature-initiated Wi-Fi connection and auto-connect settings
* Automatic replacement of saved transcripts
* Summary, title generation, sync, and sharing implementations
* Generic workflow engines, plugins, and configurable retry policies
* New mandatory device screens, prompts, and recording steps
* Remaining-count bindings and queue-management controls

A remote request may complete before connectivity or power is lost. A later
retry may repeat the service request and incur another charge. Safe local
completion does not guarantee exactly-once remote execution.

## Acceptance and validation

1. Offline capture still requires only Start recording and Save recording.
2. Automatic transcription defaults off. Enabling it discovers existing and
   newly saved untranscribed notes without requiring a browser to remain open.
3. Automatic work waits while offline and starts when connectivity and other
   prerequisites permit. Voice Notes never initiates Wi-Fi connection.
4. At most one operation runs across automatic, device-action, and portal
   requests. Eligible notes are selected oldest first.
5. Starting recording interrupts conflicting processing without waiting for a
   service response or network timeout. Paused recording prevents new starts.
6. Passes attempt each note at most once. Note-specific failures are skipped
   without starving later notes; shared failures end the pass. Automation uses
   increasing, capped shared backoff after unsuccessful passes and respects
   service-directed cooldowns. Setup/authentication failures suspend automatic
   requests until relevant settings change or an explicit fresh attempt.
7. Reboot or connectivity loss preserves WAVs and saved transcripts. Enabled
   automation rediscovers missing results without stale running flags.
8. A transcript counts as complete only after safe persistence. Completed notes
   are not automatically transcribed again. Recording starting after commit
   does not invalidate success. A save failure ends the pass without claiming
   completion or requiring an unsaved-result recovery workflow.
9. Disabling automation prevents subsequent automatic operations while allowing
   the active operation to finish safely.
10. The `voice_note` `transcribe` command starts a non-durable pass when eligible,
    including with automation disabled. An offline or busy press schedules
    nothing and creates no future request.
11. A manual pass attempts each note at most once, skips note-specific failures,
    and ends on shared failures, recording, or reboot. Without automation,
    another press is required after interruption. Explicit requests can bypass
    ordinary backoff and previous setup blocks, but not service-directed
    rate-limit cooldowns or recording/resource checks.
12. Portal status shows saved-result availability, active/latest attempt
    information, and concise coordinator waiting/setup guidance. Per-note
    error history, queue management, and device prompts are not required.
13. Existing attempt bindings retain their documented meaning. The new
    `processing.state` reflects the coordinator through an in-memory snapshot.
14. `HAS_VOICE_NOTES=0` excludes coordinator code/resources, settings, actions,
    bindings, routes, and feature-exclusive portal assets.
15. Diagnostic logs identify note, operation, failure, and retry decisions
    without exposing audio, transcripts, credentials, or unrestricted responses.
16. Exceptional interactions may abandon work safely without seamless recovery
    or exhaustive prevention. Recording interruption remains normal supported
    behavior. With automation off and no active work, coordinator state is
    `idle` even after a setup failure.

During implementation, use existing focused tests first and extend them for
concrete scheduling, retry, interruption, persistence, action/binding, and
feature-isolation risks. Keep runtime metadata and fixtures synchronized.
Do not add tests that merely freeze internal state names or source spelling.

Hardware capture latency, memory headroom under repeated attempts, and
power/network interruption behavior require subsequent device validation by
the product owner. These checks are deferred, not passed. No live cloud request,
firmware build, or hardware claim is required for this documentation change.
