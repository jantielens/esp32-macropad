---
title: Voice Notes transcription implementation handoff
description: Concrete contracts, bounded implementation phases, and validation gates for the transcription MVE.
---

## Status and authority

Approved implementation handoff, ready for a coding agent when implementation
is explicitly requested. Approval covers these contracts and scope; firmware
implementation is not yet authorized by this documentation update.
The [PRD](voice-notes-transcription-prd.md) owns product scope and acceptance.

Live Azure validation succeeded on 2026-11-15 using the private settings
and a supplied sample WAV. The configured resource, key, and deployment worked
with the [PRD's Azure deployment-style contract](voice-notes-transcription-prd.md#transcription-api-contract).
The multipart request returned HTTP 200 in
1.15 seconds with a JSON `text` string containing 44 characters. Neither the
key nor transcript was logged. The originally proposed v1 route with Bearer
authentication returned HTTP 404 with `DeploymentNotFound`; a separate v1
models-list request succeeded and included the configured model. That failure
did not establish an incorrect deployment name.

The implementation targets only the successful deployment-style contract.
Direct OpenAI and v1 fallbacks are deferred; no provider selection is needed.

The sample is valid 16-bit PCM, 48 kHz stereo, 5.46 seconds, and 1,048,402 bytes;
it was unchanged by validation. A device-format recording still needs testing.
No firmware build or device flash has been performed during planning. Do not
mark hardware behavior verified from source inspection.

## Non-negotiable implementation boundaries

* Deliver record, save, Transcribe, and read the saved result first.
* Keep Voice Notes independent of the Voice Assistant class, including its
  configuration, certificates, headers, assets, and initialization. Preserve
  mutual exclusion of the two features. Do not refactor or remove Voice Assistant.
* Keep one active transcription and one saved transcript per note. No queue,
  automatic retries, request history, result versions, or recovery framework.
* Recording takes priority. Reject transcription starts during a recording
  session, including pauses. A new recording may start during transcription;
  the latter abandons work at its next safe checkpoint.
* Disruptive interactions may require manual retry. Status after disruption is
  best effort. Preserve WAVs, resource safety, and truthful persistence results.
* Internal RAM is precious. Take every reasonable opportunity to place eligible
  allocations in PSRAM, avoid duplicate buffers, and release temporary data
  promptly. Internal allocations need a concrete safety or library reason;
  PSRAM-first applies to growth as well as initial allocation.
* Compile the complete feature out when `HAS_VOICE_NOTES=0`, including settings,
  actions, bindings, routes, and embedded web assets. Runtime hiding is not
  sufficient. Preserve unrelated shared infrastructure under its own gates.
* Do not add product-specific size, duration, transcript-length, or total-time
  caps. Provider limits, checked memory/storage usage, and finite connection and
  inactivity timeouts still apply. Do not split or transcode recordings.
* A safety check is preferable to a cancellation protocol. Reuse existing file
  reader leases and storage helpers rather than inventing ownership machinery.
* Future features justify function boundaries, not speculative frameworks.
* Stop for user input if a concrete blocker requires changing these contracts.
  Routine local types and helper names remain implementation details.

## Discovery findings

* [Recorder](../../src/app/voice_clips.cpp) already owns a catalog mutex, bounded
  open-reader slots, and a WAV publication lifecycle. Deletion already returns
  busy while a reader holds the note. Preserve this mechanism.
* [Indexed store](../../src/app/fs_indexed_store.h) supports WAV payloads using
  a metadata reader. Its index is rebuildable, not suitable as the only copy
  of transcript text.
* [Storage facade](../../src/app/storage.cpp) provides verified temporary writes
  and backup/replace behavior through `storage_write_atomic()`. Reuse it.
* [Voice Assistant](../../src/app/device_classes/voice_assistant/voice.cpp) uses
  a deployment-style Azure route, whole-WAV buffers, and small fixed response
  buffers. Its buffering and configuration ownership are not reused.
* [Component navigation](../../src/app/web_portal_component_api.cpp) enumerates
  category IDs. Registering a component in an unknown category is insufficient.
* [Current portal module](../../src/app/web/portal_voice_clips.js) already has
  lazy audio loading, seeking, download, date fallback, and playback cleanup
  helpers. Extend these rather than replacing working playback.
* [External data](../../src/app/external_data.cpp) demonstrates certificate-bundle
  verification and network timeout conventions. Its source registry and
  scheduling lifecycle must not become dependencies of Voice Notes.

## Concrete contracts

### Names and rename boundary

Use the following canonical names without legacy aliases:

| Surface             | Name                                  |
|---------------------|---------------------------------------|
| Feature gate        | `HAS_VOICE_NOTES`                      |
| Action type         | `voice_note`                          |
| Binding scheme      | `voice_note`                          |
| Navigation category | `voice-notes`, displayed as Voice Notes |
| Catalog fragment    | `voice-notes`, displayed as Notes       |
| Settings fragment   | `voice-notes-settings`                 |
| API component       | `voice-notes`                         |
| Feature script      | `/portal-voice-notes.js`               |
| Storage directory   | `/storage/voice-notes`                 |

Rename the owned recorder files and their symbols:

* `voice_clips.{h,cpp}` becomes `voice_notes.{h,cpp}`.
* `voice_clips_file.h` becomes `voice_notes_file.h`.
* `voice_clips_binding.{h,cpp}` becomes `voice_notes_binding.{h,cpp}`.
* `voice_clip_range.{h,cpp}` becomes `voice_note_range.{h,cpp}`.
* `actions/voice_clip_action.cpp` becomes `actions/voice_note_action.cpp`.
* `components/voice_clips_component.cpp` becomes
  `components/voice_notes_component.cpp`.
* `web/portal_voice_clips.js` becomes `web/portal_voice_notes.js`.
* `web/voice-clips.fragment.html` becomes `web/voice-notes.fragment.html`.
* Extend and rename `voice_wav_store.{h,cpp}` to
  `voice_notes_metadata.{h,cpp}` for WAV catalog metadata and transcript
  sidecar helpers.

Retain shared `voice_wav.{h,cpp}` and PCM WAV output for simplicity and provider
support, not compatibility with historical recordings. There is no requirement
to read old recording filenames, headers, catalogs, or metadata. Rename
directly affected test files/targets to match the feature; do not rename generic
indexed-store tests. Keep unrelated Voice Assistant symbols and settings intact.

Update gate references in the board defaults/override, ES8311 driver, startup,
shutdown/reboot/reset/OTA paths, action registry capacity, manifests, tests, and
asset tooling. Preserve the existing prerequisites and mutual-exclusion error.
Update firmware-size ownership/classification and regenerate the flag reference.
This is a rename of the existing capability, not a new independent transcription
flag. Do not change the board's hardware or advertised capabilities incidentally.

### Compile-time isolation

Use `#if HAS_VOICE_NOTES`, not `#ifdef` or runtime-only checks. Keep the default
off and preserve the intended board opt-in and existing prerequisite checks.
Do not introduce a separate transcription gate or depend on `HAS_AUDIO` alone.

The disabled build contract covers all of these surfaces:

* Wrap each feature-exclusive native implementation, including the recorder,
  transcription owner/request module, metadata helper, range helper, and binding
  module, so it emits no feature code, static data, or initializer when disabled.
  Keep feature-only dependencies inside the guard; include `board_config.h`
  before evaluating it.
* Guard startup/loop/shutdown hooks, task and buffer ownership, config types and
  `DeviceConfig` members, defaults, NVS load/save, config API fields, and secret
  indicators. Disabled builds allocate no feature settings or worker resources.
* Guard the action type/payload and union member, action manifest inclusion,
  registration, and registry capacity accounting. Do not advertise a disabled
  `voice_note` action or binding in portal/MCP catalogs, or add permissive stubs
  that accept an unavailable action.
* Guard the component aggregation, Notes/Settings registrations, navigation
  category entry, API handlers, feature-script handler declarations/definitions,
  and script route registration.
* Update the asset-generator gate mapping for `voice_notes`,
  `voice_notes_settings`, and `portal_voice_notes`. Both fragments and the script
  must be absent from preprocessed selected assets when disabled. Keep their
  controls, config-field registration, and implementation out of shared bundles;
  lazy loading alone does not exclude embedded bytes.

Do not gate truly shared WAV/audio, TLS, storage, or portal helpers solely on
Voice Notes, and do not redesign their dependency selection. Update existing
size-report ownership for the renamed/new feature modules and assets so their
bytes are attributed correctly. Small generic portal changes must contain no
Voice Notes-specific logic or data in always-loaded assets.

Extend the existing disabled-object and generated-asset checks in
[test_portal_asset_gates.js](../../tests/test_portal_asset_gates.js), rather than
adding a parallel guard harness. Cover the new native modules and both fragments,
plus enabled/disabled action/binding and navigation profiles using their current
test infrastructure. Check action payload/config exclusion as well as absence
from catalogs; runtime invisibility alone is not proof of compile-time isolation.
Use representative enabled and disabled host/preprocessor profiles, not an
all-board firmware build.

Do not migrate, import, alias, or delete the historical voice-clips directory.
The new catalog uses only the new directory. Existing data in the old directory
is untouched but is not advertised as part of the new feature. Leaving it alone
avoids unrelated destructive cleanup; it is not a compatibility obligation.

### Action and binding implementation

Implement the exact [device contract](voice-notes-transcription-prd.md#device-actions-and-bindings)
in the existing action and binding patterns. Start defaults to `general`.
Store the scenario as a session value; do not add a scenario picker or specialized
scenarios. Non-start actions reject a scenario field.

Add the scenario to the action payload and session/snapshot as needed. Preserve
the current recorder's action continuation and cancellation behavior.
Keep `last_saved.id` across subsequent Start operations within the same boot:
the existing Start clears the whole snapshot, which would violate the new
since-boot meaning. Clear errors on the next accepted domain operation.

Bindings read only in-memory snapshots. Recording and transcription use separate
state owners; no single combined state is introduced. Register complete
`BindingSchemeSpec`/documentation and action editor metadata through existing
mechanisms. Do not special-case the feature in central parse/dispatch ladders.

### Standalone settings

Add a small `VoiceNotesTranscriptionConfig` value type in the new
`voice_notes_transcription.h`. Include it as a feature-gated member of
`DeviceConfig`; runtime state remains outside `DeviceConfig`.

| Config API field       | NVS key       | String capacity including NUL | Default |
|------------------------|---------------|-------------------------------|---------|
| `voice_notes_base_url` | `vn_base_url` | 512                           | Empty   |
| `voice_notes_api_key`  | `vn_api_key`  | 256                           | Empty   |
| `voice_notes_model`    | `vn_model`    | 128                           | Empty   |

These are storage/input bounds, not recording or transcript limits. Reject
overlong values rather than truncating them. Use the existing config load/save
path and `/api/config`, not new component-specific config persistence.

The UI labels are Azure resource endpoint, API key, and Deployment name.
The existing chosen field names `base_url` and `model` mean resource root and
Azure deployment respectively, not provider selection.
Require HTTPS for nonempty endpoints, with an empty path or `/`, normalizing to
a trailing slash. Reject project/v1/deployment operation paths, embedded
credentials, query strings, fragments, whitespace/control characters, and full
transcription URLs. Reject CR/LF in key/deployment inputs. Construct the request
according to the PRD's service contract, encoding the deployment as one path
segment. Let the HTTP library parse the URL rather than extracting the
external-data subsystem's URL/parser framework.

GET returns an empty `voice_notes_api_key` and
`voice_notes_api_key_set`. Omitted or empty submitted keys retain the existing
secret; nonempty keys replace it. Do not add a key-clear workflow in this slice.
Empty base URL/model can leave setup incomplete without breaking recording.

Register non-secret fields with `registerConfigFields()` before loading config.
Handle the write-only key and configured indicator explicitly. Save through
`/api/config?no_reboot=1`. Apply new settings to future transcription attempts
without reboot; no live reconfiguration of an accepted operation.

After startup config load and after a successful settings save, publish a copied
config into the transcription owner under its mutex. Each accepted operation
captures its own settings snapshot. Never read a concurrently mutable
`DeviceConfig` string from the worker or log the key. Restore the feature's
candidate config if persistence fails, following neighboring config patterns.

No language setting is added initially; use service language detection.
No Voice Assistant setting or runtime module is reused or modified.

### Durable transcript representation

Keep the WAV catalog in `FsIndexedStore`, using the new directory and IDs.
Store one authoritative companion JSON file per transcribed note:

```json
{
  "transcript": {
    "text": "Remember to order replacement filters.",
    "model": "configured-model-or-deployment",
    "created_at": 0
  }
}
```

Paths are `<id>.wav`, `<id>.json`, and the existing `_index.json`. `created_at`
inside `transcript` is the completion timestamp, or zero when clock time is
unavailable; it is not the recording timestamp. Do not add empty summary,
scenario, version-history, sync, or sharing fields. The only scenario is
currently implicit `general` outside the active session.

Use `storage_write_atomic()` for JSON replacement, with checked PSRAM-first
serialization buffers and `StorageMutationGuard` admission. Never overwrite or
delete the WAV as a side effect of transcription. Keep the prior JSON until its
replacement is verified and promoted. Reuse the helper's `.tmp`/`.bak` lifecycle;
do not build a journal. If the primary JSON is absent but its backup exists,
allow that backup to supply the saved result and restore it before another
replacement. A small owned fallback is sufficient, not a general recovery pass.

The WAV index stays rebuildable from WAVs and does not contain transcript text.
In the feature's list projection, derive `has_transcript` from the sidecar or
backup's existence. Do not introduce a second indexed collection or modify
`FsIndexedStore` to coordinate audio and JSON. Fetch/parse text only for details.
A malformed or unreadable sidecar is reported explicitly by details, not shown
as an empty successful transcript.

Transcription holds the existing WAV reader lease until result publication and
cleanup finish. This already prevents note deletion while it is being used.
The catalog mutex is held only for short catalog/file operations, never the
network call. Delete removes associated sidecar/backup/temp files under the
normal storage guard before removing the WAV; propagate failures without
inventing rollback across files. Explicitly requested deletion may leave a WAV
without a transcript if a later deletion step fails. No orphan sweeper is needed.

### Memory placement and allocation

Apply PSRAM placement to response/transcript storage, JSON pools, serialization
and transfer buffers, and sizeable feature-owned config copies where practical.
Keep small control structures simple; do not introduce a custom allocation
framework or refactor unrelated modules.

Reuse existing allocators where their guarantees fit. The existing
`PsramJsonAllocator` can fall back to internal RAM and its reallocation does not
enforce PSRAM placement, so its name alone does not satisfy this contract.
For new variable-sized transcription documents, prefer the existing
[strict PSRAM allocator](../../src/app/bounded_psram_json_allocator.h), with a
memory-safety budget rather than a new product transcript-length cap. Preserve
PSRAM placement when growing response buffers. Allocation failure must be
explicit; do not silently fall back to large internal allocations.

Stream WAV bytes from storage. Avoid retaining response, parsed transcript,
serialized sidecar, and portal response copies simultaneously when the existing
interfaces permit reuse or earlier release. Do not put sizeable buffers,
transcript strings, or JSON pools on an internal task stack or in static internal
storage. Lazy-create optional resources and release per-attempt buffers on both
success and failure.

Keep DMA buffers, task control blocks, flash-accessing stacks, and genuinely
required library allocations internal. Follow
[task helper placement rules](../../src/app/rtos_task_utils.h); do not move a
flash-accessing task stack into PSRAM to improve heap measurements. Check library
defaults for avoidable internal copies, but do not modify global TLS allocation
policy or split the worker into multiple tasks merely to move its stack.
Preserve existing recorder behavior on no-PSRAM builds; unsupported transcription
allocations fail clearly rather than consuming unbounded internal RAM.

### Execution and transport

Add two focused sketch-root module pairs:

* `voice_notes_transcription.{h,cpp}` owns config snapshots, the single operation,
  state/error snapshots, and task scheduling.
* `voice_notes_transcription_request.{h,cpp}` builds the multipart upload and
  performs one file-transcription request. It does not own recording, portal
  navigation, retries, or persistent jobs.

Create one lazy, persistent worker on the first accepted request, using
`rtos_create_task_internal_stack()` with an initial 8192-byte stack and priority
1. It can access flash-backed storage, so its stack must not be in PSRAM under
the task helper contract. The 8192-byte size is a starting point, not a minimum:
measure stack high-water usage and right-size it with a safe margin.
Validate internal-heap headroom, including the persistent idle stack, on the
selected board. Task creation failure
must not affect recording. Use one pending slot and task notification, not a
queue; it is unavailable while processing. No task teardown framework is needed.

Protect fixed-size snapshots and config copies with a feature-owned mutex.
Never hold it over I/O or while taking the catalog mutex. Existing recorder
snapshot synchronization remains unchanged.

Admission checks are limited to busy, recording session active, OTA/transition
busy, configured settings, connected Wi-Fi, valid ID, and existing note.
Do not probe internet reachability first. Worker startup rechecks readiness
before opening the reader lease; races can fail the operation.

Use `HTTPClient` with a verified `WiFiClientSecure`, not raw HTTP parsing copied
from Voice Assistant. Use the built-in CA bundle on the current ESP32 core,
following the external-data example. Do not use `setInsecure()` or include the
Voice Assistant certificate. Reject redirects rather than forwarding credentials.

Implement the
[PRD service contract](voice-notes-transcription-prd.md#transcription-api-contract)
exactly, including its fixed API version, `api-key` authentication, deployment
path, multipart fields, and JSON response. Define the fixed version once in the
request module; do not add another setting or fallback/provider mode.

Use the HTTP library's `Stream` request overload with a small local adapter over
multipart prefix, open WAV, and suffix; compute Content-Length with checked
arithmetic. Read/upload in small chunks, not whole-WAV buffers. A local
PSRAM-first response sink may use checked growth and the library's decoded
response-body transfer so chunked and Content-Length responses are handled by
the library. These adapters solve current streaming requirements; do not turn
them into a general HTTP transport framework.

Use 15-second connect/handshake and I/O inactivity timeouts, following existing
network conventions. No fixed total request deadline or automatic retry.
Honor provider rejection of oversized input without imposing a smaller product
cap. Allocation, partial-read/write, JSON parse, and storage failures are explicit
errors. Missing/non-string `text` is invalid response; empty text is a no-speech
failure with manual retry, not a new durable state. Retain any prior transcript.

Check active recording and OTA activity at admission, upload/receive chunk
boundaries, and before publication. Capture `ota_activity_epoch()` at start and
abandon if it changes, even if OTA has since ended. Check again after blocking
library calls. Close owned HTTP/file resources on failure and release the reader
lease. Do not synchronously cancel another task or require immediate abort inside
a blocking library call. Keep network timeouts finite and recording unblocked.

Before metadata writes, acquire the storage mutation guard and retain it through
close/cleanup. Do not hold it for the entire upload. Use existing logging and
network-activity reporting conventions; never log bodies, audio, text, or keys.
After TCP cleanup, retain the project's short close-settle convention where
applicable. Long-running loops must yield.

### Portal API

Keep the existing component dispatch infrastructure under the renamed base
`/api/component/voice-notes/`. Reuse portal authentication and JSON error helpers.
IDs remain the existing 16-character lowercase hexadecimal format.

| Method   | Action                  | Result                                                 |
|----------|-------------------------|--------------------------------------------------------|
| GET      | `list`                  | Existing entries plus derived `has_transcript`          |
| GET      | `status`                | In-memory snapshots, setup status, storage information  |
| GET      | `file?id=<id>`           | Existing WAV playback/download with byte-range support |
| DELETE   | `delete?id=<id>`         | Existing deletion behavior plus companion cleanup      |
| GET      | `detail?id=<id>`         | Recording metadata and saved transcript, or null       |
| POST     | `transcribe?id=<id>`     | Accept one background attempt; no request body needed  |

Status shape:

```json
{
  "recording": {
    "state": "idle",
    "duration_ms": 0,
    "scenario": null,
    "error": ""
  },
  "last_saved": {"id": ""},
  "transcription": {"state": "idle", "note_id": "", "error": ""},
  "transcription_configured": false,
  "backend": "sd_spi",
  "total_bytes": 0,
  "free_bytes": 0
}
```

Details shape:

```json
{
  "note": {
    "id": "0000000100000002",
    "created_at": 0,
    "duration_ms": 3000,
    "size_bytes": 96044,
    "sample_rate": 16000,
    "channels": 1
  },
  "transcript": null
}
```

When present, `transcript` has the same object shape as the companion JSON.
An unavailable recording timestamp is zero or absent, matching the catalog's
existing convention; the UI must not invent a date.

POST success returns HTTP 202 with
`{"success":true,"note_id":"...","state":"preparing"}`. This acknowledges
acceptance, not remote execution or persistence. Admission failures use 400
for invalid ID/setup, 404 for missing note, 409 for busy/recording/OTA conflict,
and 503 for disconnected Wi-Fi or unavailable resources. Worker failures appear
in the status snapshot, not as a delayed response to POST. Other existing file
errors retain their meaningful status codes.

No SSE, WebSocket, status history endpoint, provider-test endpoint, or new API
framework is needed. Derive per-note display by matching `transcription.note_id`;
never apply the latest attempt status to every catalog entry.

### Portal composition

Add the guarded `voice-notes` category to the existing category table, positioned
immediately before Audio. Its existing `icon` metadata uses `voice`, the shared
icon map's microphone alias. Let generic `portalCategoryIcon` accept that optional
alias from `cat.icon`, resolve it only through the existing icon map, and retain
the category-based fallback for unrecognized values. This preserves current
emoji-metadata behavior without a Voice Notes entry or branch in shared JS.
Do not change board landing configuration, recovery-AP filtering, or global
navigation behavior. Notes is the category's first/default destination.

The renamed main component supplies Notes and the API actions. Add a nav-only
Settings component in the same owned component compilation unit, with the same
feature script. Include that unit through `portal_components.cpp`.
Do not add details to the navigation registry.

Use two fragments: `voice-notes.fragment.html` and the new
`voice-notes-settings.fragment.html`. Catalog and details are two panels within
the Notes fragment, controlled by the feature module. Back to Notes switches
panels. Do not add global hash-routing/deep-link infrastructure for details.

Reuse one playback builder for catalog and details. Release audio on panel
switch/fragment leave. Keep existing download, seek, one-playing-recording,
unknown-date, and failure behavior. Render transcripts with `textContent` and
preserved line breaks; never use HTML injection.

While Notes/details is mounted, poll status every two seconds, serially, with
no overlapping polls. Dispose the timer and ignore late responses on
`portal-fragment-leave`. Polling stops with the view, not the device operation.
Update status/action nodes in place; do not rebuild active audio controls on
each poll. Fetch the transcript after a matching successful attempt or when
details opens. Keep Refresh for catalog refresh; do not add catalog watchers.

Keep full transcript text out of list/status responses. Use current Bootstrap
utilities and fragment conventions rather than a new design system.
No placeholder summary/sharing controls or new device UI are required.

## Implementation phases and stop gates

### Phase 0 Service contract checkpoint completed

The successful local Azure request recorded above completes provider selection
and request-shape discovery. Do not repeat discovery, add fallbacks, or make
another live request merely to start implementation.

Device-format WAV and ESP32 transport checks remain in Phase 4. Any additional
live check needs applicable approval and explicitly selected non-sensitive audio.
Private settings stay outside the repository. Expected validation variable names
are `VOICE_NOTES_BASE_URL`, `VOICE_NOTES_API_KEY`, and `VOICE_NOTES_MODEL`; the base
URL for the approved contract is the resource root, not the earlier v1 test path.
Do not modify private inputs automatically, shell-source the file, print its
contents, or put secrets in command arguments or committed files.

Report HTTP status, WAV format/size, whether `text` is a string/nonempty, and
transcript length, not transcript contents. Keep the WAV unchanged and live
validation separate from CI, unit tests, and fixtures. Local service success
does not verify ESP32 execution.

### Phase 1 Establish names and durable results

Implement owned renames, `HAS_VOICE_NOTES`, action/scenario parsing and metadata,
recorder binding renames, and the companion storage operations. Preserve WAV
capture/playback and current shutdown safety. Keep the build compiling between
phases; add transcription snapshot bindings when the owner exists in Phase 2,
not placeholder implementations that claim processing works.

Update startup, board override, reboot/OTA/config-reset call sites, test
definitions/stubs, feature-size ownership, and directly dependent fixtures.
Establish enabled/disabled compile-time exclusion checks during this phase,
extending them as later phases add settings, transport, and portal assets.
Extend the renamed recorder tests for companion replacement, reader-protected
deletion, surviving index rebuild, default/invalid scenario, and last-saved
identity across a new recording. Reuse current fake storage and failure hooks.

Exit gate: existing recording tests pass under new names; WAVs remain unchanged
by transcript writes; failed replacement preserves the previous saved result.
No work queue or indexed-store refactor has been introduced.

### Phase 2 Implement settings and one transcription operation

Add the two module pairs, config/NVS/API round trip, startup configuration,
safe copied settings updates, worker, transport, snapshots, and the new
transcription bindings. Extend the component with status/details/start handlers.
Keep handlers thin. No new device transcription action.

Extend the existing recorder test harness with a link-time fake of the focused
request function where practical to exercise orchestration without real TLS.
Cover acceptance/busy, success, failure preserving the prior result, and safe
checkpoint abandonment. A focused request-format/response test file is justified
only where existing tests cannot exercise the new standalone multipart and
stream boundaries; register it normally and reuse existing host doubles.
Do not construct a miniature ESP32 network platform for unit coverage.

Exit gate: the operation works independently of the portal, reports stages
through snapshots, and persists exactly one transcript. Configuration and
automated tests contain no live service calls or secrets.

### Phase 3 Deliver the portal experience

Implement category/components/fragments, settings registration and secret state,
catalog metadata/actions, details, transcript rendering, and disposable status
polling. Update lazy script routes/declarations and asset gates. Do not edit
generated `web_assets.h` manually.

Extend the renamed portal JS test, asset-gate checks, navigation/component
capacity checks, and portal-dev-server fixtures. Simulate transcription
deterministically in memory; no cloud dependency in the development server.
Avoid recreating audio controls during polling.

Exit gate: desktop/mobile Notes and Settings are usable; details works before
and after transcription; closing the browser does not own/cancel processing.
Playback, seeking, download, deletion, and recording remain intact.
Disabled profiles contain neither fragment, feature script, routes, navigation,
nor feature-specific additions to the shared portal bundle.

### Phase 4 Verify and reconcile documentation

Run focused checks, one selected-board build, and the manual device matrix.
Reconcile source metadata and generated binding docs before the handoff.
Do not claim device behavior from mocks or the local service request.

Update the existing authoritative owners rather than creating another manual:

* [User audio topic](../user/media/audio.md): Voice Notes navigation, recording,
  setup, manual transcription, replacement/retry, and useful binding examples
* [User action topic](../user/pads/actions.md): affected action names/examples
* [Board README](../../src/boards/reterminal-sticky-interactive/README.md):
  affected links and hardware-specific facts only
* [Audio architecture](audio-architecture.md): standalone recorder/transcription
  ownership and unchanged microphone path
* [Portal architecture/API](web-portal.md) and [root README](../../README.md):
  affected API/navigation/gate references only
* [Flag reference](../compile-time-flags.md), size-report ownership, and
  `tools/mock-data/binding-docs.json`: reconcile with final source metadata

Update affected user index labels/anchors if the audio topic heading changes.
Do not add unrelated Voice Assistant cleanup, a tool catalog, or a new user
topic without approval.

## Validation commands

These are implementation commands, not checks already run. Use the renamed test
targets and register new tests in the normal suite. A shell/source spelling
check does not substitute for observable behavior.

```bash
cmake -S . -B build/host-tests
cmake --build build/host-tests --parallel --target \
  test_voice_notes test_voice_notes_sd test_voice_note_range \
  test_voice_wav test_fs_indexed_store test_action_parse \
  test_binding_schema_full test_binding_schema_full_no_psram \
  test_web_portal_path test_reboot_service_voice_notes \
  test_sticky_hardware_voice_notes
ctest --test-dir build/host-tests --output-on-failure \
  -R '^(test_voice_notes(_sd|_no_psram)?|test_voice_note_range|test_action_parse|test_binding_schema_(full|full_no_psram)|test_web_portal_path|test_reboot_service_voice_notes|test_sticky_hardware_voice_notes)$'
build/host-tests/tests/test_voice_wav
build/host-tests/tests/test_fs_indexed_store
bash tests/test_action_catalog_completeness.sh
bash tests/test_mcp_scheme_parity.sh
python3 tests/test_firmware_size_report.py
python3 tests/test_board_feature_overrides.py
python3 tests/test_config_identifier_guards.py
node --check src/app/web/portal_voice_notes.js
node tests/test_portal_voice_notes.js
node tests/test_portal_asset_gates.js
python3 tests/test_portal_dev_server.py
python3 tests/test_portal_component_capacity_matrix.py
```

Enable the Voice Notes gate in an existing action-test profile and add its
normal recorder/transcription stubs so action tests actually exercise the new
type. Retain a disabled profile that rejects/omits the action and binding and
excludes their payload/config storage. Do not rely on the current profiles,
which omit that gate, to prove enabled behavior.
Extend the existing asset test's disabled native-object and preprocessed asset
checks for the complete feature, and verify enabled assets/registration once.
Build/run any focused request test added in Phase 2 through CMake/CTest as well.

Regenerate metadata after the source contracts stabilize:

```bash
python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md
build/host-tests/tests/test_binding_schema_full --emit-json > tools/mock-data/binding-docs.json
```

Preview production assets with deterministic fixtures:

```bash
python3 tools/portal-dev-server.py --port 8765
```

Open `http://localhost:8765/?profile=reterminal-sticky-interactive&fragment=voice-notes`.
Check desktop and mobile layouts, a long transcript, empty catalog, unknown
date, missing setup, success/error/retry, and navigation away during polling.

Before firmware verification, read the current board-preference file as required
by repository instructions. The user selected `reterminal-sticky-interactive`,
which matches the existing preference. Build only that board:

```bash
./build.sh reterminal-sticky-interactive
```

This generates selected web assets as part of the normal build. Do not run bare
`./build.sh`, expand to all boards, flash a device, or invoke a live service
without the applicable approval.

### Focused manual device checks

* Record/save/play offline; capture remains Start recording and Save recording.
* Configure Azure and transcribe a short representative note.
* Close/reopen the portal while processing; then reboot after success and read
  the saved text.
* Transcribe again with a changed model/deployment; verify replacement on success
  and preservation on failure.
* Exercise one representative connection failure, storage-write failure, and
  recording/OTA checkpoint conflict. WAVs remain usable; manual retry is enough.
* Record internal free heap, largest free internal block, and minimum free
  internal heap before first use, during TLS/upload/parse/save, and after repeated
  success/failure attempts. Measure PSRAM usage and worker stack high-water
  headroom; account for the persistent worker's idle allocation. Investigate
  retained growth and justify each material new internal allocation.
* Confirm response/JSON buffer growth stays in PSRAM, no whole-WAV allocation
  occurs, and allocation failure preserves recording and the prior transcript.
  Use focused existing failure hooks or temporary diagnostics, not a new
  permanent memory-monitoring subsystem or arbitrary product limits.
* Verify serial diagnostics explain stages without secrets or transcript text.

Do not expand this into exhaustive combinations of user disruptions.
Index-rebuild and replacement-failure contracts belong primarily in focused
host storage tests, not repeated destructive testing on a user's recordings.

## Handoff completion

The product/provider decisions are approved and the local service checkpoint is
complete. There are no remaining product decisions for the implementation agent.
Stack sizing, memory headroom, disabled-feature exclusion, and hardware behavior
are measured implementation gates, not new configuration or architecture choices.
Stop for user input only if a concrete blocker requires changing a contract.

Record which commands and device checks actually passed, which were not run,
and any concrete blocker. Do not label mock/source checks as hardware validation.
This handoff is ready for implementation when explicitly requested; no firmware
changes, commit, push, or deployment are authorized by this documentation update.
