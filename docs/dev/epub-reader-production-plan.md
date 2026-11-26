---
title: E-reader MVE Scope and Handoff Plan
description: A happy-path E-reader using existing widgets, actions, bindings, and storage, with explicit naming, disposable caches, and safe resets.
---

The user approved this MVE plan on 2026-11-25. Later approved decisions about
background chapter metadata, chapter actions, labels, and status are reflected
below. Work is paused after implementation of Increment 2; device acceptance
remains outstanding.

## Pause status - 2026-11-26

The WIP is based on `release/1.51.0`. Increments 1 and 2 are implemented but
not ready for release. Increment 3 has not begun. FreeInkBook remains a
candidate, not an unconditional production decision.

Implemented work includes:

* The E-reader widget, shared session and worker, typography, page and chapter
  navigation, disposable chapter cache, and per-book resume.
* E-book upload, listing and deletion; typed reader and physical-button
  actions; scalar status bindings; and book and chapter List sources.
* Background chapter metadata loading without a visible reader, using the
  existing worker and generation, OTA, and storage ownership checks. Chapter
  selection uses the current book; chapter List item provenance protects
  against stale book selections.
* A 253-item List snapshot limit, retaining the existing 20-visible-row and
  20-recent-note limits. The Binding Reference groups item fields by their
  registered List source instead of hard-coding E-reader fields.
* Portal fixtures, user and developer documentation, runtime metadata,
  generated binding documentation, and focused host checks.

The user reported that the Increment 1 device gate passed, including reading,
navigation boundaries, lifecycle and admission, resume, invalid-cache and
position reset, illustrated content, and a long chapter. The Increment 2 device
gate is not confirmed. The user flashed an older build and saw the old static
E-reader-only Binding Reference; the corrected build below has not been
confirmed on the device.

Latest validation evidence:

* `./build.sh reterminal-sticky-interactive-sd` passed on 2026-11-25 after the
  Binding Reference fix. Application binary SHA-256:
  `26d49e30404da5e4bad8ca2b6243cc7786a9a001e64097bd302ae96b68f9f172`;
  ELF SHA-256: `5200b36f7c684aa28155a21bd337f34a774e33260c607935f636377588091296`.
  The build reported 3,901,302 bytes program storage and 97,140 bytes global
  dynamic memory. The old static reference was absent from the ELF and the
  generated portal bundle contained the updated reference.
* `ctest --test-dir build/host-tests --output-on-failure -R '^(test_list_source|test_binding_schema_full|test_binding_docs_fixture|test_runtime_variable_actions_display|test_epub_checkpoints|test_ereader_library|test_ota_activity)$'`
  passed 7/7. `python3 -m unittest tests.test_portal_dev_server` passed 50/50.
* `node tests/test_portal_binding_help.js`, `node tests/test_portal_action_picker.js`,
  `bash tests/test_action_catalog_completeness.sh`,
  `bash tests/test_mcp_scheme_parity.sh`, `python3 tests/test_firmware_size_report.py`
  (30 tests), `bash tests/test_portal_asset_config.sh`, and `git diff --check`
  passed. The portal tests intentionally log errors while exercising failure paths.
  The full staged `git diff --cached --check` reports trailing whitespace and
  an EOF blank line in vendored miniz and tjpgd files; the non-vendored staged
  changes pass. The upstream files were left unchanged.
* `node tests/test_portal_asset_gates.js` previously failed on
  `reterminal-sticky-interactive/extension_tick_interval_ms`. The board enables
  `HAS_NATIVE_EXTENSIONS` while the test expects the marker absent; this was
  not attributed to the E-reader changes.
* An earlier `test_web_portal_path` build failed due to missing
  `PORTAL_ASSET_BUILD_ID`, `TAG`, and `LOGE` in generated test code. This was
  not resolved as part of the E-reader work.

One earlier reading run sampled minimums of 58,280 bytes internal free,
31,732 bytes largest internal block, 816,672 bytes PSRAM free, and 5,272 bytes
worker task-lifetime stack margin. These are samples, not guaranteed peaks.
Two earlier watchdog resets remain unexplained; cooperative checkpoints were
changed and the user reported a successful upload/reopen retest. A stalled
SD/SPI operation has not been ruled out. Missing glyphs remain a nonfatal
warning and globally merged stylesheets limit rendering fidelity.

When work resumes:

1. Flash only with explicit permission, then hard-refresh the portal and verify
   the corrected Item Binding Reference lists fields under each parent source.
2. Complete the Increment 2 device gate: upload/list/delete, book selection,
   background chapter loading and errors, chapter selection before displaying
   the reader, physical controls, and a representative authored pad. Verify
   busy/admission, Open/delete and OTA interactions, hidden target precedence,
   and a collection larger than 20 without changing existing List limits.
3. Run Increment 3 safety, stress, memory, and performance checks only when
   separately approved. Reassess the known test failures and the candidate
   engine before release. Recreate saved Open chapter actions that still
   specify `book`; there is no migration.

Do not flash, push, delete device data, or begin Increment 3 without explicit
permission.

## Governing rule

Deliver useful reading through existing building blocks. On unexpected input,
incompatible state, or resource/storage failure, report the problem, release
owned resources, and stop or reset the reader. Losing reader state, progress,
or disposable caches is acceptable.

Crashing the device, corrupting unrelated files, leaking ownership/resources,
or bypassing hardware safety is not acceptable. Do not build recovery,
migration, repair, or fallback frameworks to preserve reader progress.

## Evidence and candidate status

Use the [spike results](epub-reader-spike-results.md) as the historical
evidence baseline. The pause status above records subsequent evidence:

* The corrected phase-2 Sticky SD build succeeded and focused host checks passed.
* The user confirmed Alice text and images after the CSS correction. Logs showed
  some page turns and chapter transitions.
* Corrected-build performance, reader-specific memory peaks, reboot resume/cache
  reuse, large chapters, full navigation boundaries, and device lifecycle stress
  are not yet demonstrated.
* Reported phase-2 timings preceded the CSS correction and exclude physical
  refresh. Boot-wide heap minima are not reader-only allocation measurements.
* Complex CSS remains approximate; stylesheets are merged globally. Some
  unsupported content can be omitted. Interlaced PNG is unsupported.
* Focused host results do not establish a clean complete host suite or physical
  rendering, and reader tests do not establish whole-board/panel qualification.

FreeInkBook remains a candidate, not an approved production decision.
Its [revision record](../../libraries/FreeInkBook/REVISION)
pins upstream revision `f7fa9b6871192ea27d8aa37a03beaa749462c3ab` and records
local changes. Reuse the current integration if focused qualification supports
it. Reopen selection if it cannot meet the safe-failure contract without
disproportionate work; do not add a second rendering engine.

## MVE features

### Reading

* Render DRM-free reflowable prose, supported PNG/JPEG illustrations, and the
  engine's basic formatting/CSS subset.
* Target Latin-script, left-to-right text and required accented characters.
* Keep the reader inside a pad widget. Configure font and font size through
  its Text style field and existing font store. Reuse the button's existing
  Padding control and device defaults; do not add reader-specific margin knobs.
* Use one shared active reading session without reader IDs.
* Require an open-book action to select the book. Do not add a default-book field
  to the widget or persist the selected book across reboot.
* Allow reader widgets on different pads, but only one visible reader at a time.
  A second visible widget reports an error without disturbing the first.
* Support previous/next page and normal chapter transitions, with explicit book
  boundaries and no wrapping.
* Provide previous/next chapter as the quick-jump option. Land on the destination
  chapter's first readable page; use existing empty-chapter traversal.
* Restore per-book progress when reopening an unchanged book. A missing, changed,
  corrupt, or incompatible position may restart reading.

Chapter skipping uses the EPUB reading order already needed for page navigation.
It requires neither whole-book indexing nor full EPUB TOC interpretation.
An uncached destination still needs layout; quick navigation is not a guarantee
of instant rendering.

### Library and controls

* Use `/books` and filename-based book IDs, including `.epub`, up to 63 UTF-8
  bytes. Allow spaces and accented names; reject path separators, control
  characters, and invalid filenames. Avoid recursive discovery, cover extraction,
  and parsing every book for metadata.
* Expose books and the current book's flat reading-order chapter list through
  collection bindings consumed by ordinary List widgets.
* Use available chapter labels, with a chapter-number or resource-name fallback.
  A flat chapter list is not advertised as a complete publisher TOC.
* Provide reader actions for opening a book, previous/next page, previous/next
  chapter, and selecting a chapter from a List item.
* Expose a small status-binding set for ordinary pad labels: state/error, current
  book, chapter, and current-chapter page information.
* Add the registered E-book Library portal fragment for upload, list, and delete,
  using existing authentication, component, file-management, and upload patterns.
* Keep the portal file-management-only. Book selection happens through pad
  actions/Lists; do not add portal Open, reader control, or session-selection APIs.
* Assign physical buttons to reader actions through existing hardware-button
  settings. Preserve global assignments, recovery, and power controls.

Library/chapter selection uses pad actions and touch Lists. Hardware-only List row
selection is deferred: existing button navigation excludes Lists, while existing
List commands only page the viewport.

Load complete ordinary book/chapter collections, not a 20-item subset or window.
Use ordinary List scrolling/paging to present the loaded collection.
The focused shared List capacity change is approved; do not introduce reader
paging actions, virtualization, or an unlimited collection service.

Separate snapshot capacity from the existing 20-visible-row setting and the
20-recent-note provider policy. Preserve both existing policies. Keep PSRAM
allocation bounds and validate counts before narrowing to widget types.
With the current 8-bit row representation, 253 data items plus two navigation
controls fit safely. Use that structural bound rather than widening the whole
widget for hypothetical large libraries. Allocation limits can be reached sooner.
If the complete collection does not fit, fail visibly rather than publish a
partial collection as complete.

### Reuse boundaries

* The widget owns display objects and presents the shared session.
* A small reader service owns book state and commands; reuse the existing worker
  and generation/cancellation mechanisms instead of a session framework.
* Typed actions are the control interface, including List item activation and
  physical buttons.
* Bindings publish status and collection snapshots. They do not scan storage,
  parse EPUBs, or perform layout while resolving.
* Ordinary Lists, buttons, labels, and existing pad navigation provide the UX.
  Do not add built-in library dialogs or reader-specific navigation screens.
* Existing Storage, font store, OTA guards, manifests, and component registration
  provide infrastructure. Add reader-owned descriptors/providers where needed,
  not parallel registries or orchestration ladders.

## Scope intentionally deferred

* Whole-book percentage/progress indexing and percentage jumps
* Full NCX/EPUB navigation interpretation, hierarchical TOCs, and fragment jumps
* Internal links, footnote return, bookmarks, search, and annotations
* Full-screen reading, simultaneous readers, and on-device typography controls
* Automatic restoration of the last library selection across reboot as a
  separate feature; explicit open-book actions remain sufficient
* Multi-book cache retention, eviction ranking, cache repair, and migrations
* Exact physical-presentation progress tracking requiring new display APIs
* New adaptive decoder machinery, compatibility certification, or board tuning

## Approved public naming and authoring contract

Use E-reader for the feature and widget, E-book Library for the portal section,
and EPUB for the supported file format. Help text says:
`Supports DRM-free, reflowable EPUB files.`
The broader feature name does not promise other formats.

* Capability flag: `HAS_EREADER`
* Widget type and selector label: `ereader`, `E-reader`
* Action type/group: `ereader`, `E-reader`
* Binding scheme: `ereader`
* Portal component/fragment: `ebook-library`, `E-book Library`
* Portal navigation category: `device`
* Books directory: `/books`

Rename the current `epub_reader` widget and `HAS_EPUB_READER` integration
consistently during implementation. Do not add aliases, compatibility fields,
or migration logic. Historical spike references and upstream SDK names remain
unchanged. Follow flag-report/size ownership and widget-manifest registration
requirements when renaming, including related generated metadata and tests.

### Typed operations

Register one reader action module through the existing built-in action manifest.
Use these operation IDs:

* `ereader.open`, labelled `Open E-book`
* `ereader.navigate`, labelled `Navigate E-reader`
* `ereader.open_chapter`, labelled `Open chapter`

`ereader.open` takes a required `book` filename. It addresses `/books/<book>`,
not an absolute path or an opaque catalog ID. The same filename is the book's
List item ID. Copy resolved arguments before asynchronous work.

`ereader.navigate` takes a required `command` enum:

* `previous_page`, labelled `Previous page`
* `next_page`, labelled `Next page`
* `previous_chapter`, labelled `Previous chapter`
* `next_chapter`, labelled `Next chapter`

`ereader.open_chapter` takes a positive, one-based `chapter` number and uses
the current session book. The chapter must be in its reading-order range.
Reject a stale chapter List selection using its source book provenance; do not
implicitly switch books. List actions supply the chapter item field.
Once chapter metadata is known, chapter selection may update the lightweight
session while its widget is hidden; the next reader view loads that target.
If metadata is unavailable, reject the selection rather than parse the book
synchronously or add an action continuation.

Operations complete when a request is accepted, not when layout or physical
refresh completes. The worker publishes loading/ready/error state. Rejected
or busy requests fail explicitly, without continuations or retry queues.
Reject new open/navigation/chapter requests while loading or finishing owned
cleanup. A caller retries; do not cancel/restart loading for every repeated tap.
Existing pad hiding/OTA cancellation still invalidates work safely.
Reserve the single in-flight command slot atomically at request acceptance,
before the worker wakes. Do not rely on a later `loading` publication to prevent
another command from overwriting the first. A hidden-view selection remains
`idle`; its short state update is still serialized.

At a start/end boundary, navigation succeeds as a no-op with `Start of book`
or `End of book` status. Do not wrap, enter error state, or re-render the same
page solely to process the no-op. Both chapter-skip directions land on the first
readable page; ordinary previous-page transitions retain last-page behavior.

Opening the same book when already ready preserves the current page as a no-op.
After an error, Open retries through the normal open/cache/reset path. Do not
add a Retry operation or automatic replay.

### Collections and item fields

Register collection descriptors with the existing List source registry; do not
serialize collections through a scalar binding resolver.

`[ereader:books]`, labelled `E-books`, contains:

* `id`: the exact filename, including extension
* `title`: a display label derived from the filename, not extracted book metadata
* `size_bytes`: file size in bytes

List activation calls `ereader.open` with `book` set to
`{"$template":"[item:id]"}`. A following existing `screen.open` action may open
the reader pad. Do not combine pad navigation with the reader action itself.

`[ereader:chapters]`, labelled `E-book chapters`, contains:

* `id`: the one-based chapter number as a string
* `title`: available label, otherwise `Chapter N`
* `chapter`: the one-based chapter number as a number
* `book`: the filename identifying the loaded book

List activation calls `ereader.open_chapter` with
`chapter: {"$template":"[item:chapter]"}`.
Use reading order, not document-fragment targets. A unique non-fragment
navigation entry matching a reading-order resource may supply its title.
Until metadata is ready, publish an empty List; expose loading and failures
through `[ereader:chapter_state]` and `[ereader:chapter_error|]`. Never reuse
another book's chapter collection. The shared List remains generic.

### Widget and session UX

The widget contains typography, not a book selector/default or margin settings.
The only new persisted widget authoring field is `widget_ereader_style`:

* Label: `Text style`
* Default: empty, inheriting the device's effective font and font size
* Maximum: the existing 127-byte style payload limit
* Supported literal keys: `font` and `font_size`, with existing typography syntax
* Example: `font:my-font;font_size:24`
* Unsupported keys or dynamic styles are rejected by normal field validation

Reuse the existing style editor/help machinery and font store; do not create
a new style parser or font catalog. No `widget_ereader_book`, default-path, or
reader-specific margin fields are present.

Use the button's effective `content_pad` (labelled `Padding` under
`Borders & Geometry`) and its inherited device defaults. Compute the reading
area from the existing content rectangle; do not apply another global SDK page
margin on top. The book's supported own CSS remains best effort.
Use ordinary pad labels and scalar bindings for status, warnings, and progress
instead of reserving a reader status line. Fatal errors must remain visible
inside the reader even when no error label is configured. Do not add a toolbar,
permanent progress line, or reader navigation controls.
For a missing/unloadable configured font, reuse the existing built-in fallback
with a warning. If that cannot initialize safely, fail visibly; do not add
fallback chains. If button padding, geometry, or typography leaves insufficient
reading space, display `Reader needs more space`. Do not auto-shrink fonts,
override padding, or add adaptive layout machinery.

With no selected book, it displays `No E-book selected`.
Opening a book selects it for the running session and starts background
chapter metadata loading even without a visible reader. Page layout starts
when a reader view is available; `ereader.navigate` requires a visible, ready
reader.

Showing a different reader pad uses the same session selection and that widget's
typography. It must not select a different book. A second visible widget fails
explicitly. Reboot clears session selection; an open action reselects a book and
resumes it when the saved position is valid.

When hidden, cancel/release rendering work and SDK arenas. Retain the selected
filename, lightweight logical progress, and the compact available chapter
metadata already needed by the chapter collection. Do not retain whole-book
layout/decoder buffers just to keep selection alive.
Changing books invalidates old chapter metadata. No automatic last-book
selection is restored after reboot.

Keep left/right touch page turning. The widget owns touch input, so external
actions belong on separate buttons. Physical-button controls invoke the same
reader operations. No per-reader targets or special hardware mappings.

Use one-based chapter/page numbers in user-facing labels, bindings, and actions.
Page counts are for the current chapter, not an invented whole-book total.
Warnings may accompany a readable page; errors stop/reset reader work.
Unknown/unavailable numeric values are unavailable, not fake zero counts.

### State and scalar binding contract

`[ereader:state]` always resolves on an available compiled reader and has four
values:

* `idle`: no active reader view, including no selected book
* `loading`: preparing a page
* `ready`: a usable page is prepared, not proof of physical panel completion
* `error`: reader work failed

The selected filename is separate from state. Hiding the view publishes `idle`
without clearing its lightweight selection. A new request clears prior
request-specific errors/warnings. Invalid action arguments or a busy rejection
are reported through the existing action error path; they do not turn a valid
current page into reader `error` state or overwrite its position.

Scalar keys and availability are:

* `[ereader:book]`: selected filename, unavailable when no book is selected
* `[ereader:chapter]`: current logical one-based chapter when known
* `[ereader:chapter_count]`: selected book's known reading-order entry count
* `[ereader:page]`: one-based current-chapter page when valid for the active layout
* `[ereader:page_count]`: that chapter's known page count for the active layout
* `[ereader:error]`: current reader failure text, unavailable when none
* `[ereader:warning]`: current nonfatal warning/notice, unavailable when none
* `[ereader:chapter_state]`: metadata state, `idle`, `loading`, `ready`, or `error`
* `[ereader:chapter_error]`: chapter metadata failure text, unavailable when none

Book/chapter metadata can remain available while hidden; layout-specific page
values are unavailable when not valid. Do not substitute zero for unknown
counts. Empty reading-order resources may be skipped without pretending the
spine count is a precomputed count of readable chapters.

Use existing fallbacks, for example `[ereader:error|]`,
`[ereader:warning|]`, and
`Chapter [ereader:chapter|?], page [ereader:page|?]`.
Keep complete scheme docs and authoring metadata beside registration; do not
create a separate manually maintained binding catalog.

### Portal UX and API contract

Expose `Device > E-book Library` with a single-file picker, `Upload` button,
filename/size list, and per-file `Delete`. Reuse upload progress/error and
confirmation patterns; show the exact filename in the deletion confirmation.
There is no Open/Select button, reader status/control panel, batch/resumable
upload, or new settings page.

* Reject duplicate filenames with a conflict. The user deletes before replacing.
* Reject deletion while the book is visible/in use or owned cleanup is pending.
  When hidden and file/rendering resources are released, deletion is allowed.
  A later open of a missing file fails/resets normally; do not coordinate a
  live session replacement or add a Close action for file management.
* Upload checks filename, `.epub` extension, framing, and completed storage.
  Do not parse book metadata, decode images, or lay out chapters during upload.
* Success means the file was stored. Help text distinguishes storage success
  from compatibility, which is checked when an Open action starts reading.
* List all ordinary `/books` EPUB files using the same book metadata/service
  as the book collection; do not independently implement catalog discovery.

Register these authenticated component actions:

* `GET /api/component/ebook-library/list`
* `POST /api/component/ebook-library/upload?book=<encoded-filename>`
* `DELETE /api/component/ebook-library/delete?book=<encoded-filename>`

The upload uses a raw `application/octet-stream` body, following the existing
font-upload lifecycle pattern. Retain storage ownership through close and
cleanup, including disconnect/failure. Do not pass binary data through the
component JSON configuration-save helper.

List response is HTTP 200:

```json
{
  "books": [
    {"id": "Alice.epub", "title": "Alice.epub", "size_bytes": 123456}
  ]
}
```

The portal response title uses the exact filename, while the book List display
title omits `.epub`; IDs retain the exact filename. Chapter labels use a unique
matching navigation title when available, otherwise `Chapter N`. Preserve IDs
without truncation; normal display previews can use the shared UTF-8 preview
helper.

Successful upload/delete uses HTTP 200 with `{"status":"ok"}`, matching the
existing font API pattern. Failures use the shared JSON error helper:

* HTTP 400: invalid filename/extension or invalid upload framing
* HTTP 404: requested book does not exist
* HTTP 409: duplicate filename, active book, or storage/activity busy
* HTTP 503: required runtime resources unavailable
* HTTP 507: storage/write failure or incomplete transfer

Return a failed complete-list result on allocation/capacity failure, never an
unmarked partial list. Use existing bounded PSRAM JSON/response helpers.
Do not add a generic action-execution endpoint, reader Open endpoint, catalog
database, metadata indexing service, or automatic overwrite/repair behavior.

## Best-effort rendering and performance

Do not promise publisher-perfect CSS or complete EPUB compatibility. Fixed
layout, SVG, unsupported image encodings, multimedia, RTL, complex shaping,
vertical text, scripting, and remote resources are outside the MVE contract.
Do not execute scripts or fetch remote resources.

Use existing error diagnostics where available. Unsupported images/glyphs may
produce placeholders or warnings; unreadable chapters/books may fail visibly.
Do not add an exhaustive unsupported-content detector. Do not knowingly truncate
text to fit an arena and label the result complete.

There are no fixed latency, reader-memory, file-size, or megapixel promises.
Measure normal cold/warm reading and optimize evidenced bottlenecks, balancing
performance and fidelity. Reuse existing timing, arena, heap, and stack logs.
Distinguish preparation time from physical display refresh.

Prefer PSRAM for buffers, indexes, fonts, and decoder allocations. Preserve
internal stacks for flash-sensitive work and hardware-required DMA allocations.
Check allocation failures and parser/decoder arithmetic and bounds.
Device resolution does not by itself bound image-decoder memory.
Use existing bounded decoding/scaling where supported; otherwise fail or omit
the image visibly rather than implementing a new adaptive decoder.

Shared code remains capability-gated and not board-name-gated. Use
`reterminal-sticky-interactive-sd` as the test board; another sufficiently capable
device can opt in, but flag enablement is not physical qualification.
Use the Storage facade on SD or flash; inadequate storage fails explicitly.

## Disposable cache and simple resume

Retain only the current chapter for the current book/layout. Reuse it after
reboot when valid. On chapter, book, layout, or SDK format changes, discard
obsolete reader-owned cache and rebuild. Returning to a previous chapter can
therefore require layout again.

Use a narrowly owned cache directory/files and existing checked writes and
mutation guards. Clear invalid or incomplete caches instead of repairing them.
Keep storage use bounded and fail visibly if current-chapter workspace cannot
fit. The candidate currently requires storage-backed layout; a no-cache fallback
is not promised. Never delete books, unrelated storage, or the whole card.

Cache and progress may be wiped during development, including existing spike
cache on SD. This is permission to simplify formats, not an instruction to
delete device files during planning.

Use the existing path/fingerprint and anchor mechanisms where straightforward.
Keep one current production format. Unknown format, malformed record, invalid
anchor, or changed identity means reset, not conversion. Do not parse legacy
records or preserve compatibility with spike formats.

Save progress through the existing atomic-write path at a straightforward
frame-ready/handoff checkpoint. Reuse presentation acknowledgement if it is
already sufficient; do not extend panel-completion APIs for the MVE.
The user accepts occasional progress mismatch or reset, including resuming ahead
of the physically visible e-paper page.

Reuse an existing character anchor for reflow if practical. Otherwise reset
after incompatible font, margin, geometry, or SDK changes. Do not build semantic
anchor migration. A failed save is reported; there is no extra shutdown/exit
flush protocol or guarantee of progress preservation after interruption.

When a view opens, choose its starting location in this order:

1. Explicit pending chapter selection from `ereader.open_chapter`
2. Valid retained in-memory session position
3. Valid saved per-book position
4. First readable page

Do not let reading the saved position overwrite an explicit chapter selection
made while the widget was hidden. Validate identity and location before using
retained data; a changed book invalidates its old metadata/position/target and
may reset normally. This is simple location precedence, not anchor recovery.

## Minimum safety contract

* Keep I/O, parsing, layout, and decoding on the worker; LVGL and binding
  resolution remain on the display task.
* Reject stale worker results after book/pad changes using existing generations.
  Bound pending navigation; reject busy actions instead of adding retry queues.
* Cancel and release owned resources at existing safe checkpoints. Do not add
  automatic replay, global pause ownership, or recovery state machines.
* Reuse OTA and StorageMutationGuard contracts. Interrupted reader work can be
  discarded; networking, OTA transport/status, and recovery stay operational.
* Active panel waveforms and required cleanup remain the display driver's
  responsibility. Do not change SPI speeds, waveform timing, or power sequencing.
* Reject duplicate uploads and deletion of an in-use book. Once a hidden
  reader has released resources, allow deletion and use normal missing-file
  failure on a later open; no live replacement protocol.
* Serialize book activation and deletion admission through the shared reader/file
  entry points. Checking idle and then deleting without a reservation is not
  sufficient: an Open request could intervene. Atomically reserve the operation
  before releasing the state lock, reject conflicting requests, and release
  ownership on completion/failure. StorageMutationGuard alone does not provide
  this reader-versus-deletion exclusion. Do not hold the state lock across I/O
  or build a transactional replacement protocol.
* Keep errors visible. A reader failure must not become a successful save,
  corrupt another subsystem, format storage, or force unsafe shutdown.

## Three delivery increments

### Increment 1 - Core reader and candidate check

Reuse the corrected spike to verify ordinary prose, an illustrated book, and a
representative long chapter. Check observed preparation/memory use and safe
failures before confirming the candidate; no broad preimplementation
certification campaign is required.

Implement widget typography, the shared session, page/chapter navigation,
lightweight per-book resume, disposable current-chapter cache, and registered
`ereader.open`/`ereader.navigate` operations. Explicit opening must work in this
increment because the widget has no default-book field.
Keep library and portal work out of this increment.

Gate: useful reading on the test board; both-direction and book boundaries;
chapter skips; reboot resume/cache reuse; safe invalid-cache/position reset;
second-visible-widget rejection; inherited button padding without double
insets; missing-font fallback and insufficient-space failure;
busy/same-book/boundary behavior; no stale-frame or ownership failure.

### Increment 2 - Reusable library and authoring

Complete reader actions with `ereader.open_chapter` and status/book/chapter
bindings, ordinary List item selection, the approved shared List capacity
separation, the E-book Library fragment, and physical-button page controls.
Keep runtime metadata, widget manifest, portal gates, and fixtures synchronized.

Gate: upload through the portal, select a book through pad actions/Lists,
select/skip chapters, turn pages through physical actions, and handle
missing/no-session/busy errors. Verify desktop/mobile portal workflows and
a representative user-authored pad. Include a collection larger than 20 and
preservation of existing visible-row/recent-note policies.
Verify hidden chapter selection takes precedence over saved position, chapter
metadata unavailability does not cause endless loading, and competing commands
or Open/delete requests cannot bypass admission.

### Increment 3 - Focused validation and optimization

Check repeated book/pad changes, reboot, low-memory/storage failures, interrupted
uploads/cache writes, and reader work during OTA/shutdown on the test board.
Reuse focused host tests and existing fault doubles. These checks require safe
cleanup/reset, not successful recovery or preserved progress.

Measure cold/warm and image-heavy reading and remaining system/stack headroom.
Optimize only observed bottlenecks and repeat affected checks.
Record untested boards/panels and known content limitations.

Gate: the user accepts observed responsiveness/fidelity; required safety checks
pass; documentation and generated metadata match the implemented MVE.

## SDK and verification ownership

Keep a pinned, license-compliant vendor snapshot and concise local patch record.
Repository maintainers own the integration. SDK upgrades are explicit:
compare patches, validate APIs, discard incompatible cache/progress, and repeat
affected host/device checks. No upgrade migration tooling is required.

Extend existing EPUB checkpoint/cache, font, widget, action, and binding tests
for concrete introduced behavior. Do not create a duplicate rendering harness
or a large compatibility matrix. Registration/gate changes use the repository's
existing action catalog, binding parity, portal assets, and size/flag checks.
Include focused empty-book/empty-chapter boundaries, excessive button padding,
missing fonts, location precedence, and concurrent admission cases.

Use the device-free portal server for frontend work. Batch build-sensitive
changes before `./build.sh reterminal-sticky-interactive-sd`; do not build all
boards without explicit approval. The pause status records completed checks.

Update the smallest existing user/developer documentation owners and links.
A distinct new user topic needs approval. Keep the spike report historical and
do not convert its observations into production guarantees.

## Approval boundary

The MVE provides illustrated reading, configurable typography, resume, chapter
skipping/selection, a library, upload, and physical-button actions.
It deliberately trades exceptional-state preservation for safe resets.

Share directory-scanning and validation helpers without introducing a catalog
service. Retained selection and chapter snapshots do not justify a session
framework; current-chapter caching does not justify a cache manager. Keep the
new admission and location-precedence logic local to the owning reader paths.

The approved work is paused at the status recorded above. Any later expansion to
percentage navigation, full TOC handling, recovery machinery, decoder changes,
or hardware tuning needs a separate scope decision.
