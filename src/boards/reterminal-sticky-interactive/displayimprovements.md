---
title: Display Improvements for reTerminal Sticky Interactive
description: Controller protocol safeguards and scoped performance opportunities for the Sticky display.
---

## Findings first

Refresh completion now distinguishes an idle BUSY level from a completed
waveform. The board-local wait observes assertion and deassertion for either
controller polarity, rather than accepting an idle sample immediately after
activation. This correctness change does not shorten panel waveforms or establish
a hardware-validated speedup.

| Priority | Finding | Evidence and qualification |
|---|---|---|
| Hardware-validation pending | Both controllers conservatively reuse compatible preparation | SSD1677 permits only consecutive successful B&W partials. SSD2677 requires the same render mode, full/partial intent and freshly selected waveform; latching and checked power-off remain. Retained-state quality still requires controller-specific validation. |
| **Low — straightforward software opportunity** | **Default-orientation screenshots unnecessarily reconstruct an already correctly packed frame.** | `copyPresentedFrame` clears the destination, processes every pixel, and yields every 16 rows while holding the driver mutex. At rotation 0, the existing presented buffer already has the required output layout. A direct copy can preserve identical output while shortening screenshot-related blocking. This does not accelerate the physical waveform. |
| Hardware-validation pending | Byte-oriented packing now uses bounded multi-row transfers | The panel-owned 800-byte buffer holds eight SSD1677 rows or four SSD2677 rows. Each presentation uses 120 payload transactions. A cumulative 4 ms upload work budget replaces fixed row-count sleeps; cancellation remains independent of yielding. Physical cursor continuity, fairness and stage-level gains still require measurement. |

The absence of geometric dirty transfers is **not itself a bug**: Seeed’s own dashboard deliberately sends full comparison frames for B&W partial refresh.

## Official baseline and sources

The pinned Seeed_GFX2 revision is:

`0e8bac1188cb915d5ea26ea16d538aed13b3cf48`

The current official `main` inspected was:

`a3f27956b088a3a28b23da0ecb99ae6159f19702`

Their [comparison](https://github.com/Seeed-Studio/Seeed_GFX2/compare/0e8bac1188cb915d5ea26ea16d538aed13b3cf48...a3f27956b088a3a28b23da0ecb99ae6159f19702) changes only star-history assets. **There is no newer controller implementation to import for a speed improvement.**

The principal evidence was:

- [Official refresh guide](https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-refresh/): mode selection, full comparison frames, change-driven minute updates, and periodic ghosting cleanup.
- [Arduino basics](https://www.seeedstudio.com/sticky/docs/en/device-guide/arduino-basics/) and [interaction guide](https://www.seeedstudio.com/sticky/docs/en/device-guide/arduino-interaction/): Gray4 configuration, regional partial API, and blocking-refresh limitations.
- Pinned [SSD1677 implementation](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD1677.cpp), [SSD2677 implementation](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD2677.cpp), and [panel update orchestration](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/panel/Panel_EPaper.cpp#L725-L877).
- Official [dashboard archive](https://files.seeedstudio.com/wiki/reterminal_sticky/res/Sticky_dashboard_demo.zip) and [peripheral archive](https://files.seeedstudio.com/wiki/reterminal_sticky/res/Sticky_peripheral_demo.zip), including their actual display, panel, SPI, and SSD1677 sources.

No refresh-duration measurements were found in the principal vendor guides
reviewed. Repository SSD1677 smoke captures show upload and preparation savings,
with user-reported normal visual operation. These are limited smoke comparisons,
not controlled whole-plan A/B results or complete hardware qualification.

## Vendor comparison

Archive references below identify paths inside the linked official ZIP.

| Vendor technique | Source/controller/mode | Our current implementation | Gap/tradeoff | Recommendation |
|---|---|---|---|---|
| Use B&W differential partial refresh for small interactive changes; periodically clean fully | [Refresh guide](https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-refresh/); both controllers through their respective implementations | **Implemented.** `Selection` uses partial B&W after a valid full baseline, with configured cleanup and manual full refresh | Repeated partials still accumulate ghosting | Retain |
| Keep Gray4 on its dedicated full-refresh path | [Arduino basics](https://www.seeedstudio.com/sticky/docs/en/device-guide/arduino-basics/); both controllers | **Implemented.** Gray4 always selects full refresh; `controller paths` use the appropriate encoding and waveform | Slower updates are not solved by substituting a monochrome waveform | Retain; grayscale partial remains outside scope |
| Refresh only when meaningful content changes | [Refresh guide](https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-refresh/): RTC update only after time/validity changes | **Implemented, more generally.** Changed-pixel detection plus `presented-frame comparison` suppress physical no-ops | Conversion can still occur before the final comparison | Retain; optimize packing rather than weakening comparison |
| Cache controller preparation while refresh mode remains unchanged | Dashboard ZIP, `epaper_panel.c:917–961`; SSD1677 B&W partial | Experimental SSD1677-only reuse between completed compatible partials | Invalidate on transitions, reset, power loss and incomplete work; official Arduino paths also reset each time | Measure preparation savings and unchanged-region integrity; do not generalize to SSD2677 |
| Use controller RAM windows for regional transfers | [SSD1677 windowed write](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD1677.cpp#L284-L323); Arduino `refreshPartial(x,y,w,h)` | **Not implemented geometrically.** All streams cover 800×480 | Official dashboard instead deliberately sends full previous/current planes; smaller RAM writes do not establish safe whole-panel behavior | Defer behind preparation/packing work and dedicated quality testing |
| Encode SSD2677 partial updates as whole-frame old/new transition pairs | [SSD2677 partial implementation](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD2677.cpp#L456-L510) | **Implemented.** `Packing` combines previous/current bits | Regional public API does **not** imply regional SSD2677 SPI traffic | Retain full comparison transfer |
| Use 10 MHz writes, 4 MHz reads, SPI mode 0 | [Sticky board configuration](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/board/configs/reTerminal_ePaper_Board_Configs.h#L14-L17); [FreeInk Sticky profile](https://github.com/Free-Ink/freeink-sdk/blob/e637713a533ea7583ba73fa2052967c21cf0335d/libs/hardware/BoardConfig/include/BoardConfig.h#L1639) provides 20 MHz adoption precedent | Board-local writes now use 20 MHz; reads remain 4 MHz, MSB-first, mode 0 | Higher write speed is experimental, not physically qualified by upstream precedent or host tests | Validate commands, detection and image integrity on each panel variant; retain the 10 MHz build for rollback |
| Transfer in bulk/chunks rather than application-level byte transactions | Dashboard ZIP, `epaper_panel.c:454–500`: 1,024-byte DMA staging; Arduino [bus implementation](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/bus/Bus_SPI.cpp#L101-L160) loops over bytes | 800-byte panel-owned batches with per-row cancellation and budgeted sleeps | Hardware cursor continuity and fairness still require qualification | Retain bounded bulk writes; no framework import |
| Select temperature-appropriate SSD2677 waveforms | [Full/partial/Gray4 selectors](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD2677.cpp#L53-L123) | **Implemented.** `Local selector` preserves the buckets and warns on the 25°C fallback | Fallback is not a real temperature measurement; quality and cold behavior remain unverified | Preserve parameters; validate temperature handling physically |
| Wait for BUSY completion; IDF can sleep on an interrupt/semaphore | Dashboard ZIP, `epaper_panel.c:514–596` | Refresh-only polling observes assertion, then completion; ordinary command waits remain ready-level waits | The initial 20 ms assertion window and polling resolution still require physical validation; interrupts remain out of scope | Preserve completion safeguards |
| Sleep between standalone Arduino updates | [Arduino panel orchestration](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/panel/Panel_EPaper.cpp#L725-L877) | **Intentionally avoided.** Interactive driver does not deep-sleep the panel after each update; SSD2677 drive power-off remains explicit | Sleep adds wake/preparation costs and invalidates retained state | Keep the interactive policy |
| Separate “fast refresh” API | [Driver capability default](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/core/Driver.h#L60), forwarded by Sticky auto-driver | Both Sticky controller implementations report no separate fast-refresh capability | Other products’ fast modes/register recipes are not Sticky evidence | Do not transplant them |

### Important vendor distinction

The dashboard’s B&W partial path is verified end to end:

1. Rotate/convert the complete canvas.
2. Submit the full 800×480 area.
3. Read the previous image from its CPU-side framebuffer.
4. Send previous data to SSD1677 `0x26` and current data to `0x24`.
5. Trigger the partial waveform.

Thus, **full-frame comparison transfers are an official design choice**, despite the generic component also exposing windowed operations.

## Our actual update path

### 1. LVGL rendering and flush conversion

The board selects a single **800×10 RGB565 draw buffer**, normally in PSRAM, and a 100 ms LVGL refresh period: `board settings`.

The `flush callback`:

- Supplies the actual padded source stride.
- Passes the LVGL area through the HAL.
- Calls the buffered driver without RGB565 byte swapping.
- Acknowledges the flush without waiting for the panel.

`pushColors` clips and rotates pixels, computes luminance, applies the shared `monochrome conversion`, and updates the packed drawing buffer.

**Dirty tracking is a changed flag and timestamps—not a dirty rectangle or dirty-row map.**

### 2. Scheduling and coalescing

The `presentation worker` runs separately from LVGL, so panel BUSY time does not directly block touch processing.

The driver combines:

- Default **150 ms quiet settling**.
- A **500 ms cap on waiting for quiet**, preventing continuous changes from postponing presentation indefinitely.
- Default **500 ms minimum interval measured from the preceding presentation’s completion**.

These waits overlap; they are **not simply added together**. Manual full refresh bypasses settling but still respects the applicable cadence.

Passive bindings are separately throttled, with initial values resolved before first rendering: `binding scheduling`. Animations and tap flashes are suppressed for this board.

### 3. No-op suppression and ownership

Before touching hardware, `present` compares drawing against the last completed frame.

For a real update:

- `drawing` remains writable by LVGL.
- `sending` becomes the immutable in-flight frame.
- `presented` remains the previous completed frame and comparison baseline.
- Snapshot validity is cleared before presentation starts.

The mutex is released during slow panel operations. This is the correct ownership model to preserve.

### 4. Selection, preparation and transfer

Full refresh is selected for grayscale, first presentation, recovery, manual cleanup, or the configured partial-count threshold. Otherwise B&W uses partial refresh.

Every presentation transfers the entire visible frame. SSD1677 skips base
preparation only when
the preceding presentation was a successful B&W partial and the next is also
a B&W partial. A successful full refresh does not seed this cache.

The presentation task owns one prepared-state flag. It is cleared before risky
work and set again only after successful physical completion of an eligible
update and, for SSD2677, checked cleanup. Begin/end, reset, power restoration/cut,
mode transitions, errors and
deferrals invalidate it. Reuse retains the pre-upload ready check; it cannot
write through an unexpectedly active BUSY level.

SSD2677 adds a render-mode, full/partial-intent and waveform-code compatibility
key. Temperature is sampled once per presentation, restoring the initialized
`0xE0=0x10` selection before `0x40` instead of reading with the retained waveform
bank. Bucket boundaries, waveform values, invalid-reading 25 C fallback and
warning behavior remain unchanged. Compatible updates skip reset/static setup;
changed or invalid keys initialize before latching. Every update still selects
the waveform bank (`0xE0=0x12`), latches OTP, waits ready, settles for 10 ms,
uploads the complete frame and performs checked power-on/refresh/power-off.
Neither drive power nor temperature measurements are cached. Restoring the
pinned initialization's pre-read selection is conservative software behavior,
not proof of retained-state temperature correctness on hardware.

Both SSD1677 planes explicitly restore the full X/Y window and start counters
before their RAM-write command. The partial border (`0x80`) and update mode
(`0xff`) remain programmed on every partial; full/grayscale preparation restores
their existing border and temperature behavior. Plane order, encoding and
full-frame payloads are unchanged:

| Controller/mode | Transfer |
|---|---|
| SSD1677 B&W | 48,000-byte previous plane plus 48,000-byte current plane |
| SSD1677 Gray4 | Two 48,000-byte bitplanes |
| SSD2677 B&W full | 96,000-byte expanded 2-bpp frame |
| SSD2677 B&W partial | 96,000-byte old/new transition frame |
| SSD2677 Gray4 | 96,000-byte 2-bpp frame |

At the configured 20 MHz write clock, 96,000 payload bytes alone require
**38.4 ms of ideal wire time**, compared with 76.8 ms at the earlier 10 MHz
clock. This is arithmetic, not measured upload latency or a claim that total
presentation time halves. Packing, transaction overhead, scheduling and the
unchanged waveform add to it. Reads remain at 4 MHz, including controller
probing and temperature sampling.

The clock change is one board-local write setting, independent of batching and
preparation caching. There is no automatic speed fallback: the write-only image
path cannot reliably detect visual corruption. Keep the preserved 10 MHz
firmware for a manual A/B comparison or rollback.

Byte-oriented packing is unchanged. Consecutive rows fill an 800-byte
panel-owned buffer, avoiding another allocation or a presentation-task stack
array. The RAM-write command is issued once per plane; each batch releases CS
and ends its transaction before any wait or return. A final remainder is
submitted if present, without splitting rows or changing plane order.

This reduces payload transactions from 960 to 120 for SSD1677 and from 480 to
120 for SSD2677. Command transactions are additional. Full visible-frame
payloads remain 48,000 bytes per SSD1677 plane or 96,000 bytes per SSD2677 frame.
Cancellation is checked between rows and immediately before submission; an
unfinished batch is discarded without activating an incomplete frame.

The upload shares one elapsed-work budget across both SSD1677 planes. After
4 ms, checked between bounded batches, it sleeps one RTOS tick with no SPI
transaction held and resets the budget. Packing and SPI time remain separate
diagnostics; `packed_bytes` counts packed rows, while `bytes` counts only
submitted payload. Cancelled uploads retain their measured duration and yield
time rather than reporting a successful full transfer.

### 5. Waveform completion and recovery

`Controller completion` preserves controller-specific commands and BUSY polarity:

- SSD1677: active-high BUSY; `0xF7` full, `0xFF` partial, `0xD7` Gray4.
- SSD2677: active-low BUSY; temperature-selected waveform, power-on, refresh, then checked power-off.

Each refresh has one wrap-safe 15-second deadline starting around activation.
Polling initially sleeps one RTOS tick and switches to nominal 2 ms sleeps after
observing BUSY. An entirely unobserved assertion within the initial 20 ms window
is an explicit error, not confirmed completion. The 20 ms window is an
experimental starting point; missed short pulses require narrower hardware
investigation, not success-shaped fallback.

Ordinary command-ready waits do not require a visible BUSY pulse. Both
controllers allow one RTOS tick for command propagation, including SSD1677
software reset and SSD2677 power-on, waveform latch and checked power-off.
Without this allowance, a faster upload could consume delayed reset BUSY as
refresh completion. The timeout remains 15 seconds per wait, so the entire
presentation may take longer than one refresh deadline.

OTA checks occur during scheduling, preparation/waits and row streaming.
Cancellation after activation is remembered while the waveform drains to
completion or its deadline. SSD1677 checks readiness before resetting on a
retry, so a timed-out active waveform cannot be reset by the next presentation.
Once SSD2677 drive ownership has begun, cleanup drains owned command work,
performs checked power-off, or cuts the rail on failure. Cancellation during
cleanup also leaves the snapshot invalid and requires full recovery. These
operations must not be bypassed for speed.

Only successful completion publishes `sending` as `presented`. Failure/cancellation invalidates the baseline and requires a full retry on a subsequent presentation request.

### 6. Screenshot correctness

The `screenshot endpoint` uses the completed-frame interface, not the current LVGL drawing buffer. It refuses capture while no completed frame is valid.

This correctly avoids exposing an unsent or interrupted frame as the displayed image. Any optimization must retain that behavior.

## Controller-specific boundaries

### SSD1677

- Real RAM window programming exists in official code.
- Both previous and current image handling matter.
- Seeed’s Arduino implementation uses regional writes, but its dashboard deliberately chooses full comparison frames.
- Same-mode preparation caching has official **SSD1677 ESP-IDF** precedent.
- Gray4 uses two planes and its own refresh setup; B&W shortcuts must not leak into it.

### SSD2677

- The required internal resolution is **800×680**, despite only 800×480 visible pixels.
- Our `initialization` preserves that geometry.
- Official code still sends **480 visible image rows**. Do not “correct” it to 680 transferred rows, or reduce the controller resolution to 480 to seek a speedup.
- Official partial support patches a CPU-side window but sends the complete old/new transition frame.
- The inspected implementation supplies no equivalent regional-transfer sequence or same-key cached-init precedent. Our waveform-keyed preparation reuse is an experiment, not hardware qualification from that source.
- No applicable `0x49` or `0x90` shortcut was found in the inspected Sticky controller implementations.

## Prioritized improvements

### Low-risk software opportunities

**1. Byte-oriented controller packing**

Implemented before the batching change. Existing reference-wire fixtures cover
native orientation, bit order, old/new transition encoding, grayscale planes,
rotation, clipping and padded source stride. Batching reuses these transforms
without changing payload concatenation or waveforms.

**2. Rotation-0 screenshot copy fast path**

- **Affected code:** `copyPresentedFrame`.
- **Benefit mechanism:** replace redundant reconstruction with a copy of the already packed completed frame.
- **Risks:** accidentally copying drawing/in-flight data or weakening snapshot invalidation.
- **Validation:** exact B&W/Gray4 output equivalence; capture refusal during interrupted presentation; retain existing rotated behavior.
- **Priority caveat:** useful primarily when screenshots are being requested.

### Changes requiring hardware A/B testing

**3. Cache preparation for consecutive SSD1677 B&W partials**

Implemented as a board-local prepared-state flag, not a second framebuffer
baseline. Only the second and later consecutive successful B&W partials reuse
preparation. Every plane explicitly restores its full window and counters.

The existing host double models addressed RAM placement and deliberately leaves
nonzero counters and a narrowed window after a plane transfer. Reference-image
checks therefore detect shifted or clipped uploads, not only equal concatenated
bytes. Tests cover full/partial chains, manual and periodic cleanup, grayscale,
controller re-selection, lifecycle, cancelled packing/activation and failures.
Incomplete work invalidates reuse, and the presentation driver still retries
fully.

Limited SSD1677 smoke captures show median preparation reductions of about
24-25 ms, within the 20-40 ms target. Controlled comparisons and extended
unchanged-region integrity and ghosting checks remain outstanding; do not
attribute batching gains to preparation reuse.

**4. Bounded multi-row SPI batches**

Implemented through the existing bulk-write path with an 800-byte member
buffer and time-budgeted one-tick sleeps. Host tests verify 120 complete payload
transactions, cancellation with a partially packed or unsubmitted batch, and
balanced transactions on existing failure paths. Simulated wire time checks
bounded scheduling across both SSD1677 planes; it is not an ESP32 performance
measurement. Larger bursts still require hardware cursor and image-quality
validation.

**5. Cadence tuning, only after measurement**

The completion-based 500 ms interval can dominate perceived response after a previous update. However, it is intentional policy, not an accidental sleep. Do not lower it silently. First establish whether rendering, cooldown, preparation or waveform time is responsible for poor interaction.

### Not recommended initially or outside scope

- SSD2677 regional-transfer experiments.
- SSD1677 window narrowing before validating the conservative full-comparison behavior.
- Grayscale partial refresh.
- LUT, PLL, forced-temperature or undocumented-register tuning.
- Increasing writes beyond 20 MHz or treating 20 MHz as qualified without panel testing.
- Removing BUSY checks, power-off cleanup, OTA cancellation or periodic full cleanup.
- Importing Seeed_GFX2 or the dashboard framework wholesale.
- Adopting sleep-first page/update policy for this interactive Macropad.

### Reuse terms

The relevant Seeed_GFX2 controller source is covered by its [MIT license](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/LICENSE). Preserve the copyright/permission notice, pinned provenance and local-change attribution already represented by our `panel header` and `retained license`.

**No governing reuse grant was found for the dashboard/peripheral display components.** Their button component’s license does not license the remaining application. Use those sources as behavioral evidence; do not copy their implementation without confirmed permission.

## Practical hardware measurement checklist

Run separately on identified SSD1677 and SSD2677 units.

1. **Record conditions:** controller identification, panel revision, firmware/configuration, rotation, temperature, supply/battery voltage and screenshot polling.
2. **Timestamp distinct stages:**
   - Touch/event received and action dispatched.
   - LVGL rendering and flush conversion.
   - Coalescing/cadence waiting.
   - Frame comparison/copy.
   - Reset/register preparation and waveform latch.
   - Packing CPU time.
   - SPI payload transfer.
   - Power-on BUSY, refresh BUSY and power-off BUSY.
   - Completed-frame publication.
3. **Use a logic analyzer:** capture SCK, CS, DC, RESET, BUSY and optionally a timing GPIO. Correlate with software timestamps and video of the panel.
4. **Do not treat existing `present_us` as transfer time:** it includes scheduling, preparation, packing, waveform waits and potentially multiple presentations.
5. **Exercise representative cases:** first full, settled no-op, reverted change, one-button update, dispersed changes, full-screen navigation, manual cleanup and Gray4 ramp.
6. **Check repeated partial quality:** at least the planned 50 small updates, retaining the configured full-refresh cleanup cadence. Photograph unchanged areas, alternating edges, text, contrast and residual images.
7. **Verify recovery:** OTA during cadence, streaming and BUSY; interrupted SSD2677 power transitions; timeout recovery; next full update; screenshot refusal while invalid.
8. **A/B one change at a time:** compare identical content and conditions, reporting distributions rather than one best sample.

The `existing acceptance plan` remains outstanding; host doubles cannot establish panel quality, timing or physical recovery.

## Validation and conclusion

Executed successfully:

```text
cmake --build build/host-tests --parallel 2 --target test_sticky_epaper_driver test_sticky_epaper_driver_diagnostics
ctest --test-dir build/host-tests --output-on-failure -R '^test_sticky_epaper_driver(_diagnostics)?$'
```

Both host profiles pass. The SPI double records the settings used by actual
read/write operations and transaction completion, covering 20 MHz writes,
4 MHz reads, MSB-first mode 0, and release before waits or cancellation exits.
Existing wire-equivalence tests retain both controllers' B/W and Gray4 payloads.
SSD2677 cache coverage checks fresh reads and bank restoration, skipped reset
and static setup only on compatible keys, bucket boundaries and invalid-read
fallback, full/partial and full/Gray4 transitions (including shared `0xf1`
codes), latch settling, complete payloads and checked drive cleanup. Setup,
temperature, latch, upload, waveform and cleanup cancellation invalidate reuse;
failed power transitions cut the rail and the presentation driver retries fully.
Host results do not establish physical clock accuracy or image quality.

**Conclusion:** B&W differential partial waveforms, previous/current comparison
data, no-op suppression, cleanup refreshes, temperature-specific SSD2677
selection and separate asynchronous presentation remain unchanged. Writes now
use the experimental 20 MHz setting while reads retain the vendor 4 MHz clock.
The interactive profile still avoids sleep after every update.

Bounded multi-row transfers, conservative SSD1677 partial preparation reuse and
SSD2677 waveform-aware preparation caching are implemented; extended physical
acceptance remains outstanding. SSD2677's expected 70-100 ms eligible preparation
saving is a target, not a hardware measurement. Screenshot-copy work
remains deferred. There is no evidence-backed reason to change waveform values
or SSD2677 scan geometry.