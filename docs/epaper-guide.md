---
title: E-Paper Guide
description: Detailed guide for the ESP32 Macropad e-paper device class, including hardware model, wake behavior, image refresh flow, portal configuration, and current limitations.
ms.date: 2026-06-05
ms.topic: concept
---

## Overview

The e-paper device class is the low-power, non-interactive branch of ESP32 Macropad.

Instead of rendering an LVGL user interface and waiting for touch input, an e-paper device wakes on demand, refreshes one or more configured full-screen images, and returns to deep sleep. The current board targets are the Soldered Inkplate 5V2, the Soldered Inkplate 6FLICK, and the Seeed reTerminal E1003.

This guide holds the detailed e-paper-specific material. Generic project docs, such as the README and changelog, stay intentionally high level so they do not become a second copy of the same evolving information.

## Current Scope

The current implementation targets two boards and one usage model:

| Board | SoC | Display | Decode |
|---|---|---|---|
| Inkplate 5V2 | ESP32 classic | 720 × 1280 3-bit grayscale | Inkplate library |
| Inkplate 6FLICK | ESP32 classic | 1024 × 758 3-bit grayscale | Inkplate library |
| Seeed reTerminal E1003 | ESP32-S3 | 1404 × 1872 16-level grayscale (IT8951) | G16P fast path, or JPEGDEC + dither, at native resolution |

* Interaction model: non-touch, battery-oriented dashboard
* Render model: fetch the current remote image slot, draw it, sleep

The reTerminal E1003 driver assumes the server delivers an image already at the panel's native resolution — there is no on-device scaling, by design, to keep the battery-powered refresh path fast. It accepts three transport formats:

* **G16P** (preferred) — a small magic-stamped header (`G16P`, version, width, height, payload length, CRC32) followed by the panel's 4&nbsp;bpp packed-nibble framebuffer. Because the server has already done the tone mapping and Floyd–Steinberg dithering, the firmware copies the nibbles straight into the framebuffer with no JPEG decode and no large working buffer — the fastest, lowest-power wake path. The optional `EPAPER_VERIFY_CRC32` compile flag (default off) checks the payload CRC before drawing.
* **G16Z** (compressed G16P) — a `G16Z` magic followed by a header-less (raw) DEFLATE stream of the complete G16P bytes. The server emits this when compression shrinks the payload, so the device pulls ~0.3–0.5× the bytes off WiFi and inflates it into a PSRAM buffer with the ROM's malloc-free tinfl before rendering the reconstructed G16P. On boards with the SD image cache, the **compressed** transport blob is what gets written back, so a later cache hit skips the re-download and reads only ~0.4 MB off the shared HSPI bus (vs ~1.3 MB for a full G16P) before re-inflating in PSRAM.
* **Baseline JPEG** — when the blob is neither G16P nor G16Z, the firmware decodes it with JPEGDEC straight into a 16-level grayscale framebuffer and applies Floyd–Steinberg dithering on-device. Progressive JPEGs are rejected up front, so the image endpoint must emit baseline JPEGs.

The shared web portal is still used for setup and configuration, but the runtime behavior is intentionally much simpler than the interactive display class.

## Runtime Model

```mermaid
flowchart TD
    Wake[Wake from timer or button] --> Mode{Boot path}
    Mode -->|Short button press| Force[Force refresh]
    Mode -->|Timer wake| Normal[Normal refresh]
    Mode -->|Long button hold| Config[Enter Config Mode]
    Mode -->|Cold boot with no URL| Config

    Force --> Wifi[Connect WiFi]
    Normal --> Wifi
    Wifi --> CrcEn{CRC change\ndetection on?}
    CrcEn -->|No| Draw[Draw remote image]
    CrcEn -->|Yes| Sidecar[Fetch image sidecar CRC]
    Sidecar --> Compare{CRC changed?}
    Compare -->|No| Sleep[Deep sleep]
    Compare -->|Yes| Draw
    Draw --> Panel[Refresh panel]
    Panel --> Sleep
    Config --> Portal[Run web portal until idle timeout]
    Portal --> Sleep
```

## Board Profile

The e-paper boards are materially different from the interactive boards in this repository.

* `HAS_EPAPER` is enabled
* `HAS_DISPLAY` is disabled
* `HAS_TOUCH` is disabled
* `HAS_BUTTON` is disabled
* `HAS_EPAPER_WAKE_BUTTON` is enabled
* MQTT remains enabled for shared config, health, and portal paths

That split is intentional. The e-paper board does not participate in the LVGL display stack, touch stack, widget stack, or image-fetch subsystem used by interactive boards.

## Image Refresh Pipeline

Each refresh cycle uses the same high-level sequence:

1. Connect to WiFi.
2. On cold boot only, kick SNTP and wait up to 2 s for the first time sync so the status overlay timestamp is meaningful on the very first refresh. The ESP32 RTC keeps wall-clock through deep sleep, so subsequent wakes skip the wait.
3. When **CRC change detection** is enabled (portal toggle, default off — see [Image and Sidecar Contract](#image-and-sidecar-contract)), fetch `<image-url>.crc32`.
4. Compare the sidecar value to the last successfully displayed CRC.
5. Skip the panel refresh when the CRC is unchanged, unless the refresh was explicitly forced. With change detection disabled, every scheduled wake redraws the panel.
6. Clear the framebuffer so the boot splash or low-battery screen does not bleed through at the configured rotation.
7. Draw the image with the board driver (Inkplate library on the 5V2; G16P direct copy or JPEGDEC → IT8951 framebuffer on the reTerminal E1003).
8. Composite the status overlay on top of the image.
9. Trigger the panel refresh.
10. Read battery voltage.
11. Store the new CRC after a successful update.
12. Put the panel to sleep.
13. Enter ESP32 deep sleep until the next wake.

On the Inkplate 5V2 and Inkplate 6FLICK, the image is fetched and decoded by the Inkplate library. On the reTerminal E1003, the firmware fetches the blob over HTTP(S): a G16P payload is copied straight into the 16-level grayscale framebuffer (no decode), while a baseline JPEG is decoded with JPEGDEC and Floyd–Steinberg dithered into the framebuffer. Either way the image is drawn at the panel's native resolution with no scaling.

A refresh is always forced (CRC skip is bypassed) when any of these is true:

* Cold boot — the panel currently shows only the boot splash, so the dashboard image must be drawn at least once before the CRC short-circuit becomes safe.
* Short press of the WAKE button — the user is actively looking at the panel.
* The portal `Refresh e-paper now` action.

## Image and Sidecar Contract

The current implementation expects public HTTP or HTTPS image URLs.

Supported image formats:

* PNG
* JPEG
* BMP

The change-detection sidecar is fetched from the same path with `.crc32` appended.

Change detection is controlled by the **Use CRC32 change detection** toggle on the Image Sources page (persisted to NVS, default off). When it is off, the firmware skips the `.crc32` fetch entirely and redraws the panel on every scheduled wake; when it is on, an unchanged CRC short-circuits the refresh to save a panel update and battery. A forced refresh (cold boot, WAKE button, or the portal `Refresh e-paper now` action) always bypasses the skip regardless of the toggle.

Examples:

```text
https://example.com/dashboard.png
https://example.com/dashboard.png.crc32
```

Sidecar body formats currently accepted:

* `0x12345678`
* `12345678` (hex, 8 chars)
* `305419896` (decimal)

If the sidecar fetch fails, times out, or returns unparseable content, the device treats the image as changed and proceeds with a normal refresh.

## Image Carousel

The portal can configure up to five image slots. Each slot has:

* A URL
* A per-slot Duration in seconds
* A Stay flag that keeps the same slot active when the refresh result does not change the content

Slots are filled top-to-bottom. Empty slots are skipped. A slot's Duration is the amount of time the device stays on that image before the next wake.

There is no separate global "wake every" control in the current UI. The wake-to-wake cadence is driven by the active slot's Duration. When only the first slot is populated, the board behaves like a single-image setup.

## Wake Button Behavior

The Inkplate wake button has two meanings:

* Short press from deep sleep: force an immediate refresh and bypass the CRC skip path
* Long press at boot or wake: enter Config Mode

The long-press threshold is 2.5 seconds.

A short-press refresh always wins over the hourly schedule. Even when the current local hour is disabled in the schedule (a timer wake at that hour would sleep without drawing), a button wake bypasses the schedule gate and refreshes the image, then resumes normal scheduled sleeping on the next timer wake.

Button wake classification is done once at boot. That prevents the refresh path and the config-mode path from re-interpreting the same physical press differently later in startup.

## Status Overlay

Every successful refresh composites a small status chip on top of the dashboard image before the panel-drive waveform runs, so the overlay is always in sync with the image being refreshed.

* **Font** &mdash; the overlay uses the medium font (Inter 12 pt). The smallest font face (Inter 8 pt) stays compiled in for future high-DPI boards but is not readable on the Inkplate 5V2 panel.
* **Background** &mdash; a filled rounded-rectangle chip with no outline; on a panel this size the outline made the overlay feel boxed-in, while the fill on its own already reads as a chip.
* **Items** &mdash; configurable bitmask covering battery icon, battery percentage, wall-clock time, and last cycle duration. The fields appear in the order listed and only the enabled items are drawn.
* **Position** &mdash; one of the four panel corners, set via the E-Paper portal page.
* **Time field** &mdash; sourced from the ESP32 RTC, which is seeded by SNTP on the first cold boot after a power cycle and then carried forward across deep sleep. If the clock has not yet synced (very first wake on a flaky network), the time field falls back to a placeholder.

## VCOM Calibration

The Inkplate's TPS65186 PMIC stores a panel-specific VCOM bias voltage in its on-board EEPROM. The value is printed on the e-paper ribbon cable and only needs to be set once per device.

The portal exposes three actions under the VCOM card on the E-Paper page:

* **Read current VCOM** &mdash; queries the TPS65186 and shows the value currently programmed into EEPROM.
* **Show test pattern** &mdash; draws an all-grey-levels calibration pattern so you can compare candidate VCOM values visually. If the input field has a value, the pattern is drawn with that value written to the TPS65186 *volatile* registers only (EEPROM is not touched, and the value reverts on the next panel power cycle). With the input empty, the pattern is drawn with the EEPROM-programmed value.
* **Write VCOM (programs EEPROM)** &mdash; commits the value in the input to the EEPROM. The TPS65186 EEPROM is rated for roughly 100,000 program cycles, so this should only be done when the new value visibly improves image quality.

The preview path is intentionally non-destructive: experiment freely with `Show test pattern`, then only `Write VCOM` once you have picked a final value.

## Status Screens

The e-paper flow now renders explicit full-screen status screens in lifecycle moments where users otherwise only see a stale image.

* Boot splash appears on cold boot and on button wakes before WiFi work starts, so the device gives immediate feedback that a wake was accepted.
* Configuration mode screen is drawn when entering AP or STA recovery portal mode, showing SSID and URL/IP details needed to open the portal.
* Error screen appears when refresh fails, with a human-readable reason and retry timing guidance.
* Low-battery screen appears when battery reads below 3.2 V before WiFi connect, then the device sleeps for 600 seconds to reduce brownout churn.

## Frontlight Behavior

Frontlight controls are present only on boards that compile with `HAS_EPAPER_FRONTLIGHT=true`.

* Brightness range is 0 to 63.
* Duration is in seconds.
* Frontlight runs on button wakes only, not on timer wakes.
* A value of 0 brightness disables frontlight output.

The Inkplate 5V2 board keeps `HAS_EPAPER_FRONTLIGHT=false`, so the card stays hidden there.

## E-Paper Endpoints

E-paper component endpoints are split across two components: `epaper-status` and `epaper-vcom`.

* `POST /api/component/epaper-status/refresh` triggers an immediate refresh attempt.
* `GET /api/component/epaper-status/status` returns latest refresh counters, timing, battery, and last outcome fields.
* `GET /api/component/epaper-vcom/vcom` reads the current programmed VCOM.
* `POST /api/component/epaper-vcom/vcom` programs VCOM to TPS65186 EEPROM.
* `POST /api/component/epaper-vcom/vcom-test-pattern` draws the grayscale calibration pattern and optionally previews a volatile VCOM candidate via query `?vcom=-X.XX`.

## Portal Configuration Model

On e-paper boards, the web portal uses a dedicated **E-Paper** category as the primary landing area. The category contains five pages:

* **Status** &mdash; read-only refresh counters, timing, battery, and last outcome, plus a `Refresh e-paper now` action. Auto-refreshes every 5 seconds.
* **Image Sources** &mdash; up to 5 image slots with per-slot Duration and Stay flags, plus the hourly refresh window and timezone offset.
* **WiFi Failure Backoff** &mdash; WiFi retry sleep cap after repeated failures.
* **Status Overlay** &mdash; overlay enable, corner position, color, and per-field bitmask (battery, percentage, time, duration).
* **VCOM** &mdash; one-time TPS65186 EEPROM calibration controls.

The operating mode is not exposed as a separate page on e-paper boards. The Image Sources and Status Overlay pages each write the hidden `operating_mode=duty_cycle_epaper` field on save so the board cannot drift into an unrelated transport mode through normal portal use.

## Status Semantics

The status card mixes NVS-backed values, RTC-retained values, and last-attempt values. That distinction matters when interpreting what the page shows.

### Last Refresh

The last refresh timestamp is stored in RTC-retained memory after a successful image update, but only when the clock is valid.

That means it:

* Survives deep sleep
* Does not survive full power loss or hard reset
* Cannot be computed meaningfully until the device clock is synced

### Successful Refreshes

The refresh counter is also stored in RTC-retained memory.

It counts successful panel updates, not timer wakes, not skipped CRC checks, and not failed draw attempts.

### Last Draw Result

The last result reflects the most recent refresh attempt in the current boot context.

Current values:

* `updated`
* `skipped`
* `fetch_failed`
* `draw_failed`
* `disabled`

### Last Sidecar HTTP

This field reports the final HTTP status returned by the `.crc32` fetch path.

Typical values:

* `200` when the sidecar exists and was fetched successfully
* `404` when no sidecar file exists
* `0` when the request failed before a usable HTTP response was received

The portal currently renders `0` as `N/A`.

### Last Image CRC

The CRC shown in the portal is the last successfully committed image CRC from NVS. It represents the last known displayed image identity, not necessarily the most recent sidecar body when a refresh failed.

## Power and Telemetry

The e-paper duty cycle measures and retains a per-wake timing budget in RTC memory so the portal can show "last cycle" numbers and MQTT can publish them on the next wake.

### Wake Modes

The e-paper device wakes from either the RTC timer or the WAKE button. The current UI no longer exposes a separate global "wake every" control; the wake cadence is driven by the active slot's Duration.

* Slot Duration controls how long the device sleeps after showing that slot.
* The schedule can still disable refreshes for selected local hours. A WAKE-button press overrides the schedule and refreshes anyway; only timer wakes honor the disabled hours.
* Button-only operation remains possible when the timer wake is intentionally disabled in firmware behavior.

### Sleep-Time Compensation

After each cycle the firmware subtracts the active loop duration from the planned slot Duration so the wake-to-wake cadence stays close to the configured value. The planned sleep time is logged before deep sleep, including the wake timestamp when the clock is valid.

### Battery Reporting

The battery voltage is read before the panel-drive waveform sags the cell, on both the "updated" and "skipped" code paths. The portal renders it as both volts and a 0&ndash;100&nbsp;% linear estimate derived from a 3.00&ndash;4.20&nbsp;V range.

### MQTT Telemetry

When MQTT is configured, every wake publishes a retained JSON document to `<base>/epaper/state` with the following shape:

```json
{
  "battery_mv": 3870,
  "battery_pct": 75,
  "wifi_rssi": -62,
  "image_crc32": 305419896,
  "refresh_result": "updated",
  "refresh_count": 42,
  "sidecar_http_status": 200,
  "timing": {
    "boot_to_wifi_ms": 2310,
    "crc_retry_count": 1,
    "crc_to_draw_ms": 4820,
    "draw_to_mqtt_ms": 180,
    "total_active_ms": 7820,
    "last_elapsed_ms": 5100
  }
}
```

The MQTT connect attempt is bounded at 5 s. A clean DISCONNECT is sent before deep sleep to avoid spurious LWT messages on the broker.

### Home Assistant Auto-Discovery

Twelve sensor entities are auto-discovered into Home Assistant, all reading from the JSON state topic above:

| Entity                          | JSON field                       | Unit |
|---------------------------------|----------------------------------|------|
| Battery                         | `battery_pct`                    | %    |
| Battery Voltage                 | `battery_mv`                     | mV   |
| E-Paper Refresh Count           | `refresh_count`                  |      |
| E-Paper Last Refresh Result     | `refresh_result`                 |      |
| E-Paper Image CRC               | `image_crc32` (formatted as hex) |      |
| E-Paper Sidecar HTTP Status     | `sidecar_http_status`            |      |
| E-Paper Wake Loop Time          | `timing.total_active_ms`         | ms   |
| E-Paper Boot to WiFi            | `timing.boot_to_wifi_ms`         | ms   |
| E-Paper CRC to Draw             | `timing.crc_to_draw_ms`          | ms   |
| E-Paper Draw to MQTT            | `timing.draw_to_mqtt_ms`         | ms   |
| E-Paper Refresh Elapsed         | `timing.last_elapsed_ms`         | ms   |
| E-Paper CRC Fetch Attempts      | `timing.crc_retry_count`         |      |

WiFi RSSI is intentionally not duplicated &mdash; the generic `WiFi RSSI` entity from the shared health discovery already updates on every wake.

Discovery configs are retained on the broker, so they are only published on cold boot. An `RTC_DATA_ATTR` flag suppresses the entire discovery burst (health + e-paper) on subsequent warm wakes to save battery.

## Config Mode

Config Mode uses the same portal idle-timeout mechanism as the rest of the project.

On the Inkplate board, the default portal idle timeout is 300 seconds. That gives you enough time to join the access point, configure WiFi, and set the image URL without leaving the device awake indefinitely on battery power.

## SD Image Cache

The SD image cache is a **device-class capability** for e-paper boards that expose a microSD slot on the *same* SPI bus as the panel controller. It is gated by the `EPAPER_SD_CS_PIN` compile-time flag and lives in the shared `epaper/epaper_sd_cache` module, so any future e-paper board can opt in from its `board_overrides.h` without touching a driver. Among the current targets only the reTerminal E1003 qualifies; the Inkplate 5V2 has no shared-bus SD slot, so the entire cache is compiled out there.

It is a downloaded-blob cache, not a generic image store. It caches the original transport blob the publisher served — a compressed **G16Z** wrapper when the server sent one, or a raw **G16P** framebuffer otherwise. A cache hit skips the re-download and reads the small blob off the card (~0.4 MB for G16Z vs ~1.3 MB for G16P), then re-inflates in PSRAM if needed, which is far cheaper than the extra SD read a full-size frame would cost. The entry is keyed by the content-stable image id parsed from the publisher's redirect URL and stored on the card as `/cache/<id>.g16z`.

How a cached refresh works:

1. The firmware resolves the image redirect to a blob URL and content id **without** downloading the ~1.3&nbsp;MB body.
2. On a cache **hit**, the blob is read straight from SD and the slow HTTP body download is skipped entirely &mdash; the main battery win.
3. On a cache **miss**, the blob is downloaded over HTTP(S), drawn, and then staged in PSRAM. The write-back to SD happens on the awake tail (after the panel is already showing the image), so the ~1&ndash;2&nbsp;s card write does not sit in the wake-to-visible path.

Because the microSD card and the IT8951 panel share one SPI bus, the cache never calls `SPI.begin()` itself &mdash; the driver owns the bus. After every `SD.end()` the cache module invokes a driver-supplied `restore_panel_bus()` callback that re-initializes the panel's SPI bus and chip-select, so an SD-cache hit followed by a panel refresh draws a clean image rather than garbage.

The cache is controlled from the portal:

* A **Cache images on SD** toggle on the Image Sources page enables or disables it (persisted to the NVS key `ep_sd_en`, default off). The toggle row is shown only when the build reports the `epaper_sd_cache_supported` capability &mdash; it is hidden on the Inkplate 5V2.
* A **Clear SD cache** action deletes every cached blob; the next refresh repopulates the card.

## Current Limitations

The e-paper device class is intentionally narrow in this first version.

Current limitations include:

* Public image URLs only
* Full refresh only, no partial-update pipeline
* No offline image fallback when the network is unreachable (the SD blob cache speeds up repeated images on boards with a shared-bus microSD slot, but is not an offline carousel)
* No touch UI runtime
* Carousel slots all point to remote URLs; the SD cache stores only previously fetched blobs, not user-managed local images
* Hourly schedule uses a fixed UTC offset rather than full timezone rules (DST must be adjusted manually)

Those constraints keep the runtime predictable and power efficient while the device class matures.

## Documentation Strategy

When new e-paper capabilities land, update this guide first.

Keep the generic markdown files high level:

* `README.md` should explain what the e-paper class is and where it fits
* `CHANGELOG.md` should summarize what changed
* This guide should hold the board-specific behavior, wake semantics, status-field meaning, and image-sidecar contract

That split keeps the general docs readable while leaving room for the e-paper feature set to grow over time.