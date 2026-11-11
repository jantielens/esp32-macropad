# Display Improvements for ReTerminal Sticky Interactive

## Findings first

**No confirmed controller-protocol bug emerged from this audit.** There are software overheads worth addressing, but no evidence justifies shortening BUSY waits, changing waveform parameters, or removing recovery safeguards.

| Priority | Finding | Evidence and qualification |
|---|---|---|
| **Medium — hardware-gated opportunity** | **Every presentation repeats controller preparation.** | `Panel::refresh` always calls `initialize()`: SSD1677 has 10/10 ms reset delays; SSD2677 has 20/50 ms reset delays, followed by waveform preparation. Seeed’s Arduino implementation also reinitializes for partial updates. However, its **ESP-IDF SSD1677 implementation caches preparation between consecutive same-mode partials**. That is a credible optimization for SSD1677—not proof that initialization can safely be skipped on either controller indiscriminately. |
| **Low — straightforward software opportunity** | **Default-orientation screenshots unnecessarily reconstruct an already correctly packed frame.** | `copyPresentedFrame` clears the destination, processes every pixel, and yields every 16 rows while holding the driver mutex. At rotation 0, the existing presented buffer already has the required output layout. A direct copy can preserve identical output while shortening screenshot-related blocking. This does not accelerate the physical waveform. |
| **Low — profile before optimizing** | **Controller streaming repeatedly unpacks and repacks individual pixels.** | `Panel::stream` traverses the whole visible frame for every plane. SSD1677 traverses it twice. It also introduces 30 explicit task delays per stream. Byte-oriented packing and bounded transfer batching are plausible improvements, but neither their actual cost nor achievable savings has been measured. **Do not simply delete the yields.** |

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

No refresh-duration measurements were found in the principal guides reviewed. Consequently, this assessment makes **no measured speedup claim**.

## Vendor comparison

Archive references below identify paths inside the linked official ZIP.

| Vendor technique | Source/controller/mode | Our current implementation | Gap/tradeoff | Recommendation |
|---|---|---|---|---|
| Use B&W differential partial refresh for small interactive changes; periodically clean fully | [Refresh guide](https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-refresh/); both controllers through their respective implementations | **Implemented.** `Selection` uses partial B&W after a valid full baseline, with configured cleanup and manual full refresh | Repeated partials still accumulate ghosting | Retain |
| Keep Gray4 on its dedicated full-refresh path | [Arduino basics](https://www.seeedstudio.com/sticky/docs/en/device-guide/arduino-basics/); both controllers | **Implemented.** Gray4 always selects full refresh; `controller paths` use the appropriate encoding and waveform | Slower updates are not solved by substituting a monochrome waveform | Retain; grayscale partial remains outside scope |
| Refresh only when meaningful content changes | [Refresh guide](https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-refresh/): RTC update only after time/validity changes | **Implemented, more generally.** Changed-pixel detection plus `presented-frame comparison` suppress physical no-ops | Conversion can still occur before the final comparison | Retain; optimize packing rather than weakening comparison |
| Cache controller preparation while refresh mode remains unchanged | Dashboard ZIP, `epaper_panel.c:917–961`; **SSD1677 B&W partial** | **Not implemented.** `Initialization` repeats each presentation | Requires trustworthy prepared-state invalidation; official Arduino paths also reset each time | Best controller-side A/B candidate, SSD1677 only initially |
| Use controller RAM windows for regional transfers | [SSD1677 windowed write](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD1677.cpp#L284-L323); Arduino `refreshPartial(x,y,w,h)` | **Not implemented geometrically.** All streams cover 800×480 | Official dashboard instead deliberately sends full previous/current planes; smaller RAM writes do not establish safe whole-panel behavior | Defer behind preparation/packing work and dedicated quality testing |
| Encode SSD2677 partial updates as whole-frame old/new transition pairs | [SSD2677 partial implementation](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD2677.cpp#L456-L510) | **Implemented.** `Packing` combines previous/current bits | Regional public API does **not** imply regional SSD2677 SPI traffic | Retain full comparison transfer |
| Use 10 MHz writes, 4 MHz reads, SPI mode 0 | [Sticky board configuration](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/board/configs/reTerminal_ePaper_Board_Configs.h#L14-L17); IDF dashboard also writes at 10 MHz | **Implemented exactly.** `SPI settings` | No official evidence here for a higher safe panel clock | Do not raise the clock without hardware qualification |
| Transfer in bulk/chunks rather than application-level byte transactions | Dashboard ZIP, `epaper_panel.c:454–500`: 1,024-byte DMA staging; Arduino [bus implementation](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/bus/Bus_SPI.cpp#L101-L160) loops over bytes | **Partially implemented—and already better than importing the Arduino bus loop.** `Streaming` calls `writeBytes()` for each 100/200-byte row | More transactions than the IDF implementation; larger staging costs memory and complicates cancellation | Profile bounded multi-row batches; no framework import |
| Select temperature-appropriate SSD2677 waveforms | [Full/partial/Gray4 selectors](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD2677.cpp#L53-L123) | **Implemented.** `Local selector` preserves the buckets and warns on the 25°C fallback | Fallback is not a real temperature measurement; quality and cold behavior remain unverified | Preserve parameters; validate temperature handling physically |
| Wait for BUSY completion; IDF can sleep on an interrupt/semaphore | Dashboard ZIP, `epaper_panel.c:514–596` | **Alternative implemented:** `bounded cooperative polling`, nominally every 2 ms | Interrupt waiting can reduce polling work, not waveform duration; adds lifecycle complexity | Low priority |
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

Every presentation then initializes the controller and transfers the **entire visible frame**:

| Controller/mode | Transfer |
|---|---|
| SSD1677 B&W | 48,000-byte previous plane plus 48,000-byte current plane |
| SSD1677 Gray4 | Two 48,000-byte bitplanes |
| SSD2677 B&W full | 96,000-byte expanded 2-bpp frame |
| SSD2677 B&W partial | 96,000-byte old/new transition frame |
| SSD2677 Gray4 | 96,000-byte 2-bpp frame |

At the configured 10 MHz, 96,000 payload bytes alone require **76.8 ms of ideal wire time**. This is arithmetic, **not measured transfer latency or a predicted speedup**; transaction overhead, packing, scheduling and controller activity add to it.

### 5. Waveform completion and recovery

`Controller completion` preserves controller-specific commands and BUSY polarity:

- SSD1677: active-high BUSY; `0xF7` full, `0xFF` partial, `0xD7` Gray4.
- SSD2677: active-low BUSY; temperature-selected waveform, power-on, refresh, then checked power-off.

Waits are bounded to 15 seconds **per wait**, not necessarily per entire presentation.

OTA checks occur during scheduling, preparation/waits and row streaming. Once SSD2677 drive ownership has begun, `cleanup` completes bounded power-off or cuts the rail on failure. These operations must not be bypassed for speed.

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
- The inspected implementation supplies no equivalent regional-transfer sequence or cached-init precedent.
- No applicable `0x49` or `0x90` shortcut was found in the inspected Sticky controller implementations.

## Prioritized improvements

### Low-risk software opportunities

**1. Byte-oriented controller packing**

- **Affected code:** `Panel::stream`.
- **Benefit mechanism:** reduce repeated coordinate calculations, bit extraction and per-pixel packing without changing bytes sent or waveform selection.
- **Smallest reuse:** the MIT SSD2677 byte-expansion approach; similarly small byte/bitplane transformations for other paths.
- **Risks:** native-X mirroring, bit order, old/new transition encoding and grayscale inversion.
- **Validation:** extend the existing driver test with byte-equivalent representative patterns, transitions and rotations. Preserve OTA checkpoints and cooperative scheduling. Measure packing separately before claiming benefit.

**2. Rotation-0 screenshot copy fast path**

- **Affected code:** `copyPresentedFrame`.
- **Benefit mechanism:** replace redundant reconstruction with a copy of the already packed completed frame.
- **Risks:** accidentally copying drawing/in-flight data or weakening snapshot invalidation.
- **Validation:** exact B&W/Gray4 output equivalence; capture refusal during interrupted presentation; retain existing rotated behavior.
- **Priority caveat:** useful primarily when screenshots are being requested.

### Changes requiring hardware A/B testing

**3. Cache preparation for consecutive SSD1677 B&W partials**

This is the most worthwhile controller-side experiment.

- Keep it board-local.
- Initially restrict it to consecutive successful SSD1677 B&W partials.
- Invalidate prepared state on reset, power loss, failure, cancellation and mode changes.
- Retain full cleanup and recovery paths.
- Do not assume SSD2677 supports the same optimization.

Measure preparation savings separately and verify unchanged-region integrity over repeated partials.

**4. Bounded multi-row SPI batches**

Use the existing bulk-write path, not a new graphics framework.

Potential benefit is reduced transaction/staging overhead—not less waveform time. Preserve cancellation granularity, watchdog fairness, CS semantics and safe transaction release. Avoid allocating another full controller framebuffer merely to batch writes.

**5. Cadence tuning, only after measurement**

The completion-based 500 ms interval can dominate perceived response after a previous update. However, it is intentional policy, not an accidental sleep. Do not lower it silently. First establish whether rendering, cooldown, preparation or waveform time is responsible for poor interaction.

### Not recommended initially or outside scope

- SSD2677 regional-transfer experiments.
- SSD1677 window narrowing before validating the conservative full-comparison behavior.
- Grayscale partial refresh.
- LUT, PLL, forced-temperature or undocumented-register tuning.
- Higher SPI clocks without panel qualification.
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
cmake --build build/host-tests --parallel 2 --target test_sticky_epaper_driver
ctest --test-dir build/host-tests --output-on-failure -R '^test_sticky_epaper_driver$'
```

The existing test passed. No source changes, flashing, firmware builds, commits or pushes were performed.

**Conclusion:** We already apply Seeed’s main applicable optimizations: B&W differential partial waveforms, previous/current comparison data, no-op suppression, cleanup refreshes, temperature-specific SSD2677 selection, vendor SPI clocks, and separate asynchronous presentation. We also avoid the Arduino examples’ sleep-after-each-update policy.

The best next steps are **byte-oriented packing**, the **rotation-0 screenshot copy**, and a **hardware-gated SSD1677 preparation-cache experiment**. There is no evidence-backed reason to change waveform values or SSD2677 scan geometry.