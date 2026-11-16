---
title: Voice Notes transcription PRD
description: Transcription-only minimum viable experience and extension boundaries for personal voice notes.
---

## Status and purpose

Approved product scope and handoff contract. The
[implementation handoff](voice-notes-transcription-implementation-plan.md)
specifies the engineering choices and verification gates. Approval of these
documents does not itself authorize firmware implementation.

The follow-up [background processing PRD](voice-notes-background-processing-prd.md)
defines automatic scheduling, retries, recording-first interruption, and a
non-durable device trigger. Its explicitly identified scope supersedes the
corresponding MVE exclusions below; this PRD retains the original increment's
contract.

The subsequent [enrichment PRD](voice-notes-enrichment-prd.md) defines title and
summary generation from saved transcripts and extends processing controls.
Its identified scope supersedes the corresponding exclusions below.

The minimum viable experience (MVE) adds manually initiated transcription to
saved voice notes. It establishes a useful, independently testable operation
before adding summarization or automatic background orchestration.
The [implementation handoff](voice-notes-transcription-implementation-plan.md)
records concrete engineering decisions, phases, and validation gates.

## Implementation priority

> [!IMPORTANT]
> Build the happy path first. KISS is a product requirement, not an optional
> cleanup step. Do not turn unusual user interactions into a recovery framework.

The required experience is: record, save, select Transcribe, and read the saved
result. Preserve the WAV, avoid crashes and corruption, handle resources safely,
and never report an unsaved transcript as saved.

If the user changes settings, starts conflicting activity, disconnects, or
otherwise disrupts processing, the operation may fail and require manual
re-initiation. Detailed state reporting after these interactions is best effort.
Log failures through existing facilities; do not silently claim success.
An explicit user-requested deletion is not accidental WAV loss.

Use small safety checks where necessary, such as a single-operation busy check,
bounded I/O, and safe persistence. Do not add exhaustive guards, cancellation
protocols, persistent error history, retry-save workflows, transactional
coordination across features, or a matrix of exceptional UI states.
Existing recording, storage, and OTA safety contracts still apply.

Voice Notes is fully standalone. It must not depend on Voice Assistant
configuration, headers, certificates, assets, or its device-class lifecycle.
The two features are mutually exclusive. Voice Assistant is inspiration, not
shared ownership; its eventual removal must not break Voice Notes. Do not
refactor or remove that class in this MVE.

## Product direction

The longer-term goal is a personal voice inbox: capture a thought quickly,
recognize and understand it later, and use or share the result.

Personal voice notes are the primary scenario. Lightweight reminders and to-dos
are important future scenarios. Meeting recording is secondary and depends on
microphone suitability. A calendar or general scheduling system is not a goal.

Capture takes precedence over enrichment. The recording interaction remains:
Start, speak, Save, done. Users must not choose a scenario, enter a title, wait
for transcription, or have internet connectivity to save a recording.

## Vocabulary and greenfield policy

The feature is named Voice Notes. A note is the complete item: its recording,
transcript, and future enrichment. Recording means the audio; transcript means
the text of what was said; summary means a condensed interpretation.
Voice inbox describes the longer-term experience, not a navigation label.

Use the following labels consistently:

| Surface                    | Wording                                              |
|----------------------------|------------------------------------------------------|
| Top-level navigation       | Voice Notes                                          |
| Catalog navigation         | Notes                                                |
| Configuration navigation   | Settings                                             |
| Recording controls         | Start recording, Save recording                      |
| Details heading            | Voice note, accompanied by recording date/time        |
| Playback action            | Play recording                                       |
| Transcription actions      | Transcribe, View transcript, Retry transcription      |
| Details action             | View details                                         |
| Whole-item deletion        | Delete note                                          |
| Settings section           | Transcription                                        |

This is a greenfield standard. Breaking changes are acceptable: do not preserve
older vocabulary, settings, folders, configuration shapes, API names, or action
names through aliases, compatibility paths, or migration machinery. The
implementation plan must identify and update directly dependent callers,
fixtures, metadata, and documentation together.

Existing recordings are not a compatibility requirement. Recording filenames,
directory layouts, catalog formats, and metadata contracts may change without
supporting old files or providing migration. WAV preservation requirements apply
to recordings managed by the new implementation, not compatibility with old ones.

Reuse useful existing behavior and infrastructure, but do not retain legacy
naming merely for compatibility. Update names within the feature and its
direct dependencies; this is not a mandate for unrelated repository-wide
renaming. No firmware or storage rename is performed by this documentation change.

## MVE scope

* Promote Voice Notes to a top-level web portal navigation item.
* Provide Notes and Settings beneath that item.
* Provide a dedicated note details view.
* Configure transcription connection settings in Settings.
* Initiate transcription of a saved note from the web portal.
* Run processing on the device independently of the initiating browser.
* Show coarse processing stages and actionable errors.
* Persist the transcript associated with its recording.
* Read the saved transcript in the web portal.
* Retry a failed operation manually.
* Transcribe again using current settings, replacing the single saved transcript.
* Provide detailed diagnostics through the existing serial logging conventions.
* Establish the `voice_note` action and binding contracts defined below.

### Explicit exclusions

* Summaries, generated titles, and one-line note descriptions
* Dedicated reminder, to-do, or meeting workflows
* Cloud storage sync, sharing workflows, and dedicated text export controls
* Embedded WAV metadata or changes to the WAV format
* Direct OpenAI and other provider modes; the MVE supports the verified Azure
  contract only
* Automatic transcription after recording
* Feature-initiated Wi-Fi connection and an auto-connect setting
* Automatic retries, a durable work queue, or automatic reboot resumption
* Percentage progress, upload counters, and estimated completion times
* A generic workflow engine or provider plugin framework
* Transcript version history, automatic audio splitting, and format conversion
* Exactly-once execution, exhaustive conflict handling, and durable error history
* Device actions to initiate transcription, new device screens, and mandatory
  scenario selection

## User experience

### Navigation

Voice Notes is a top-level item, with Notes as its default landing view and
Settings as the other navigation destination. Note details is a child view
opened from Notes, not a third navigation item.

Follow the portal's existing navigation, fragment, and accessibility patterns.
This changes the feature's placement, not the portal's overall navigation model.

### Notes catalog

Each entry shows recording date/time, duration, size, playback controls,
transcription status, and applicable actions:

* Transcribe whether or not a transcript already exists, except while processing
  is busy
* Retry transcription after a failure, with configuration guidance when needed
* One details-opening action: View transcript when a saved transcript exists,
  otherwise View details
* Delete note using the existing deletion-confirmation conventions

View transcript opens the same details view as View details, with the transcript
readily visible. Keep playback available without requiring transcription.
Processing stage and failure information belong to the relevant catalog entry;
do not introduce a separate activity panel.

Until generated titles arrive, identify notes by recording date/time. Leave room
for future titles and short descriptions without showing empty placeholders.
An empty catalog explains how to record a note without blocking access to Settings.

### Note details

Use a dedicated full view rather than a modal so long transcripts are readable
on desktop and mobile. Include:

* Back to Notes navigation
* Voice note heading and recording date/time
* Recording duration, size, and playback controls
* Full saved transcript when available
* Applicable transcription action, current stage, or actionable error

A note without a transcript still has a useful details view. Opening the view
does not start transcription. Keep catalog and details status consistent without
requiring the initiating browser to stay open.

Summaries and sharing can later extend this view; do not add empty sections or
disabled future controls now.

### Settings

Provide a separate Settings fragment under Voice Notes. Its Transcription
section owns the setup experience for the endpoint and API key, plus any
model/deployment selection described in the API contract below.

When transcription is not configured, Notes and note details explain the
requirement and link directly to Settings. Recording and playback remain
available. Use existing credential-handling conventions; never display saved
keys in plaintext.

Voice Notes owns its transcription settings independently of Voice Assistant.
Inspect existing request-handling patterns for useful techniques, but do not
introduce dependencies on that class or a shared AI configuration platform.

Future LLM endpoint/key settings and automation knobs belong here when their
features arrive, not as placeholders in the MVE.

### Successful transcription

1. The user records and saves a note with Start recording and Save recording.
2. In Notes or note details, the user selects Transcribe for that saved note.
3. The device accepts the request or explains why it cannot start.
4. The portal shows the current coarse stage.
5. After the transcript is saved, the portal shows completion and the text.

Use Preparing, Transcribing, and Saving transcript as coarse stage labels.
Show Transcript ready only after persistence succeeds. On failure, show
Transcription failed with a concise reason and the applicable next action.
These are user-visible descriptions, not a requirement for a persisted state
machine.

The operation does not hold a web request open until processing finishes.
Closing or reloading the browser does not cancel accepted work. Returning to the
portal normally shows current device-owned status or the saved result. Do not
add a durable status system to reconstruct unusual interruptions.

### Transcribing again

Keep Transcribe available after success when the processor is not busy. A new
attempt uses the currently configured endpoint, key, and model/deployment.
This supports trying a different model without a separate retranscription flow.

Keep one saved transcript per note, with no history. Replace it only after the
new result is successfully saved. If the attempt fails, retain the previous
saved transcript. A short explanation can state that another request replaces
the transcript and may incur service charges; no confirmation dialog is required.

### Connectivity and failure

The device must already have connectivity to the configured transcription
service. Portal access alone does not establish internet reachability.
The feature does not initiate a Wi-Fi connection.

If connectivity is unavailable or drops, a service request fails, or the result
cannot be saved, report the failure explicitly. Preserve the recording and any
previously saved result. Do not report completion until persistence succeeds.

The portal gives a concise reason and a manual retry path when appropriate.
Configuration or authentication failures should direct the user to correct the
problem rather than imply that retrying unchanged will fix it.

If the device reboots during processing, the recording remains available.
There is no automatic resumption in this MVE. The next portal visit must not
show a stale operation as still running; the user can initiate transcription
again. Do not persist work stages or add reboot recovery to achieve this.

## Device actions and bindings

Use `voice_note` as the umbrella namespace for both actions and bindings.
Replace the existing `voice_clip` action type and `recorder` binding scheme
without compatibility aliases. Do not rename the separate voice-assistant
contracts merely to unify these names.

No new device screen or hardware-specific button layout is required. Existing
pad actions and labels provide the recording controls and status display.

### Recording actions

Use one action type with explicit commands:

```json
{
  "type": "voice_note",
  "command": "start",
  "scenario": "general"
}
```

| Command   | Device-facing label | Behavior                         |
|-----------|---------------------|----------------------------------|
| `start`   | Start recording     | Begin a recording                |
| `pause`   | Pause recording     | Pause the current recording      |
| `resume`  | Resume recording    | Resume the current recording     |
| `save`    | Save recording      | Save the current recording       |
| `discard` | Discard recording   | Discard the unsaved recording    |

`scenario` is optional, applies only to `start`, and defaults to `general`.
Only `general` is accepted in the MVE; reject unknown scenarios rather than
silently substituting the default. Reject `scenario` on other commands.
The session retains its scenario, so Save does not need to specify it again.

The normal start action therefore needs no scenario field:

```json
{
  "type": "voice_note",
  "command": "start"
}
```

Future scenario values may include `todo` and `meeting`. Those specialize the
intent of a recording without requiring separate action types or a picker in
the default recording flow. Do not expose them before they are implemented.
Do not add toggle or combined record/save commands in this slice.

Transcription initiation remains portal-only. A future device command can be
added when note targeting is actually needed; do not design or implement that
contract now.

### Binding keys

Use `[voice_note:<key>]` with operation-specific key groups:

| Key                     | Meaning                                                        |
|-------------------------|----------------------------------------------------------------|
| `recording.state`       | Current recorder state                                         |
| `recording.duration_s`  | Captured seconds, excluding pauses                             |
| `recording.scenario`    | Active session's scenario, initially `general`                  |
| `recording.error`       | Latest recorder error                                          |
| `last_saved.id`         | ID of the last successfully saved note since boot               |
| `transcription.state`  | State of the active or latest transcription attempt since boot  |
| `transcription.note_id` | Note targeted by that attempt                                  |
| `transcription.error`   | Latest transcription-attempt error                             |

Example device labels:

```text
Recording: [voice_note:recording.state]
Duration: [voice_note:recording.duration_s] s
Transcription: [voice_note:transcription.state]
```

Keep the recorder lifecycle values: `idle`, `starting`, `recording`, `paused`,
`saving`, `discarding`, and `error`. Retain existing captured-duration behavior
rather than introducing a duration-history feature. `recording.scenario` is
unavailable when no session is active.

Transcription state values are `idle`, `preparing`, `transcribing`, `saving`,
`ready`, and `error`. `idle` means no attempt since boot. Other values describe
the active or latest attempt, identified by `transcription.note_id`.
`ready` means the result was saved; `error` describes the attempt even when an
older transcript still exists. Fast stages need not remain visible for a
minimum duration.

`last_saved.id` is an in-memory since-boot snapshot, not a persistent catalog
lookup. It may differ from `transcription.note_id`; never associate a
transcription status with the latest recording implicitly.

Clear each domain's error when its next operation is accepted. Empty or
unavailable values follow existing binding conventions, including pipe
fallbacks. These snapshots are best effort and reset on reboot, not durable
history or a promise to reconstruct disrupted operations.

Resolvers must use safe in-memory snapshots, not read files or perform network
requests. Do not add full-transcript bindings, arbitrary per-note lookup, or
persisted reporting history. Later summarization can add its own keys, such as
`summary.state` and `summary.note_id`, when the feature exists; no generic
processing-state abstraction is required.

### Registration and authoring

Follow the existing self-registering action and documented binding-scheme
patterns. Update directly dependent editor metadata, action/binding reference
metadata, fixtures, and examples to the new contract together. Reuse the normal
catalog/schema checks; do not add a parallel registry or compatibility layer.

## Functional and reliability requirements

* Process at most one transcription at a time for the MVE. Reject additional
  starts clearly; do not introduce a queue.
* Use the same busy check for repeated clicks and multiple portal clients;
  no request-deduplication or idempotency subsystem is required.
* Keep recording usable. Respect existing audio ownership, OTA activity, and
  resource-safety contracts rather than competing unsafely with capture.
  Reject transcription starts while recording. If recording starts during
  transcription, abandon transcription at the next safe checkpoint and let
  the user retry; do not pause and resume the operation.
* Do not impose artificial recording-size, duration, transcript-length, or total
  transcription-time caps. Honor provider limits and finite connection and
  inactivity timeouts. Check allocations, arithmetic, reads, and writes; report
  resource exhaustion rather than crashing or silently truncating output.
  These are safety boundaries, not new configurable product limits.
* Internal RAM is a critical resource. Use PSRAM wherever safe and practical,
  including transcript/response data, JSON pools, serialization buffers, and
  eligible task stacks. Stream audio rather than buffering an entire WAV.
  Keep unavoidable internal allocations small and justified by hardware,
  library, or flash-safety requirements. Do not silently spill large optional
  transcription allocations into internal RAM when PSRAM allocation fails.
  Fail the attempt safely instead, preserving recording and the saved result.
  Verify internal-heap and stack headroom on the selected device.
* Do not modify or rewrite audio to attach a transcript.
* Associate results with a stable note identifier.
* Persist results safely so an interrupted write cannot replace a valid result
  with partial data.
* A saved transcript must survive portal reloads and device reboots.
* Handle a note disappearing during processing without crashing, corrupting
  storage, or recreating its WAV. Rejecting a conflicting action or abandoning
  the transcription is sufficient; do not add coordinated cancellation or an
  orphan-reconciliation subsystem.
* Render transcript text as untrusted text, not executable portal markup.
* Use the existing credential-management conventions. Do not expose credentials
  in responses, metadata, or diagnostic logs.

The MVE does not guarantee exactly-once service execution. A request may have
completed remotely before a local failure or reboot. A manual retry can repeat
the request and incur another service charge.

### Compile-time isolation

`HAS_VOICE_NOTES` is the single feature gate for recording and transcription.
It defaults off and is enabled only by intended board configurations, preserving
the current hardware prerequisites and mutual exclusion with Voice Assistant.
Use compile-time exclusion, not hidden controls or runtime availability checks.

With the gate off, firmware must contain no Voice Notes-exclusive implementation,
static data, task resources, settings fields/NVS handling, action payloads or
registration, binding registration, portal components/routes, or embedded web
assets. Neither navigation nor action/binding catalogs may advertise the feature.
The Notes fragment, Settings fragment, and feature script must be absent from
selected assets, not merely unreachable. Feature-specific controls, config-field
registration, and script logic must not leak into always-loaded portal bundles.

Shared audio, WAV, TLS, storage, and portal infrastructure used by other features
remains available under its own existing gates. Do not add a separate transcription
flag, a new runtime capability framework, or disabled-feature placeholder modules.
Verify both enabled behavior and disabled exclusion with existing focused
compilation, catalog, and asset-selection checks.

## Transcription API contract

Use the verified Azure OpenAI deployment-style file transcription API for models
provided through Microsoft Foundry. Support exactly one request/response contract,
not arbitrary services selected by URL.

Configure three values:

| Setting                 | Meaning                                                   |
|-------------------------|-----------------------------------------------------------|
| Azure resource endpoint | HTTPS resource root, such as `https://<resource>.openai.azure.com/` |
| API key                 | Key for that resource                                     |
| Deployment name         | Exact transcription deployment name on that resource     |

The endpoint is not a Foundry project URL, an OpenAI v1 base URL, or a full
transcription operation URL. Append this route, URL-encoding the deployment as
a single path segment:

```text
POST /openai/deployments/{deployment}/audio/transcriptions?api-version=2025-03-01-preview
api-key: <key>
```

Upload the saved WAV using multipart fields `model` (the configured deployment
name) and `file` (filename `recording.wav`, MIME type `audio/wav`). Accept the
default JSON response and extract its `text` string. Do not add interactive
authentication, Bearer/v1 fallbacks, model discovery, arbitrary headers,
configurable response mappings, or a provider registry.

Keep the API version fixed in the request implementation, not user-configurable.
Keep the resource, key, and deployment configurable; no deployment name is
hardcoded. A deployment named `gpt-4o-transcribe` succeeded in the local service
check recorded in the handoff. Other deployments must support the same contract.
Do not send model-specific options, language hints, or `stream=true`.

The local service contract is verified; device-side upload, persistence, and
display remain implementation validation gates.
Use explicitly selected non-sensitive audio and private local credentials.
CI and automated tests must not use a live service or require cloud secrets.

Direct OpenAI and other providers are deferred. Keep request construction
localized so a later feature can introduce another contract without changing
recording, persistence, or portal orchestration.

Use a non-streaming file-transcription response with checked resource usage.
Stream file bytes from storage; do not load an entire WAV into RAM. Reject inputs
that exceed service limits rather than splitting or transcoding them.
There is no additional product-specific size cap or fixed total request deadline.
Realtime sessions, remote batch jobs, and agents are out of scope.

References:

* [Microsoft file transcription workflow](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/transcribe-overview)

## Persistence and processing boundaries

Separate the transcription operation from its portal trigger. Later automation
must be able to invoke the same operation without simulating portal interaction.
REST handlers should delegate processing rather than own the workflow.
Keep provider-specific request construction and response parsing localized.
Borrow techniques from Voice Assistant where useful, not dependencies on its
helpers or conversation lifecycle. Reuse shared infrastructure outside that
class. A focused function boundary is sufficient; no provider framework or
shared AI configuration platform is required.

Keep transient activity in memory. Persist the transcript, not every stage
transition. A saved result is the durable checkpoint for later processing.
Ordinary progress and the latest error may live in memory. Their loss on reboot
is acceptable; saved transcripts must remain durable.

Reuse `FsIndexedStore` and the storage facade where appropriate. The current
recording implementation uses an indexed WAV collection with basic
recording properties. Select a durable result representation that preserves
transcripts through index reconstruction, not only ordinary reloads.
Do not treat a rebuildable index as the sole authoritative copy of user content.

The action/session scenario defaults to `general`, as defined above. Do not
add a mandatory selector or implement specialized scenarios for this MVE.
The handoff specifies the authoritative companion JSON representation; it is
not an open coding-agent decision. A redundant stored scenario field is not
required while every recording is `general`. WAV metadata remains out of scope.

## Diagnostics

Use the existing project logging facilities for serial diagnostics. Log the
note identifier, stage transitions, elapsed time, applicable service status
codes, and failure causes. Include enough context to distinguish connectivity,
request, response-parsing, and storage failures.

Do not log audio, transcript contents, credentials, or unrestricted service
response bodies. Keep portal messages concise; serial logs carry the technical
detail. Percentage reporting is not required in either surface.

## Future extensions and reasonable seams

These are directions to preserve, not additional MVE deliverables.
Future extensibility means preserving useful boundaries, not implementing
future infrastructure now.

### Summarization

Add a separate operation that consumes a saved transcript and produces a short
note description and a fuller summary. It should not require uploading or
transcribing the audio again. Summary failure must not invalidate a transcript.
Scenario can guide the output. Short notes may not benefit from a full summary.
If a transcript is replaced later, summary invalidation belongs to the
summarization feature; do not add result-version machinery in this MVE.

### Additional scenarios

Allow future entry points to supply specialized intents such as `todo` or
`meeting` alongside the default `general` scenario, without adding friction to
recording. Changing the intent later
should not require rerecording audio. Reminder functionality stays within the
device's limited alarm capabilities, not a calendar or scheduling product.

### Automatic processing and intermittent connectivity

Later, record whether transcription and optional summarization were requested,
and derive pending work from missing saved results. A single worker can resume
eligible work when connected. A separate policy may permit requesting Wi-Fi
through existing connection management.

Bounded retry/backoff and reboot recovery belong to that later slice. Avoid
persisted intermediate states and a general-purpose durable job system unless
a demonstrated requirement makes them necessary.

### Sync and sharing

Keep audio, transcript, and future summary independently addressable by note
identity. Future sync can copy selected artifacts to destinations such as
OneDrive or Google Drive. Sharing is a distinct, deliberate user action, not an
implicit consequence of sync. Do not implement connectors or sharing plumbing
in anticipation of this work.

### WAV metadata

Embedded WAV metadata is explicitly deferred but planned. Preserve the option
to embed stable attributes such as scenario and recording identity later.
Do not require WAV changes to deliver transcription now. The eventual format
and reader/writer behavior need their own design; legacy compatibility is not
a requirement.

### Simplicity constraint

Prefer a focused operation, durable note-associated data, and existing helpers.
Do not add speculative interfaces, empty future fields, plugin registries, or
generalized orchestration solely to accommodate this roadmap.
Do not expand the test scope into exhaustive combinations of disruptive actions.
Cover the happy path and concrete safety risks with existing focused checks.

## Acceptance criteria

1. Offline recording still requires only Start recording and Save recording and
   produces a usable note without any enrichment interaction.
2. A user can transcribe a supported saved note and read its persisted text in
   the portal without changing the audio file.
3. Closing the initiating browser does not stop accepted work. Reopening the
   portal normally exposes its current status or completed result; reporting
   after other disruptive actions is best effort.
4. The portal exposes a meaningful stage or failure without percentage progress.
5. Repeated starts cannot create concurrent transcription requests.
6. Missing connectivity, a connection drop, service rejection, invalid response,
   and storage failure are reported without losing the recording or falsely
   reporting success. Retry remains an explicit user action.
7. Saved transcripts survive reboot and index reconstruction. Interrupted work
   is not falsely displayed as still running after reboot.
8. Conflicting actions or deletion during processing do not crash the device,
   corrupt storage, or cause transcription to delete or recreate a WAV.
   Failed processing may require manual re-initiation; exact recovery reporting
   is not an acceptance requirement.
9. Transcript text cannot execute markup or scripts in the portal.
10. Serial diagnostics distinguish processing stages and failure causes without
    leaking credentials or audio/transcript contents.
11. Voice Notes appears at the top navigation level with Notes and Settings;
    Notes is the landing view and details is not a separate navigation item.
12. The catalog exposes recording metadata, playback, status, and applicable
    actions. A single details-opening action is labelled View details or View
    transcript as applicable and reaches the same dedicated view, which supports
    mobile reading and Back to Notes.
13. An untranscribed note can be opened and played without starting processing.
    A saved transcript is fully readable in details.
14. Missing transcription setup links to Settings without blocking recording
    or playback. Settings supports transcription setup without future placeholders.
15. User-facing vocabulary follows Voice Notes, note, recording, and transcript.
    The implementation does not add legacy aliases or compatibility machinery.
16. Transcribe remains available after success when processing is not busy.
    A successful new attempt replaces the sole saved transcript; a failed
    attempt preserves the previous saved result.
17. A real Azure OpenAI deployment in Foundry accepts a saved WAV through the
    selected API contract and returns a transcript that the device saves and
    displays. Direct OpenAI is outside the MVE.
18. The `voice_note` action supports start, pause, resume, save, and discard.
    Start defaults to `general`; explicit `general` is accepted, unknown
    scenarios and scenario fields on other commands are rejected.
19. The `voice_note` bindings expose the documented recorder and transcription
    snapshots without file/network I/O. Note identities and attempt status
    follow the since-boot semantics above, including failed retranscription
    with an older saved result.
20. Device action/binding metadata and examples use the new contract without
    legacy aliases. No new device screen or device transcription action is
    needed to complete the MVE.
21. Eligible allocations use PSRAM, including allocation growth. Device checks
    measure internal-heap headroom during processing and after repeated attempts,
    account for retained task memory, and justify unavoidable internal usage.
    Allocation failure does not consume an unbounded internal fallback or lose
    the WAV or a previously saved transcript.
22. With `HAS_VOICE_NOTES=0`, focused compilation and selected-asset checks
    confirm no feature-exclusive code/data, settings storage, action/binding
    registrations, portal routes, or Notes/Settings/script assets are included.
    Enabled-profile checks confirm the complete feature is registered once.

Validate these observable behaviors using existing focused checks and targeted
manual/device testing where needed. Extend existing tests for concrete risks;
do not introduce a custom harness merely to mirror implementation details.

## Handoff readiness and validation

No product or provider decision remains open. The handoff specifies standalone
settings ownership, durable result layout, compile-time isolation, best-effort
conflict handling, and safety timeouts. Do not reopen these as architecture
choices during coding without a concrete blocker and user input.

Device-format audio, ESP32 execution, memory headroom, feature-disabled exclusion,
and the complete portal experience still require the implementation checks in
the [handoff](voice-notes-transcription-implementation-plan.md). These are
verification work, not permission to redesign the scope.

## Implementation references

* [Current recording implementation](../../src/app/voice_clips.cpp)
* [Indexed-store API](../../src/app/fs_indexed_store.h)
* [WAV metadata reader](../../src/app/voice_wav_store.cpp)
* [Voice-assistant transcription request](../../src/app/device_classes/voice_assistant/voice_transcription_request.cpp)
* [Web portal architecture](web-portal.md)
* [Logging guidelines](logging-guidelines.md)
* [Action conventions](../../.github/instructions/action-types.instructions.md)
* [Binding conventions](../../.github/instructions/binding-system.instructions.md)
