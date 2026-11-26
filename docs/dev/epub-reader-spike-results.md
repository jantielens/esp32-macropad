---
title: EPUB Reader Spike Results
description: Evidence, limitations, and follow-up questions from the two-phase FreeInkBook reader spike.
---

The two-phase spike demonstrates that an experimental EPUB reader can run on
`reterminal-sticky-interactive-sd`. The richer FreeInkBook pipeline is a
promising candidate for a production reader, but this spike does not establish
production readiness, broad EPUB compatibility, or acceptable resource and
latency budgets.

The implementation is on `spike/freeinkbook-reader`. Phase 1 is commit
`7fbd8218`; phase 2 replaces the text-only pipeline in the same widget. The
[pinned SDK revision and local modifications](../../libraries/FreeInkBook/REVISION)
are recorded alongside the vendored library.

## Scope and outcomes

| Area | Phase 1 | Phase 2 |
|---|---|---|
| Container and reading order | SDK container parsing and our text extraction | SDK container parsing and layout |
| Pagination | Our plain-text paginator using configured font metrics | SDK page layout and pagination |
| Content | Text only | Text, supported images, and a CSS subset |
| Navigation | Tap left or right, including chapter transitions | Same interaction, using SDK pages |
| Persistence | Text-byte reading position | Versioned SDK character anchor and page number; page cache on SD |

The user confirmed the configured font and pagination correction in phase 1.
In phase 2, the user confirmed that text and images appear in the Alice EPUB
after the CSS correction. Device logs also show page turns and transitions
between front matter and chapters. These observations do not establish that
every EPUB, styling feature, image format, or navigation edge case works.

The experimental widget takes an absolute EPUB path and is gated for the
target board. A library browser, bookmarks, percentage jumps, table-of-contents
UI, upload UI, and production polish were outside the spike.

## Integration design

The [reader widget](../../src/app/widgets/ereader_widget.cpp) owns the
LVGL objects, input, and image handoff. The
[SDK worker](../../src/app/widgets/epub_sdk_worker.h) performs book I/O,
parsing, layout, image decoding, and text rendering outside the LVGL task.
Built-in font requests are serviced on the LVGL task; configured TrueType
font data is loaded for the SDK font engine. The
[font bridge](../../src/app/widgets/epub_sdk_font.h) and
[font store](../../src/app/font_store.cpp) contain the corresponding paths.

The worker lays out an entire chapter on a cache miss. The
[cache adapter](../../src/app/widgets/epub_cache_storage.h) writes serialized
pages under `/storage` on SD, using a temporary file and rename/backup commit.
An 8 KiB PSRAM buffer supports buffered writes and read-ahead; the current
chapter index also occupies PSRAM. On a hit, the reader loads cached page
data rather than re-laying out the chapter. Images are decoded again when
their pages are rendered; decoded images are not the persistent page cache.

Cache identities include the book fingerprint and layout parameters, such as
font and geometry. A layout revision invalidates stale pages when layout
behavior changes. The SDK position stores book identity, chapter, character
anchor, layout identity, and page number. A phase 1 position has a different
format and is restarted with a warning instead of being interpreted as an SDK
position. Reboot resume and cache reuse in phase 2 still need device tests.

Storage operations use the storage facade and bounded activity guards.
Long-running parsing, rendering, and font loading check for interruption.
These mechanisms have focused host coverage, but the spike has not validated
OTA, shutdown, or shared SPI contention under device stress.

## Measurements and evidence

All measurements below are observations from particular builds and runs, not
performance guarantees. Page-ready times exclude final e-paper refresh.

| Observation | Result | Qualification |
|---|---|---|
| Phase 1 application size | 3,736,602 bytes | Final phase 1 build |
| Phase 2 application size | 3,882,942 bytes | Build after the CSS correction; 146,340 bytes more than phase 1 |
| Phase 1 first-page preparation | About 1.8 seconds | Earlier device run, before the final phase 1 font/pagination changes |
| Phase 2 Alice title page | About 10.1 seconds to page-ready | Initial cache miss; run before CSS correction |
| Phase 2 Alice frontispiece | About 13.1 seconds for chapter change to page-ready | Image-heavy page; run before CSS correction |
| Phase 2 Alice page turns | About 2.8 and 3.5 seconds to page-ready | Two turns within a chapter affected by the CSS bug |
| Phase 2 PSRAM | 1,349,584 bytes boot-wide minimum free; 1,376,244 bytes sampled minimum largest block | Alice run before CSS correction; not a reader-only peak |
| Phase 2 internal RAM | 57,264 bytes boot-wide minimum free; 31,732 bytes sampled minimum largest block | Same run; not a reader-only peak |

The phase 1 and phase 2 timings used different runs and content. They are not
a controlled comparison. The Alice run with timings had a CSS bug that hid
paragraphs, so its chapter pagination and text-page performance are not valid
evidence for the corrected build.

At the observed widget dimensions, the grayscale worker frame and RGB565 LVGL
frame together use roughly 1 MiB of PSRAM. Fixed SDK arenas and the cache I/O
buffer reserve another 1,352 KiB, before font and other allocations. The
worker uses an internal RAM stack because it performs flash-sensitive
operations. SD and display share the SPI bus; the board configures SD at
1 MHz. These facts identify optimization candidates, not a measured cause of
the observed latency.

## Test evidence and gaps

The final phase 2 board build succeeded with
`./build.sh reterminal-sticky-interactive-sd`. Focused host tests passed for
font storage, EPUB parsing checkpoints, CSS behavior, cache interruption and
read-ahead, and widget behavior. The vendored SDK layout and cache suites
passed 422 and 180 checks respectively. They include image-related host
checks but do not validate the full device rendering path.

A temporary host check against the actual Alice EPUB, after the CSS fix,
produced 11 pages, 11,189 chapter characters and three image placements. The user
subsequently confirmed that text and images appeared on the device. The host
result uses a test font and is not a device performance measurement.

The following remain unverified or incomplete:

* A large-chapter EPUB on the device, including images and styling.
* Phase 2 reboot resume, cache reuse after reboot, and both directions across
  all chapter and book boundaries.
* Reader-specific peak internal RAM and PSRAM, including largest free blocks,
  under representative workloads.
* Corrected-build first-page, chapter-change, page-turn, and display-refresh
  timings for representative cold and warm caches.
* OTA, shutdown, pad changes, SD errors, and shared SPI contention while
  loading or writing caches.
* Malformed, encrypted, unsupported, or unusual EPUBs and missing-glyph paths.

A broader host build encountered unrelated existing compilation errors.
Focused checks passed; the complete host suite was not established as clean.

## Problems and limitations

The first device attempt hit a worker stack canary; moving the ZIP scan buffer
to an arena and increasing stack headroom prevented recurrence in the observed
run. Phase 1 initially used the wrong default font and overestimated some text
widths. Those issues were corrected and the user confirmed the result.

An application-only serial upload can leave another OTA slot active. This
caused one image test to run the old text-only firmware. Verify the running
firmware identity when comparing results.

The spike benefited from reusing SDK parsing, layout, and test suites, and
the user could validate actual text and images on the target. The integration
also exposed the cost of large allocations, chapter-wide layout, shared
hardware, and partial CSS support. Device debugging and a misleading old
firmware upload delayed validation. The fixed build has not yet supplied the
complete performance and lifecycle evidence needed for a production decision.

The SDK approximates complex CSS selectors using the rightmost selector. In
Alice, a title-page-only rule was broadened to all paragraphs and silently
hid chapter text. The local fix ignores `display:none` for complex selectors;
it can therefore show content that the original stylesheet intended to hide.
Other complex selector behavior remains approximate, and manifest stylesheets
are merged globally instead of selected per chapter.

The SDK can silently omit unsupported or unresolved content. Supported image
rendering includes PNG and JPEG, but interlaced PNG is rejected; complete
format and EPUB compatibility have not been established. The configured font
may lack individual glyphs. The spike reports errors and missing glyphs where
the SDK exposes them, not every possible omission.

## SDK maintenance and production follow-up

Most integration code is firmware-side. The vendored SDK also has local
changes for cancellation, PSRAM allocation, ZIP stack usage, JPEG symbol
isolation, and CSS behavior. Retained third-party licenses accompany the
vendored sources. A local comparison with the pin found 31 identical and 13
modified comparable SDK source/header files, plus five local forwarding
wrappers; packaging differences were excluded from those counts. The changes
are therefore more than a configuration-only integration, but do not replace
the SDK layout engine. An SDK upgrade requires comparing and reapplying or retiring
these modifications, checking adapter APIs and cache/position compatibility,
and repeating host and device validation.

Production scope should decide the reader interaction model and feature
boundaries, supported EPUB/CSS and image behavior, resource budgets,
responsiveness targets, cache retention, error handling, migration, and SDK
ownership. Measure stage-specific I/O, layout, decoding, rendering, and display
latencies before selecting optimizations. Consider reducing framebuffers and
right-sizing or safely reusing arenas, while preserving large-chapter and
hardware lifecycle safety. Both lower latency and lower memory use appear
plausible, but neither a few-second responsiveness target nor a particular
memory reduction is demonstrated by the spike.
