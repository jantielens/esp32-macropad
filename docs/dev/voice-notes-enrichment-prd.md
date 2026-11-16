---
title: Voice Notes title and summary enrichment PRD
description: Transcript-based title and summary generation within the existing recording-first background coordinator.
---

## Status and scope

Agreed product direction for the next Voice Notes increment. This PRD records
requirements; it does not authorize firmware implementation. The
[implementation handoff](voice-notes-enrichment-implementation-plan.md) fixes
the Azure request contract, storage layout, interface names, and focused
verification for a coding agent.

The [transcription PRD](voice-notes-transcription-prd.md) and
[background processing PRD](voice-notes-background-processing-prd.md) remain the
baseline. This increment supersedes their exclusions of title and summary
generation, transcription-only automation and device initiation, and separate
future title and summary operations. Title and summary now form one enrichment
operation, with one request and one committed result.

Existing recording priority, standalone Voice Notes ownership, credential
protection, storage/OTA safety, power policy, and compile-time isolation remain
applicable. No cloud worker or Foundry hosted agent is introduced.

## Implementation priority

Build the normal record, transcribe, enrich path first. Reboot, settings edits,
deletion, and repeated requests may abandon processing. Preserve committed
content, release resources safely, and report failures honestly. Work may be
redone; do not prevent shutdown, reconstruct exact interrupted status, or add
coordinated recovery merely to preserve progress.

Starting another recording is normal use, not an exceptional interaction.
Recording-first interruption remains a first-class requirement.

## Product goal

Turn a saved voice note into an identifiable, readable item without adding
recording steps. Capture remains Start recording, speak, Save recording.
Connectivity, transcription, and enrichment are not prerequisites for capture.

A title is the quick identifier for a note. A summary condenses its content;
the transcript remains the full account of what was said. The recording,
transcript, and enrichment belong to the same stable note ID.

General is the only implemented scenario. Future to-do and meeting scenarios
may select different enrichment instructions and structured outputs. Do not
implement their fields, prompts, controls, or workflow infrastructure now.

## Greenfield policy

No backwards compatibility is required. Use processing terminology for
responsibilities that now span both operations. Use `auto_process` as the
automation concept, not a
transcription-named setting with broadened hidden semantics.

Replace the device command `transcribe` with `process`. Do not retain aliases,
old configuration fallbacks, migrations, dual storage formats, or compatibility
branches. Update directly dependent APIs, portal callers, metadata, fixtures,
examples, and documentation together during implementation.

Internal module/interface names belong in the implementation handoff. Keep
actual transcription settings and execution operation-specific. Existing
`transcription.*` bindings remain because their meaning is useful, not for
compatibility. Avoid unrelated restructuring.

## Enrichment operation

### Input and completion

Use the successfully saved, readable transcript as input. Do not upload audio
again, generate from an unsaved remote transcription response, or derive the
title from the summary. Both fields use the transcript in one LLM request.

The result contains exactly the required user-content fields:

```json
{
  "title": "Kitchen renovation ideas",
  "summary": "Ideas for renovating the kitchen include improving storage and comparing worktop materials before deciding on a budget."
}
```

Validate both fields and persist the enrichment as one result. Completion
requires successful safe local persistence, not merely a successful HTTP call.
Do not expose a title-only or summary-only success after partial validation or
an interrupted write. Model and generation-time metadata may accompany saved
content; exact storage details belong in the implementation handoff.

Preserve the recording, transcript, and any previous enrichment on failure.
Reboot or interruption must not expose partial content as completed enrichment.
A deleted note must not be recreated by processing.

### Structured output contract

Require a deployment supporting strict schema-constrained structured output.
The schema requires string fields `title` and `summary` and disallows additional
properties. Prompting for JSON or using JSON mode alone is insufficient.

The operation must distinguish successful complete output from refusals,
content filtering, truncation, transport errors, and unsupported service
configuration. Parse and validate locally, requiring a nonblank title and a
string summary that may be empty. Report unsuccessful attempts explicitly; do
not strip Markdown fences, scrape prose, silently repair output, or fall back
to another provider or free-form response mode.

Only distinguish provider failures when the distinction changes scheduling or
corrective guidance. No exhaustive provider-error taxonomy is required.

Schema conformance does not guarantee factual accuracy or identical wording
between attempts. Treat transcript content as untrusted input to summarize,
not instructions that override the generation task. Render all generated text
as untrusted text, never executable portal markup.

The handoff must verify a concrete Azure API, API version where applicable,
supported deployment, schema, and response/error envelope against
[Azure structured-output documentation](https://learn.microsoft.com/en-us/azure/foundry/openai/how-to/structured-outputs).
Do not assume the transcription endpoint's operation path or API version also
supports LLM generation. No live cloud request is authorized by this PRD.

### Content guidelines

Titles normally contain three to eight words in sentence case. Identify the
specific topic or intent, without generic prefixes such as Voice note or
Summary. Do not invent facts to make a title more specific.

Summaries use one compact paragraph, normally one to three sentences and
roughly 30 to 80 words when the source warrants it. Short transcripts may
produce much shorter summaries. These are writing targets, not minimum lengths
or reasons to pad content. Express them in generation instructions, not
word-count validators or retries of otherwise useful results.

An empty transcription response is a successful result saved as No speech
detected. It follows the same enrichment flow, with a title such as No speech
detected and an empty summary when there is nothing meaningful to summarize.
Saved results prevent automatic retries; no silence-specific orchestration or
persistent failure tracking is needed.

Preserve important intent, qualifications, decisions, and stated next steps.
Do not invent commitments, assignees, dates, or deadlines. Summarize directly
rather than starting with The speaker discusses. Use the transcript's language
by default; translation and language-selection controls are out of scope.

Do not silently truncate transcripts to fit a model context window. Provider
limits and allocation failures require explicit errors rather than incomplete
input disguised as a complete summary. Automatic splitting and multi-pass
summarization are excluded.

## Automatic processing and orchestration

### One automation setting

Broaden the existing switch to Automatically process saved notes, off by
default. No separate transcription, title, or summary automation switches are
needed. Each service's configuration determines which operations are available;
the switch determines whether they run automatically.

Supporting text must explain that automatic processing includes existing notes,
uses configured services, and gives recording priority. Explain that
transcription sends recordings to Azure and enrichment sends transcripts.
Saving service settings alone does not enable automation.

Enabling automation discovers missing results in existing and newly saved
notes. Disabling it prevents subsequent automatic operations; an operation
already active may finish safely. Accepted manual work remains independent of
the automatic setting.

### Artifact-derived eligibility

Use saved artifacts as the source of truth, not persisted workflow stages:

| Saved content                  | Next eligible work                         |
|--------------------------------|--------------------------------------------|
| Recording without transcript   | Transcription, if configured               |
| Transcript without enrichment  | Enrichment, if configured                  |
| Transcript with enrichment     | None                                       |

A remote success without a saved transcript never unlocks enrichment. A
missing or unreadable transcript prevents enrichment. Report unreadable saved
content when it is used; do not add automatic repair, deletion, or a corruption
recovery subsystem.
Existing transcripts can be enriched without transcription configuration.
Missing optional LLM configuration does not block transcription.

Keep one coordinator, one worker, and at most one active operation across
automatic, device-action, and portal requests. Traverse notes oldest first
using the existing ordering. Visit each note at most once per pass, allowing
at most one transcription and one enrichment attempt on that visit.

After transcription is safely saved, enrichment for the same note may follow
before visiting the next note. Recheck recording, connectivity, configuration,
OTA, and resource eligibility between operations. Do not reserve eligibility
for an entire chain or perform unavailable work merely because a pass began.

### Failure scope and recovery

* A note-specific transcription failure skips enrichment for that note and
  permits visiting later notes.
* A transcript save failure prevents enrichment and ends the pass.
* A note-specific enrichment failure preserves the transcript, skips that note
  for the remainder of the pass, and permits visiting later notes. A subsequent
  eligible pass retries missing enrichment without retranscribing.
* A failed retranscription leaves a previous saved transcript usable for
  missing enrichment. Eligibility follows saved content, not latest-attempt
  status.
* Connectivity loss, storage failure, recording interruption, or a conflicting
  OTA operation ends work safely under the existing coordination rules.
* Authentication/configuration failures suspend the affected service, not
  unrelated operations. An LLM setup failure must not block transcription;
  a transcription setup failure must not block enriching existing transcripts.
* A service-directed cooldown applies only to the affected service.
* Other shared or transient failures may end the current pass. Later automatic
  passes reconsider eligible work; continuing every independent operation
  during a failing pass is not required.

Allow three automatic retries after 30 seconds, 1 minute, and 5 minutes, then
halt the affected service until a new recording is saved, a manual request is
accepted, or its settings change. Count failed passes per service, not per note.
A new saved ID resets ordinary retry budgets for both services but preserves
setup blocks and provider cooldowns. Manual requests reset requested runnable
services; settings changes reset that service's budget and setup block.
Reboot resets in-memory bookkeeping.

Keep service-directed cooldowns and bounded service-level bookkeeping, not a
general availability engine. A delayed, halted, or setup-blocked service must
not prevent later passes from
selecting independent eligible work. Do not let a success in one service erase
the other service's failure delay. An individual note failure must not create
a tight retry loop.

Explicit requests may bypass ordinary backoff or permit a fresh setup attempt,
but never bypass the affected service's rate-limit cooldown or resource checks.
Do not attempt an ineligible upstream operation and then run its dependent
operation without the required saved input.

Enabled automation rediscovers missing results after reboot or interruption.
With automation off, interrupted manual work requires another explicit request.
Do not persist queues, pass membership, running flags, intermediate stages,
retry history, or manual intent.

Expose `transcription.retry_status` and `enrichment.retry_status` bindings:
empty before retries, Retry 1 of 3, Retry 2 of 3, Retry 3 of 3, or Retries
halted. These describe service-level cycles, not individual notes. When no
independent operation can run because retries are exhausted, expose
`processing.state` as blocked and its API reason as `retries_exhausted`.

### Recording and resource priority

Give other device tasks breathing room with a fixed two-second worker delay
after an attempted operation when the pass can continue. Release operation
resources before waiting, then use the existing eligibility checks before
more work. The coordinator remains running with no active service during the
gap; no new state or configurable pacing is needed.

Preserve prompt, safe recording admission without waiting for LLM responses or
network timeouts. Recording interruption is normal supported behavior, not a
service failure. Keep already committed results when recording starts and
release resources safely. The handoff must identify how the new operation
meets the existing interruption contract.

Reuse the baseline storage facade, OTA coordination, PSRAM-first allocation,
and flash-writing stack safety contracts. Do not add a parallel safety or
cancellation framework.

Processing does not initiate Wi-Fi, keep the device awake, or indefinitely
prevent shutdown/reboot. No extra recording step, waiting screen, or mandatory
device prompt is introduced.

## Manual controls and portal experience

### Everyday processing

Use one per-note action, Process note, for missing configured work. With a
recording only, it may transcribe and then enrich. With an existing transcript,
it may generate missing enrichment. It never replaces existing artifacts.

After failure, the same control may read Retry processing. Hide it when all
configured work is complete. If missing work cannot run because setup is absent,
show concise guidance linking to Settings; do not claim the missing artifact
exists or label the entire note fully processed.

Missing setup for one service does not reject a request that can perform work
with the other service. If nothing can run because required setup is missing,
explain what is missing and enqueue nothing.

Manual per-note processing works with automation off and never starts a pass
over other notes. Closing the browser does not cancel accepted work. Avoid
separate everyday Transcribe, Generate title, Generate summary, and retry
buttons.

### Deliberate replacement

Place replacement controls in a secondary More actions menu in note details:

* Transcribe again, when a saved transcript exists and transcription is configured
* Regenerate title and summary, when saved enrichment and a readable transcript
  exist and the LLM is configured

Transcribe again replaces only the transcript. It does not automatically
regenerate or invalidate existing enrichment. Regenerate title and summary
replaces both fields together, using the current saved transcript and settings.
Explain this behavior and possible service charges near these controls.

Keep a single saved transcript and a single saved enrichment result per note.
Do not add version history, stale-result tracking, dependency-version metadata,
or automatic invalidation. Previously generated content may remain based on
an older transcript.

Failed regeneration preserves the previous result. Automation does not retry
that replacement request: the saved enrichment still marks missing-work
processing complete. Another replacement requires an explicit request.

### Presentation and status

Show the title as the catalog's quick identifier, with recording date/time as
secondary information. Use recording date/time as the fallback when no title
exists; do not display empty title placeholders.

Show the summary as a paragraph in note details with the full transcript
available below. Preserve playback, recording facts, navigation, and deletion
behavior. Do not add a separate activity panel, queue screen, or generation
options dialog.

Keep durable artifact availability distinct from the active/latest attempt.
An error regenerating content must not hide a previous saved result.
Coordinator-wide waiting/setup guidance may identify the affected service.
Missing optional configuration is not itself a global processing error.

## Service configuration

Voice Notes Settings owns separate Transcription Service and LLM Service
sections. Each has an Azure resource endpoint, API key, and deployment name.
Do not make the LLM implicitly reuse transcription credentials or endpoint,
even when both deployments happen to use the same resource.

Preserve credential protection, blank-key retention, validation, safe
persistence, and runtime settings application. In-flight operations use their
accepted settings snapshot. Actual service-setting changes may clear that
service's setup block and ordinary backoff, but not its outstanding
service-directed cooldown. Toggle-only changes do not clear failure controls.

Support one explicit Azure contract now. Keep provider-specific transport
separate from generation instructions and result validation without introducing
a provider abstraction. Extract shared transport only for concrete reuse.
Future providers may be selected independently for transcription and enrichment;
OpenAI-compatible naming alone does not establish structured-output support.

Do not add a provider selector with unimplemented options, provider framework,
arbitrary headers, user-configurable API versions, prompt editor, or model
discovery in this increment.

## Device action and bindings

### Start processing action

Keep the `voice_note` action type and recording commands unchanged. Replace
`transcribe` with `process`, labeled Start processing:

```json
{
  "type": "voice_note",
  "command": "process"
}
```

Request a best-effort pass over missing configured work, including with
automation off. Accept no note ID, scenario, regeneration option, or extra
fields. Recording `start` continues to support only the General scenario.

Admission acknowledges initiation, not completed content. No eligible work
with all applicable results present is a successful no-op. Missing setup for
unfinished work must produce guidance, not a false completion claim, but must
not reject independent work that can run.

Offline, busy, recording, and resource rejection record no deferred request.
Repeated presses do not enqueue, extend, or parallelize a pass. A service
cooldown cannot be bypassed; independent eligible work in the other service
may still proceed.

### Binding contract

Keep `recording.*`, `last_saved.id`, and `transcription.*` keys and their
existing meanings. Do not use transcription bindings to report LLM activity.

Broaden `processing.state` to cover both operations, retaining `idle`,
`waiting`, `running`, and `blocked`. An active operation is `running`. With
automation off and no active manual work, the coordinator is `idle`.
Do not report a global block while independent work can proceed. Exact
service-aware reason reporting belongs in the handoff and remains best effort.
Do not reconstruct precise waiting/blocking reasons after exceptional
interactions or persist extra state for that purpose.

Add these keys to the existing `voice_note` scheme:

| Key                  | Meaning                                            |
|----------------------|----------------------------------------------------|
| `enrichment.state`   | Active/latest title-and-summary attempt state       |
| `enrichment.note_id` | Note associated with that attempt                  |
| `enrichment.error`   | Latest enrichment-attempt error                    |
| `transcription.retry_status` | Transcription service retry cycle, not per-note |
| `enrichment.retry_status` | Enrichment service retry cycle, not per-note |

Enrichment states are `idle`, `preparing`, `generating`, `saving`, `ready`,
and `error`. They describe the attempt, not durable availability or the whole
coordinator. `ready` means the result was safely saved. Clear the attempt's
error when the next enrichment operation is accepted.

All bindings read safe in-memory, since-boot snapshots, never storage or
network I/O. The latest enrichment note may differ from `last_saved.id`.
Do not add title/summary content bindings, arbitrary-note lookup, separate
title/summary status groups, or remaining-count bindings.

## Architecture boundaries and exclusions

Scheduling selects eligible work; each operation owns its request, response
validation, persistence, and cleanup. Extend existing owners and reuse shared
mechanisms without keeping a second transcription scheduler.

Future scenario-specific enrichment may use different schemas selected from
the saved scenario. One request now is not a requirement to combine every
future LLM task into one call. Split future work only for a concrete product
or resource need.

Explicitly excluded:

* To-do extraction, meeting notes, new scenarios, sharing, and cloud sync
* Generic job schemas, dependency registries, plugins, and configurable pipelines
* Per-note retry schedules, quarantine management, and durable error histories
* Retained unsaved results, retry-save workflows, and resumable requests
* Exactly-once remote execution, automatic transcript splitting, and translation
* Per-field regeneration, manual content editing, and configurable output styles
* Voice Assistant ownership or dependencies
* New feature flags or disabled-feature placeholder services

Use only `HAS_VOICE_NOTES` to isolate feature-exclusive code, settings, actions,
bindings, routes, and assets. Remote requests can complete before local failure;
a later retry may repeat charges. Do not claim exactly-once execution.

## Acceptance and validation

1. Recording and playback remain available offline and without either service.
   Capture adds no mandatory control, wait, or scenario selection.
2. One default-off automation switch discovers existing and newly saved missing
   results. Configuring a service alone does not enable automatic uploads.
3. Transcription-only configuration still transcribes. LLM-only configuration
   can enrich existing transcripts. Complete notes are not automatically redone.
4. Enrichment starts only from a successfully saved, readable transcript.
   Failed transcription or failed transcript persistence never unlocks it.
5. One LLM request returns a validated title and paragraph summary using strict
   structured output. Refusals, truncation, unsupported configuration, invalid
   fields, and resource failures are explicit failures without prose fallbacks.
6. Title and summary commit together. Failed generation or regeneration
   preserves the recording, transcript, and previous enrichment.
7. One worker owns processing across all initiation paths. Oldest-first passes
   attempt each operation at most once per visited note and recheck eligibility
   between transcription and enrichment.
8. Setup blocks and cooldowns apply to the affected service without blocking
   independent eligible work. Other shared/transient failures may end a pass;
   later automatic passes reconsider eligible work without tight retry loops.
   Explicit requests respect service cooldowns.
9. Recording interrupts conflicting work without a network-length wait.
   Results committed before interruption remain successful.
10. Reboot rediscovery relies on saved artifacts, not persisted workflow stages.
    Manual work does not resume automatically when automation is off.
11. The catalog uses a generated title or recording-time fallback. Details show
    the summary and transcript as untrusted text.
12. One Process note or Retry processing control handles missing work.
    Missing setup does not reject independent runnable work. If setup prevents
    all work, explain the issue and enqueue nothing. Replacement actions stay
    in details and replace only their named artifacts.
13. Retranscription leaves existing enrichment unchanged. Explicit regeneration
    uses the current transcript and replaces title and summary together.
    Failed regeneration retains the old result without an automatic
    replacement retry.
14. The `process` device command initiates missing-work passes without queued
    intent, targeting, regeneration, or recording-command changes.
15. Existing operation bindings retain their meaning. Enrichment bindings and
    coordinator status reflect safe snapshots without filesystem reads.
16. Separate LLM credentials remain protected. Logs identify note, operation,
    status, and retry decisions without exposing transcripts, generated content,
    credentials, audio, or unrestricted provider responses.
17. Feature-disabled builds exclude the new implementation, configuration,
    authoring metadata, routes, and feature-exclusive portal assets.
18. Directly dependent configuration names, action catalogs, schemas, fixtures,
    examples, and documentation use the new contract without compatibility paths.
19. Exceptional interactions may abandon work without coordinated recovery or
    exact status reconstruction. Unreadable artifacts produce an error when
    used, without an automatic repair subsystem.

Start implementation verification with existing focused checks and extend them
for concrete scheduling, structured-response, persistence, interruption, and
service-isolation risks. Select a single firmware verification board under the
repository's build policy when implementation requires a build.

Hardware recording-start latency, repeated-interruption memory headroom,
power-loss behavior, and live deployment compatibility remain unverified until
explicitly tested. The product owner owns subsequent hardware validation.
Documentation creation alone requires no firmware build or cloud request.
