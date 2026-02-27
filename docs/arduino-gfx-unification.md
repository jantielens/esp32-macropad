# Arduino_GFX Unification Research (JC3248W535 + Other Boards)

This document captures research performed while adding support for the **JC3248W535** board and evaluating whether we could simplify the codebase by using **Arduino_GFX** for *all* displays.

## Context

- Repo: `esp32-template-wifi`
- Branch context: JC3248W535 support introduced an Arduino_GFX-based display driver.
- Installed Arduino_GFX library:
  - **GFX Library for Arduino v1.6.2** (Moon On Our Nation)

## Current Boards / Display Stacks

### JC3248W535 (ESP32-S3, 320×480)
- Display controller: **AXS15231B**
- Bus: **QSPI** (ESP32-S3 QSPI)
- Current approach in this repo: **PSRAM framebuffer** (`renderMode() == Buffered`) with driver-level pixel rotation in `pushColors()`. The panel stays in portrait; `present()` sends dirty rows 0..maxDirtyRow via `gfx->draw16bitRGBBitmap(0, 0, fb, w, rows)` — always starting at (0,0) because QSPI address windows are lost on CS toggle.
- **History:** Originally used `Arduino_Canvas` as a buffered backend, replaced in issue #65 with a leaner PSRAM framebuffer (no Canvas object or GFX drawing API overhead) and dirty-row tracking.

### CYD v2 / v3 (ESP32-2432S028R, 320×240)
- Display controller: **ILI9341-class** (SPI)
- Current approach: **TFT_eSPI**
- Notes:
  - CYD v2/v3 require a gamma/inversion workaround today (see existing display driver fixes).

### ESP32-C3 Super Mini + ST7789 panel (240×280)
- Display controller: **ST7789V2-class** (SPI)
- Current approach: **Removed** (board and driver discontinued in v0.0.56).
- Notes:
  - This driver was removed along with the esp32c3-waveshare-169-st7789v2 board target.

## Key Question

> If all boards/displays used Arduino_GFX, would that simplify the codebase?

Answer: **Possibly, but not automatically**.

Using one graphics library can reduce “library surface area”, but you still need to handle:
- per-panel init quirks (gamma, inversion, offsets)
- performance differences (SPI vs QSPI, small-rect flush patterns)
- memory tradeoffs (canvas/framebuffer vs direct streaming)

In other words: unifying the *library* doesn’t remove the need for *board-specific behavior*; it mostly changes where that behavior lives.

## LVGL Flush Callback Models

### Model A — Direct/Streaming (typical for SPI)
- LVGL flush callback immediately:
  - sets window
  - streams pixels
  - returns
- Pros:
  - minimal extra RAM
  - usually fastest for SPI TFTs
- Cons:
  - LVGL may flush many small rectangles; overhead depends on bus/driver

### Model B — Buffered/Canvas + Present Step
- LVGL flush callback writes into a framebuffer/canvas.
- A separate `present()` step pushes the buffered result to the display.
- Pros:
  - can match vendor/sample recommended flows
  - can reduce flicker/tearing depending on panel stack
- Cons:
  - higher RAM usage
  - extra copy/flush overhead

## Verification: Does Arduino_GFX Support Direct LVGL Flush for AXS15231B QSPI?

We inspected Arduino_GFX v1.6.2 sources for the AXS15231B panel and the ESP32 QSPI databus.

### Findings

- The panel class **does** implement a window primitive:
  - `Arduino_AXS15231B::writeAddrWindow(x, y, w, h)`
  - It programs `CASET`, `RASET`, then issues `RAMWR`.

- The QSPI bus class **does** implement pixel streaming:
  - `Arduino_ESP32QSPI::writePixels(uint16_t *data, uint32_t len)`
  - It repacks RGB565 pixels into a DMA-aligned buffer and transmits in chunks.
  - Chunk size is limited by `ESP32QSPI_MAX_PIXELS_AT_ONCE` (defaults to **1024** pixels).

### What this means

- AXS15231B QSPI is **not fundamentally incompatible** with a direct/windowed LVGL flush callback.
- A direct flush driver is **technically feasible** using:
  - `writeAddrWindow()` (panel)
  - `writePixels()` (bus)

### Why the canvas-based approach may still be preferable

Even though direct flush is possible, it may not be the best default:
- LVGL’s small-rectangle flush cadence can amplify overhead.
- QSPI `writePixels()` repacks into a DMA buffer each chunk; many small flushes can cost CPU.
- The canvas approach centralizes presenting to a controlled cadence (after `lv_timer_handler()`), matching sample/vendor patterns.

## “Clean” Architecture Recommendation

If we decide to pursue Arduino_GFX unification, the cleanest structure is:

1. Keep the existing `DisplayDriver` HAL as the stable interface to LVGL.
2. Provide **two Arduino_GFX-backed implementations**:

   - **Arduino_GFX SPI (direct/streaming)**
     - Targets: ILI9341 (CYD), ST7789 (ESP32-C3)
     - Implements LVGL flush by streaming pixels directly.
     - Uses `renderMode() == Direct` and keeps `present()` as a no-op.
     - Re-implements CYD gamma/inversion and ST7789 offset quirks in `applyDisplayFixes()` and/or panel setup.

   - **Arduino_GFX QSPI (buffered/canvas)**
     - Targets: AXS15231B (JC3248W535)
     - Uses canvas/framebuffer-like behavior (`renderMode() == Buffered`) and a real `present()`.

This avoids forcing all boards into a higher-RAM buffered model, while still allowing Arduino_GFX to be the common library.

## Decision Checklist (Future)

Before switching any existing board from TFT_eSPI/native drivers to Arduino_GFX:

- Confirm correctness:
  - gamma/inversion matches current behavior (CYD)
  - offsets/color order match current behavior (ST7789 module)
- Confirm performance:
  - UI responsiveness and frame rate
  - CPU usage impact
- Confirm memory:
  - LVGL buffers and any canvas buffers fit target RAM/PSRAM

## Suggested Next Steps (Optional)

- Prototype an Arduino_GFX-based **SPI direct** driver for CYD + ST7789 to evaluate simplification impact.
- The Canvas has been replaced with a leaner PSRAM framebuffer (issue #65) — same memory footprint but avoids the Canvas object overhead. True direct QSPI partial writes are impossible due to CS-toggle address-window loss.
