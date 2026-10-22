---
title: Interactive E-Paper Developer Guide
description: Add an always-on LVGL e-paper board with correct refresh ownership, configuration, and validation.
ms.date: 2026-10-16
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
boot-only; refresh cadence and full-refresh threshold apply live through the synchronized
configuration snapshot. Inkplate grayscale always uses full waveforms, so its
full-refresh threshold has no effect in that mode.

B/W conversion is also boot-only: `epaper_bw_conversion` selects `threshold`
(default), `bayer4x4`, or `blue_noise`. `epaper_bw_threshold` ranges from 1 to
255, defaults to 128, and affects only threshold conversion. Drivers cache
these settings at startup and use the shared `monochrome_is_white()` helper
with logical screen coordinates, so partial flushes cannot shift a pattern.
Grayscale, E-Paper Frame rendering, and monochrome uploaded-font rasterization
are independent of these settings.

`epaper_vcom_mv` is a separate boot-only setting stored for all interactive
e-paper boards, including those whose drivers ignore it. It is an unsigned
magnitude from 0 to 5000 mV; `0` selects `EPAPER_DEFAULT_VCOM_MV` in consuming
drivers rather than applying zero volts. Keep the saved zero unchanged so a
later firmware/driver profile uses its own board default. Nonzero overrides
remain explicit panel calibration and must be checked when swapping panels.
The board default is `0` (unspecified) unless overridden. IT8951 consumes this
setting during initialization and rejects a missing voltage. No driver
capability API or live voltage changes are involved. The portal always shows
VCOM in negative volts; its default button saves the zero selection.

The blue-noise map in `drivers/blue_noise_64.h` is a precomputed, periodic
64x64 void-and-cluster rank map. Generation uses a 10% seeded initial pattern
(Python `random.Random(1400).sample(range(4096), 409)`), a toroidal Gaussian
kernel with sigma 1.5 and radius 7, cluster/void relaxation, and three rank
phases: removing clusters to zero, filling voids to half, and removing clusters
from the complemented half-pattern. Dividing each rank by 16 gives 256 levels
with 16 pixels each. The 4 KiB table is read-only and gated by
`HAS_LVGL_EPAPER`; firmware needs neither a generator nor random state.

`HAS_EPAPER_GRAYSCALE` defaults to the existing Interactive E-Paper capability.
Monochrome panels override it to `false`. `GET /api/config` advertises
`epaper_supported_render_modes`; unsupported persisted modes normalize to the
board default, and API writes reject unsupported modes.

## Waveshare 7.8 Interactive

The [board reference](../../src/boards/waveshare-7.8-interactive/README.md)
covers physical connections, button recovery, and flashing.

Build `waveshare-7.8-interactive` for the Unexpected Maker FeatherS3[D], with
16 MB flash, 8 MB QSPI PSRAM, native USB CDC, and the Waveshare 7.8" IT8951
HAT. This is an always-on, non-touch Macropad profile using normal pads and
widgets, not the sleep-first E-Paper Frame lifecycle.

The display-based target name does not change the FeatherS3[D] board
definition or reference wiring; other ESP32 carriers need matching board settings.

```bash
./tools/install-custom-partitions.sh
./build.sh waveshare-7.8-interactive
```

The `ota_4mb_16MB_ext` layout provides two 4 MiB OTA application slots,
7.625 MiB LittleFS storage, and a 256 KiB native Extensions partition.
The three slots hold 56 KiB, 56 KiB, and 120 KiB of usable ELF data.
Use signed ESP32-S3 (`-s3.ext`) packages and prefer slow-changing widgets.
SD storage, USB/BLE HID, audio, and battery telemetry remain disabled.

Back up settings and files before migrating from the earlier `default_16MB`
layout. A full USB flash must install the new partition table; initialize
the new filesystem and restore the backup afterward. Firmware-only OTA
does not migrate partition boundaries.

| Signal | Feather GPIO | Connection |
|---|---|---|
| HAT SCK / MOSI / MISO | 36 / 35 / 37 | Dedicated SPI bus |
| HAT CS / RESET / HRDY | 10 / 38 / 5 | HRDY LOW means busy |
| Button | 14 | Normally-open contact to GND, active LOW |
| HAT 5V / GND | External 5 V / common GND | Always powered, no GPIO17 boost control |

Do not feed the HAT from the Feather's 3.3 V regulator. Use 3.3 V logic and a
common ground. The panel-specific VCOM default is -1.53 V (`EPAPER_DEFAULT_VCOM_MV=1530`);
confirm the panel label before substituting another panel. GPIO17 is unused.
The default is landscape 1872 x 1404 with rotation 2 for the confirmed mounting.
The button has no default tap or hold actions; assign these in the portal.
Existing boot-time configuration-button behavior remains available.

Transport and startup follow the working
[photoframe implementation](https://github.com/jantielens/esp32-it8951-photoframe/tree/1a70d2cf215310951f6631467387c1c399bf2bd9):
200 ms reset LOW and HIGH delays, 24 MHz SPI writes, 1 MHz reads, I80 packed
mode, big-endian 4-bit uploads, and VCOM selector 1 with readback verification.
No GxEPD2 dependency is added to the interactive driver. The shared
`IT8951EpaperDriver` retains the E1003's separate packing, power, VCOM selector,
SPI speed, and temperature behavior through its board configuration.

Grayscale is the default; selecting B/W in the existing e-paper settings takes
effect after reboot. B/W uses regional DU waveforms; grayscale uses regional
GC16. Both modes perform full-panel GC16 on first presentation, explicit full
refresh, and the configured cleanup threshold (default 10 regional updates).
The initial cadence is a 500 ms minimum presentation interval, 150 ms settling,
and 1000 ms passive binding interval. These are scheduling intervals, not a
promise of measured panel latency. Regional grayscale ghosting and response
time require hardware validation.

For bring-up, check cold boot and USB diagnostics, asymmetric corner labels
and grayscale bars in landscape, small text/widget updates in both modes,
repeated pad navigation, a forced full refresh, and saved button tap/hold
actions. Check cleanup after repeated regional updates and OTA followed by
reboot. Host transport checks do not establish physical image quality.

## CrowPanel 4.2 Profile

Build `crowpanel-4.2-interactive` for the ESP32-S3-WROOM-1-N8R8 with 8 MB flash,
8 MB OPI PSRAM, and the 400 x 300 B/W A-revision panel. The profile uses the 3 MiB
OTA layout with LittleFS, hardware-UART serial, no touch, and no SPI-SD support.
Run `./tools/install-custom-partitions.sh` if the ESP32 core does not yet know
the `ota_3mb_8MB` partition option, then `./build.sh crowpanel-4.2-interactive`.

The [Elecrow wiki](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-paper_4.2-inch_HMI_Display.html),
its [Arduino examples](https://www.elecrow.com/download/product/CrowPanel/E-paper/4.2-DIE07300S/Arduino/Examples.zip),
and [hardware files](https://www.elecrow.com/download/product/CrowPanel/E-paper/4.2-DIE07300S/CrowPanel-ESP32-Display-4.2E-Inch.zip)
are the reference for wiring. Controller commands follow the separate
[A-revision examples](https://github.com/Elecrow-RD/CrowPanel-ESP32-4.2-E-paper-HMI-Display-with-400-300/tree/master/example/arduino_A_green_circular_sticker_on_the_back).
V1.2A factory firmware was confirmed working on a unit whose PCB is marked V1.2;
the PCB marking alone does not distinguish the panel protocol. This profile
targets that A-revision protocol, not the older SSD1683 examples.

| Signal | GPIO | Behavior |
|---|---|---|
| EPD SCK / MOSI / CS | 12 / 11 / 45 | SPI mode 0 |
| EPD DC / RESET / BUSY | 46 / 47 / 48 | Driver treats BUSY LOW as active |
| EPD power | 7 | HIGH enables the panel |
| Menu / Back | 2 / 1 | Active LOW contacts |
| Rotary Up / Down / OK | 6 / 4 / 5 | Three active LOW contacts, not quadrature |
| Boot | 0 | Active LOW boot strap |

The board overrides own the panel wiring through `EPAPER_SPI_SCK_PIN`,
`EPAPER_SPI_MOSI_PIN`, `EPAPER_CS_PIN`, `EPAPER_DC_PIN`, `EPAPER_RESET_PIN`,
`EPAPER_BUSY_PIN`, and `EPAPER_POWER_PIN`. The driver uses these definitions
rather than hardcoded GPIO assignments.

Reset connects to ESP32 EN and is not a configurable GPIO input. The SD slot
uses SPI GPIO39/40/13/10 and power GPIO42; the existing SD_MMC storage backend
cannot drive it. This profile deliberately does not advertise SD support.

### Monochrome Uploaded Fonts

`HAS_FREETYPE_FONTS` defaults to `HAS_TTF_FONTS && HAS_LVGL_EPAPER`.
On every eligible interactive e-paper board, enabled builds use FreeType 2.13.2
for uploaded fonts when the active driver presents in B/W mode. Grayscale
retains LVGL/STB. Set the flag to `0` in a board override to select STB for both
modes. There is no separate runtime font backend setting. Built-in Montserrat
fonts are unchanged. B/W conversion is configured independently of the font
backend.

The monochrome adapter loads outlines with
`FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_MONO | FT_LOAD_NO_BITMAP`, renders with
`FT_RENDER_MODE_MONO`, and expands packed pixels into A8 coverage containing
only 0 and 255. Existing fallback fonts and metrics corrections still apply.
The active presentation mode participates in the font-instance cache key.

Each instance owns a FreeType library and face and caches up to 16 glyphs.
Drawing pins bitmap buffers until LVGL releases them. FreeType allocations use
the PSRAM-first LVGL allocator, with internal RAM fallback. Failed allocations
invalidate the face and library; the next glyph request recreates them before
retrying. Cached bitmap buffers remain independent and valid while pinned.
Initial instance-creation failures use the existing cached bitmap fallback.
OTA guards block new font resolution and glyph work while an update is active.

The pinned minimal library, license notices, provenance, and heap-workspace patch
live in `libraries/MacropadFreeType`. `build.sh` discovers this project-local
library without a download, global installation, or temporary symlink. See
[Library Management](library-management.md#project-local-freetype).
The patch moves large auto-hinter and monochrome raster workspaces to checked
per-call heap allocations. It preserves the rendering algorithms and pool size.
The adapter requires its `FT_AutoMono_Heap_Workspaces` marker; an unpatched
library cannot silently link against the original 16 KiB LVGL stack.

Host tests cover mode selection, grayscale/STB parity, binary monochrome pixels,
Latin and Devanagari allocation-failure retries, pinned cache eviction, and
cleanup. Hardware testing confirmed expected rendering and navigation with the
production integration. Actual task stack watermark, peak internal/PSRAM use,
latency, and OTA stress need separate measurements; successful functional
testing alone does not establish them. Capture
`/api/screenshot?format=png` after the physical refresh settles to inspect the
presented B/W or grayscale pixels on CrowPanel, Inkplate 6FLICK, and IT8951
interactive displays. The PNG captures the digital presentation data and does
not show optical artifacts such as ghosting.

### Panel Refresh Implementation

The driver keeps two 15,000-byte PSRAM buffers, converts RGB565 with the selected
B/W conversion (threshold 128 by default), and applies the A-revision sample's controller sequence:
RESET HIGH 10 ms, LOW 100 ms, HIGH 100 ms; initialization without a pre-command
BUSY wait; white RAM planes `0x10`/`0x13` and GC waveforms for a full clear;
then a second reset/initialization and the image through `0x13` with GC waveforms.
Full refresh follows the reference's 500 ms clear stabilization, additional
100 ms reset stabilization, 300 ms initialization stabilization, and 500 ms
redraw stabilization. Ordinary partial updates use DU waveforms.
White-plane clears use 256-byte SPI writes, with a shorter final write for
each 15,000-byte plane. Failed buffer or mutex allocation immediately releases
all partially allocated resources and leaves the driver unavailable.
Five 42-byte waveform tables are
written to `0x20` through `0x24`; `0x17` with `0xA5` activates the refresh.
Partial updates transfer the
full frame. The always-on controller stays awake between updates; unlike the
sample's final sleep step, it does not issue deep sleep after presentation.
The driver treats raw BUSY LOW as active and HIGH as ready. Hardware testing
confirmed this polarity on the V1.2A unit: full waveforms took about 1.17 seconds
and partial waveforms about 0.57 seconds, ending with a LOW-to-HIGH transition.
It differs from the A-revision sample's wait condition. Debug logs report the
raw pin level at wait entry and exit along with elapsed time.
BUSY waits are bounded at 15 seconds. A timeout marks the display unavailable
until reboot. The presentation loop defers new waveforms during OTA.
Pixel changes normally settle for 150 ms before presentation, but settling is
capped at 500 ms from the first pending change so repeated LVGL redraws cannot
postpone a frame indefinitely. The configured minimum refresh interval still
applies independently; waveform time is additional.

All `HAS_DISPLAY` boards provide the standard action `button_nav` with commands
`previous`, `next`, `tap`, and `tap_hold`, labeled Previous, Next, Tap, and
Tap & Hold. CrowPanel preassigns these through `HW_BUTTON_DEFAULTS_JSON`, using
the normal hardware-button action format. Other boards keep their existing
defaults and can assign navigation through the action editor:

| Input | Tap | Hold |
|---|---|---|
| Menu | None | Existing configuration-entry gesture |
| Back | None | None |
| Rotary Up / Down | Previous / next actionable focus | None |
| Rotary OK | Tap focused button or rocker zone | Tap & Hold focused button or rocker zone |
| Boot | Full refresh | None |

An explicit saved empty action array disables that event's default. Navigation
runs on the LVGL task, skips hidden/disabled buttons and inactive rocker zones,
and reuses normal tap/hold dispatch. Each pad visit starts without a selection:
Next selects the first actionable target, Previous selects the last, and Tap or
Tap & Hold does nothing until a target is selected. If a selected target becomes
unavailable, selection clears. A pad-wide action takes precedence over individual
buttons and must also be selected before tapping. Screen selection uses a custom
pad with screen actions; there is no built-in screen picker.

Selection follows the button's live border color, opacity, and corner radius,
with twice its border width and a minimum of 2 pixels. Borderless buttons use
a 2-pixel border in their text color. Rocker selection frames the selected zone.
This temporary highlight does not change saved appearance settings.

Confirmation starts without a selection or focus outline. The first Next
selects Cancel, the first Previous selects Approve, and the first Tap selects
Cancel without activating it. Subsequent Previous or Next switches between
the buttons; Tap activates the selected option. A replacement confirmation
starts without a selection again. Tap & Hold does nothing in the dialog.
Confirmation cannot be bypassed by physical navigation. The first press when
asleep only wakes the screen.

Hardware acceptance checks remain required:

* Cold boot, BUSY polarity, white clear and image orientation at all four rotations
* Partial-update latency, ghosting, and forced/scheduled full refreshes
* Every physical contact, release debounce, OK hold without a following tap,
   Menu configuration entry, and Boot without unintended bootloader entry
* Unselected pad entry, first/last selection, custom screen-selection pads,
   disabled/hidden bindings, both rocker types, confirmation
   cancellation/acceptance, wake-first behavior, and pad saves while focused
* Framebuffer changes during a waveform and OTA while a refresh is pending

## Inkplate 6FLICK Profile

`inkplate6flick-interactive` is an Interactive E-Paper target with a 3.5625 MiB,
no-OTA application partition and 280 KiB LittleFS filesystem. Back up stored
files before serial-flashing this partition layout; the smaller filesystem
does not preserve files from the old layout.

The render mode selects the Inkplate library framebuffer at boot:

* **B/W** uses the 1-bit framebuffer. The driver converts RGB565 pixels with a
   boot-selected threshold, Bayer, or blue-noise conversion. `partialUpdate()` is available only in this mode;
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

The interactive profile supports three signed ESP32-S3 native Extension slots
(56 KiB, 56 KiB, and 120 KiB of usable ELF capacity). Its dedicated
`ota_8mb_e1003_ext_32MB` partition scheme reserves the final 256 KiB of flash
for Extensions without changing the storage partition's starting offset. Back
up stored pads and other files before flashing this profile over an older
E1003 build: the storage partition becomes 256 KiB smaller, and mounting an
existing larger LittleFS volume asserts and reboots instead of formatting it.
After backing up the old volume, erase the storage region before flashing the
new partition table; then restore the files to the freshly formatted volume.
Run `./tools/install-custom-partitions.sh` after
updating the repository, or `./setup.sh` for a fresh installation. Prefer
slow-changing Extension widgets because frequent updates are costly on e-paper.

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

Build the board and exercise its supported render modes on hardware. Verify that
touch or physical input continues to work while a waveform is pending, a full refresh can be queued,
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