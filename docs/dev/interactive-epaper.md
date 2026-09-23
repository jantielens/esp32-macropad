---
title: Interactive E-Paper Developer Guide
description: Add an always-on LVGL e-paper board with correct refresh ownership, configuration, and validation.
ms.date: 2026-09-23
ms.topic: how-to
---

## Scope

Interactive E-Paper is the always-on display profile for boards that run the
normal LVGL application on an e-paper panel. It is distinct from the
sleep-first [E-Paper Frame](../epaper-frame-guide.md) product class.

Set these board flags:

```cpp
#define HAS_EPAPER_PANEL true
#define HAS_DISPLAY true
#define HAS_LVGL_EPAPER true
```

Do not set `IS_EPAPER_FRAME` for an interactive board. That identity owns the
image-refresh-and-sleep lifecycle and excludes the LVGL display stack.

## Driver Contract

An interactive e-paper driver returns `DisplayDriver::RenderMode::Buffered`.
LVGL flushes write to its drawing framebuffer. `present()` decides when to run a
physical panel waveform.

The driver must meet these rules:

* Allocate large framebuffer snapshots in PSRAM and handle allocation failure.
* Protect drawing-buffer writes with a mutex.
* Copy the drawing framebuffer into a presentation snapshot while holding the
  mutex, then release the mutex before the waveform begins.
* Preserve writes that arrive during a waveform so the next presentation is not
  lost.
* Coalesce updates with a minimum refresh interval and a settle interval.
* Skip identical frames when the panel state is already known.
* Keep B/W partial-refresh accounting separate from full-refresh accounting.
* Implement `requestFullRefresh()` for the display-refresh action when the
  hardware supports it.

The [Display and Touch Architecture](display-touch-architecture.md) documents
the common DisplayDriver and Arduino compilation-unit contracts.

## Configuration Contract

Interactive drivers consume generic e-paper configuration, never vendor-named
configuration. Use `EpaperRefreshSettings` and the `EPAPER_*` constants:

* `EPAPER_RENDER_MODE_BW` and `EPAPER_RENDER_MODE_GRAYSCALE`
* `EPAPER_DEFAULT_RENDER_MODE`
* `EPAPER_MIN_REFRESH_INTERVAL_MS`
* `EPAPER_REFRESH_SETTLE_MS`

The portal exposes one shared passive binding interval, minimum presentation
interval, and full-refresh threshold for both modes. The render mode is
boot-only; refresh cadence and threshold apply live through the synchronized
configuration snapshot. Inkplate grayscale always uses full waveforms, so its
full-refresh threshold has no effect in that mode.

## Inkplate 6FLICK Profile

`inkplate6flick-interactive` is an Interactive E-Paper target with a 3 MB,
no-OTA application partition. Review its binary size before enabling additional
features or libraries.

The render mode selects the Inkplate library framebuffer at boot:

* **B/W** uses the 1-bit framebuffer. The driver converts RGB565 pixels with a
   stable 4x4 Bayer dither. `partialUpdate()` is available only in this mode;
   the first presentation and scheduled ghosting-control refreshes use a full
   waveform. The Inkplate library's partial update still scans the entire
   panel; it does not provide regional transport.
* **Grayscale** uses the 3-bit framebuffer. The library cannot perform a
   partial update in this mode, so every physical presentation is a full
   waveform.

The driver owns the B/W full-refresh cadence. Do not use InkplateLibrary's
automatic full-update threshold because its nonzero value suppresses the next
partial waveform. The driver compares the mode-specific drawing and presented
framebuffers, skipping no-op physical updates before it starts a waveform.

The frontlight is independent of panel presentation. Brightness `0` switches it
off without changing the panel state; nonzero brightness maps to the controller's
63-step range. This board uses `SCREENSAVER_BACKLIGHT_ONLY` and disables display
animations to avoid wasting slow panel waveforms on transient states.

## reTerminal E1003 Profile

`reterminal-e1003-interactive` is the normal Macropad profile for the Seeed
reTerminal E1003. It retains the board's 32 MB flash and OTA partition while
using a PSRAM-backed 4-bit grayscale drawing buffer and a second snapshot for
physical presentation.

The driver uploads the snapshot through the IT8951 and releases its framebuffer
mutex before starting a waveform. Both modes union LVGL flush rectangles and
align the resulting native-panel region for regional uploads. B/W refreshes the
region with DU; grayscale uses regional GC16. The initial presentation and
forced refreshes use full-panel GC16, as do scheduled refreshes in both modes.
The E1003 firmware requires the IT8951 VCOM write selector `0x0002`;
selector `0x0001` only reads the configured value. Regional grayscale GC16
requires hardware validation for visual quality and update timing.

The GT911 touch controller is at `0x5D` on GPIO19/GPIO20, with GPIO2 interrupt
and GPIO16 reset. It shares the physical I2C lines with the SHT4x and RTC, but
uses the project's `Wire1` path to avoid Wi-Fi ISR contention on `Wire`.

## Inkplate Touch Contract

The Inkplate Cypress controller is event-driven, not level-polled. Its
`getData()` result of zero can mean either an explicit release report or that no
event is pending. The touch driver must read the controller only when an event
is pending, retain the most recent contact state between events, and release the
LVGL input state only after an explicit zero-finger report. Serialize controller
reads so another touch observer cannot consume a one-shot report.

## Board Integration Checklist

1. Create a board directory named `<hardware>-interactive` with its override
   header and metadata.
2. Add the FQBN and partition choice to `config.sh`.
3. Add a display-driver selector, constructor branch, header selection, and
   conditional include in `display_drivers.cpp`.
4. Add an optional touch-driver selector, constructor branch, header selection,
   and conditional include in `touch_drivers.cpp`.
5. Update the generated board-to-driver table:

   ```bash
   python3 tools/generate-board-driver-table.py --update-drivers-readme
   ```

6. Regenerate embedded portal assets after changing web sources:

   ```bash
   ./tools/minify-web-assets.sh
   ```

7. Keep all Interactive E-Paper-only code gated by `HAS_LVGL_EPAPER`: driver
    aggregation, portal component and assets, full-refresh action, and REST
    route. Extend the action-catalog and asset-variant tests when adding one of
    those surfaces.

## Validation

Build the board and exercise both render modes on hardware. Verify that touch
continues to work while a waveform is pending, a full refresh can be queued,
and changes made during a waveform appear on the next refresh. Confirm that
failed snapshot allocation reports the display as unavailable instead of using a
partially initialized driver.

For Inkplate 6FLICK, also verify these hardware behaviors:

* B/W partial-refresh latency and ghosting at the configured binding cadence
   and full-refresh threshold
* Grayscale full-waveform coalescing and no-op skips
* Edge, hold, and rapid-sequence touch behavior, including a wake touch followed
   by a normal pad-button touch
* Repeated pad saves, icon replacement, and deletion while monitoring free and
   largest-block PSRAM values
* Notification, alert, gauge, bar-chart, and first-pad-entry rendering so only
   meaningful final states reach the panel