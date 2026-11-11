---
title: reTerminal Sticky MVE Implementation Plan
description: Phased implementation, acceptance gates, and progress tracking for interactive reTerminal Sticky support.
ms.date: 2026-11-11
---

## Status and execution rules

The MVE scope is agreed. Phase 0 source research and user-facing control
decisions are complete. The user signed off Phase 0 and its researched design
on 2026-11-10. The user approved combining Phases 1 and 2 on 2026-11-10.
Phases 1 and 2 are implemented and software-validated. Both remain In progress
because hardware acceptance is pending. The three P2 recovery findings have
been fixed and regression-tested; Phase 3 is the next software handoff.

* Plan location: tracked `docs/dev/sticky-implementation-plan.md` on the Sticky
  feature branch only; delete this plan before merging back to `release/1.48.0`
* Local research archives and validation logs remain ignored in `build/temp/`;
  links to that evidence require the retained local files
* Proposed target: `reterminal-sticky-interactive`
* Existing device class: `macropad`, with Interactive E-Paper capabilities
* No new device class or sleep-first E-Paper Frame target
* Repository baseline inspected: `eb96ccd6`
* Research date: 2026-11-10
* Hardware validation: no hardware available; use the latest published
  schematic/source as the design baseline, not as evidence of a tested unit
* Firmware builds and implementation tests: not run during planning

Use `Not started`, `In progress`, `Blocked`, and `Done` for phase status.
Check tasks only after completing them. A compiled driver is not proof of
physical operation. Keep hardware acceptance pending until tested on a device.
Record exact commands, results, hardware revision, and evidence in the
[execution log](#execution-log). Update the phase summary and outstanding
decisions at each handoff. Do not mark a phase Done with unmet acceptance
criteria; describe blockers instead.

## Agreed scope

* Wi-Fi using existing provisioning, reconnect, portal, MQTT, and OTA paths
* Interactive pads and basic touchscreen operation
* 800 x 480 B&W rendering with partial refresh and full-refresh cleanup
* Native four-level grayscale rendering using full refresh
* Three configurable physical buttons using the existing action system;
  all runtime tap/hold assignments initially empty
* GPIO48 buzzer integrated with existing button feedback and alert tones
* SHT40 temperature and humidity through the existing sensor infrastructure
* BQ27220 fuel-gauge readings through the existing sensor infrastructure
* Battery percentage and voltage, signed current after verification, and
  status fields only when their meaning is established
* Normal operation on USB and battery, without a battery-runtime commitment

Battery work is read/report only. Board-required startup power handling is
necessary bring-up, not a new battery-management feature. Do not implement
charging policy, gauge calibration, low-battery shutdown, or power tuning.

### Deferred work

* SD card, microphone, and six-axis IMU
* PCF8563 RTC integration and scheduled wake
* Automatic deep sleep, touch-to-wake, custom shutdown, and runtime optimization
* Advanced gestures and new multitouch behavior
* Recorded audio, MP3/Music playback, and microphone processing
* Grayscale partial refresh unless later verified and separately accepted
* A Sticky Frame variant

Do not enable unrelated optional capabilities merely because the chip supports
them. Keep BLE/BLE HID, USB HID, native Extensions, image-fetch, and image-library
features disabled for this MVE. Preserve normal portal, MQTT, and MCP support.

## Reference material and preliminary findings

Use Seeed's linked Arduino and ESP-IDF resources extensively. Prefer verified
hardware behavior and existing project interfaces over transplanting a demo's
application architecture, blocking loops, or optional features.

### Source register

* [Hardware overview](https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/)
  establishes the hardware inventory and primary pin map.
* [Arduino basics and display](https://www.seeedstudio.com/sticky/docs/en/device-guide/arduino-basics/)
  documents Seeed_GFX2, OPI PSRAM, CH343P serial, controller detection, Gray4,
  and the buttons-at-top orientation.
* [Arduino interaction](https://www.seeedstudio.com/sticky/docs/en/device-guide/arduino-interaction/)
  provides GT911 startup, touch power, coordinate mapping, and B&W partial
  refresh examples.
* [Arduino peripherals](https://www.seeedstudio.com/sticky/docs/en/device-guide/arduino-peripherals/)
  provides button, PWM buzzer, SHT40, BQ27220, and external-power examples.
* [ESP-IDF basics](https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-basics/)
  links the peripheral and dashboard demo archives and describes board/device
  ownership.
* [ESP-IDF pages and peripherals](https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-pages/)
  establishes separate touch/sensor buses and shared sensor-bus ownership.
* [ESP-IDF refresh](https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-refresh/)
  distinguishes grayscale full, monochrome full, and monochrome partial refresh;
  partial waveforms can require full comparison-frame transfers.
* [Sticky schematic](https://files.seeedstudio.com/wiki/reterminal_sticky/res/reTerminal_Sticky_Schematic_diagram_260609.pdf)
  was inspected as searchable text. Its drawing title blocks identify
  Rev 01, dated 2026-06-05; actual hardware revision is unknown.
* [Peripheral demo archive](https://files.seeedstudio.com/wiki/reterminal_sticky/res/Sticky_peripheral_demo.zip)
  was downloaded and inspected for low-level interfaces and provenance.
* [Dashboard demo archive](https://files.seeedstudio.com/wiki/reterminal_sticky/res/Sticky_dashboard_demo.zip)
  was downloaded and inspected for board sequencing, display, touch, sensors,
  charger, and buzzer behavior.
* [Seeed_GFX2 source](https://github.com/Seeed-Studio/Seeed_GFX2)
  was inspected at main revision
  `0e8bac1188cb915d5ea26ea16d538aed13b3cf48`. Recheck and pin the selected
  revision before adopting it as a dependency.
* [Sticky automatic controller selection](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_Sticky_Auto.cpp),
  [SSD1677 interface](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD1677.h),
  and [SSD2677 interface](https://github.com/Seeed-Studio/Seeed_GFX2/blob/0e8bac1188cb915d5ea26ea16d538aed13b3cf48/src/driver/epaper/Driver_SSD2677.h)
  show two panel variants, different BUSY behavior, and previous/current-frame
  requirements.

* [BQ27220 datasheet](https://www.ti.com/lit/ds/symlink/bq27220.pdf) and
  [technical reference manual](https://www.ti.com/lit/ug/sluubd4/sluubd4.pdf)
  were checked for units, current sign, configuration, and calibration.

All downloaded references are retained under
[sticky-phase0-references/](../../build/temp/sticky-phase0-references/).
Only static source/text inspection was performed; no vendor source was executed,
built, installed, or flashed.

### Hardware contract established from Phase 0 sources

These are documentation-backed assignments, not measurements on our unit.

| Function                 | Reference assignment                                                |
| ------------------------ | ------------------------------------------------------------------- |
| MCU and memory           | ESP32-S3; 32 MB flash; 8 MB OPI PSRAM                                 |
| Display                  | 3.97-inch, 800 x 480, four grayscale levels; SSD1677 or SSD2677        |
| Display SPI              | SCK 13, MOSI 14, CS 15, DC 16, reset 17, BUSY 18, enable 47          |
| Shared SPI MISO          | GPIO12; verify panel readback wiring before relying on it            |
| GT911 touch              | SDA 3, SCL 2, INT 21, reset 41, enable 42 active high                |
| Sensor I2C               | SDA 1, SCL 0; SHT40 at 0x44, BQ27220 at 0x55                         |
| Buttons                  | AI/OK 4, Up 5, Down 6; active low                                    |
| Buzzer                   | GPIO48, PWM tones                                                   |
| System power controls    | Examples drive power hold 45 and power lock 46 high                  |
| Charger and USB detect   | Charger enable 39 active low; external-power detect 9 active high    |
| Deferred SD              | Shares display SPI; CS 8, power enable 10, detect 11                 |
| Deferred microphone      | Clock 19, data 20, enable 38                                         |
| Serial                   | CH343P UART bridge; TX 43, RX 44; examples use 115200 baud             |

Important findings:

* Do not assume one panel controller or one BUSY polarity. The vendor auto
  driver handles missing/undriven MISO reads and retries the other controller.
  The local integration must preserve bounded errors and add diagnostics for
  fallback selection; floating-MISO detection needs physical acceptance.
* Four gray levels do not require changing the shared snapshot ABI to 2 bpp.
  Existing display snapshots support 1- and 4-bit packed formats.
* The vendor reports a 100 mOhm current-sense resistor versus a 10 mOhm gauge
  default. Current must preserve its sign and use verified board/gauge scaling;
  avoid applying the factor twice to a preconfigured gauge.
* GPIO9 indicates external power, not proven active battery charging. Do not
  label USB presence as `battery_charging` without independent evidence.
* Maintaining power hold/lock during normal operation is necessary even though
  custom power management is deferred. PWR_HOLD is the latch data input;
  PWR_LOCK is its clock, so drive hold high before lock.
* The vendor ESP-IDF touch example programs controller resolution; its
  coordinate assumptions differ from the Arduino touch example. Do not copy
  either transform without checking the GT911's reported configuration.

## Repository integration map

Paths are relative to the repository root; links resolve from this plan.
The Phase 0 design is approved; new filenames remain implementation proposals.

* [Board configuration](../../src/app/board_config.h),
  [target configuration](../../config.sh), and a new
  `src/boards/reterminal-sticky-interactive/` directory own target selection,
  pins, metadata, board startup, and board-specific sensors.
* [Board startup hook](../../src/app/board_hardware.cpp) already includes
  `BOARD_HARDWARE_SOURCE`; [application startup](../../src/app/app.ino)
  calls `board_hardware_init()` before display/configuration initialization
  but after startup logging and a delay. D2 proposes a narrowly gated earlier
  hook for the power latch.
* [E1003 interactive overrides](../../src/boards/reterminal-e1003-interactive/board_overrides.h)
  provide an e-paper profile reference, not a pin/controller template.
* [Display HAL](../../src/app/display_driver.h),
  [display aggregator](../../src/app/display_drivers.cpp),
  [CrowPanel buffered driver](../../src/app/drivers/crowpanel42_epaper_driver.cpp),
  and [IT8951 driver](../../src/app/drivers/it8951_epaper_driver.cpp)
  provide presentation, coalescing, cleanup, and snapshot patterns.
* [Touch aggregator](../../src/app/touch_drivers.cpp) and
  [GT911 implementation](../../src/app/drivers/gt911_touch_driver.cpp)
  provide an existing controller path. Prefer configuration and a narrowly
  justified transform change over another GT911 driver.
* [Hardware buttons](../../src/app/hw_buttons.cpp) and
  [button configuration](../../src/app/hw_button_config.cpp)
  already provide debouncing, tap/hold dispatch, defaults, and feedback.
* [Audio API](../../src/app/audio.h),
  [audio worker](../../src/app/audio.cpp),
  [output HAL](../../src/app/audio_output_driver.h),
  [output aggregator](../../src/app/audio_output_drivers.cpp), and
  [tone parser/renderer](../../src/app/tone_alert_overlay.h)
  own feedback and alerts. Existing output backends consume PCM; a GPIO
  assignment alone cannot connect a frequency-driven PWM buzzer.
* [Sensor aggregator](../../src/app/sensors.cpp),
  [sensor manager](../../src/app/sensors/sensor_manager.cpp), and
  [StopWatch board sensor](../../src/boards/m5stack-stopwatch/stopwatch_battery_sensor.cpp)
  demonstrate the existing `BOARD_SENSOR_SOURCE` registration path.
* [Shared I2C lock](../../src/app/i2c_bus.h) protects Wire bus 0 specifically.
  Do not assume it also serializes Wire1 or initialize the same controller
  again on different pins.
* [Storage facade](../../src/app/storage.h) remains the file-I/O boundary.
* [Host-test registration](../../tests/CMakeLists.txt) owns permanent coverage.
  Arduino subdirectory implementations must also enter the appropriate
  sketch-root aggregator.

## Phase summary and dependencies

| Phase | Deliverable                          | Prerequisites | Status      | Result                       |
| ----- | ------------------------------------ | ------------- | ----------- | ---------------------------- |
| 0     | Resolve hardware and design gates    | None          | Done        | Research/design signed off   |
| 1     | Board, power, Wi-Fi, and OTA bring-up | 0             | In progress | Checks pass; device pending  |
| 2     | Display and touch                    | 0, with 1     | In progress | Checks pass; device pending  |
| 3     | Physical buttons and tone feedback   | 1 software    | Not started | Software handoff ready       |
| 4     | Environmental and battery sensors    | 1             | Not started | Not run                      |
| 5     | Integrated acceptance and docs       | 2, 3, 4       | Not started | Not run                      |

Phases 2, 3, and 4 have no required implementation dependency on each other.
Coordinate shared board configuration and hardware access. Their combined
on-device behavior is accepted in Phase 5.

After the recovery-fix checkpoint below, Phase 3 software implementation may
proceed without declaring Phases 1/2 physically accepted. Carry their open
hardware checks forward and exercise them alongside Phase 3 when hardware is
available. Phase 3 itself has not started; this handoff does not implement it.

## Phase 0: Resolve hardware and design gates

### Tasks

* [x] Review the schematic, both downloadable demos, and relevant Seeed_GFX2
  implementations. Record source versions, file/function references, and license
  compatibility before adopting third-party code or dependencies.
* [x] Identify the available hardware revision and panel controller. Decide
  support for both documented panel variants; do not silently ship a
  single-controller target as general Sticky support. No hardware is available;
  record both variants as implementation targets with physical acceptance deferred.
* [x] Compare the existing project toolchain with vendor requirements. Confirm
  flash/PSRAM modes, UART selection, and a reusable two-slot OTA partition
  layout. Do not copy the XIAO board profile's defaults without checking 32 MB
  flash and the repository's build conventions.
* [x] Choose the smallest maintainable display integration. Recommend a
  driver-scoped, MIT-licensed low-level subset behind our buffered HAL rather
  than installing the complete Seeed graphics/runtime stack. D1 records why
  the unmodified library is not suitable.
* [x] Establish controller detection, BUSY polarity, refresh waveforms, baseline
  handling, pixel packing, rotation, and timeout/error-reporting behavior.
* [x] Choose I2C controller ownership once. Use touch on Wire1 and sensors on
  Wire, preserving the documented GPIOs. This differs from the vendor bus
  numbering to follow the repository's ESP32-S3 touch convention; see D2.
* [x] Review documented power hold/lock timing, touch/display enable sequencing,
  safe deferred-peripheral states, charger defaults, and reset/recovery.
  Record physical verification separately; do not add a custom charging policy.
* [x] Select the buzzer integration seam in the existing audio worker. Prefer
  a narrow tone-capable backend using existing parsed pattern segments and
  command ownership, while leaving PCM backends unchanged. Do not infer tone
  frequency back from PCM or add direct buzzer calls at feedback call sites.
* [x] Resolve monophonic feedback/alert overlap, stop/preemption, volume/mute,
  and completion semantics before coding. Obtain approval for any unavoidable
  user-visible difference from existing audio behavior.
* [x] Confirm BQ27220 register units/sign and identify the unresolved current
  calibration assumption. Select reliable initial status fields. Keep reads
  non-destructive; no unseal, calibration, or data-memory writes.
* [x] Choose sensor placement and dependency strategy. Prefer existing board
  source hooks for board-specific behavior; extract a reusable SHT4x adapter
  only if justified. Use an established SHT4x library when compatible; avoid
  adding a large sensor framework.
* [x] Preserve sensor-cache semantics deliberately. Current tests require
  last-good values after failed reads; initially unavailable fields are null.
  Log read failures and do not manufacture zeroes. If fresh/stale distinction
  or immediate invalidation is required, seek approval for a narrowly scoped
  extension rather than silently changing all sensors.
* [x] Finalize board flags, optional inherited features, button defaults, and
  recovery control. User confirmed all runtime assignments empty and Up held
  during startup for configuration recovery. Do not map hold to deep sleep.

### Acceptance and handoff

* [x] Record decisions D1-D7 below with evidence and affected implementation paths.
* [x] Confirm source support for B&W partial and Gray4 full refresh on both
  controllers; physical quality/timing remains unverified.
* [x] Record hardware-only unknowns as explicit later-phase test conditions.
* [x] Obtain detailed-plan/design approval before production edits.
  User signed off Phase 0 on 2026-11-10.

Expected output: decisions and source inventory in this temporary plan.
No new public research document, hardware framework, or dependency installation
is needed merely to finish planning.

## Phase 1: Board, power, Wi-Fi, and OTA bring-up

### Tasks

* [x] Add `reterminal-sticky-interactive` to `config.sh` and create board
  overrides plus `metadata.json`. Use `macropad` metadata, not `epaper_frame`.
* [x] Add minimal board hardware source/header through the existing hook and
  the narrowly gated early power hook from D2. Establish power hold/lock and
  bus ownership before peripherals; preserve other boards' startup order.
* [x] Reuse `partitions_ota_8mb_32MB.csv`, retaining two OTA application slots
  and internal filesystem storage. No partition installer change is expected.
* [x] Configure CH343P UART serial correctly; do not advertise native USB HID
  based on the presence of USB-C.
* [x] Keep SD, microphone, IMU, RTC, and unsupported audio/file features disabled.
  Set safe shared-SPI chip-select/power states without initializing SD.
* [x] Verify target discovery, installer metadata, and capability accuracy using
  existing build/installer mechanisms rather than board-specific CI ladders.
* [x] Update flag ownership/reference generation immediately if introducing
  `HAS_*` or `IS_*` flags; use driver selectors for driver selection.

Likely files: new board directory, `config.sh`, `app.ino`, and
`board_hardware.h/.cpp`. Read the build and compile-time
instructions before editing these paths.

### Acceptance and checks

* [x] Focused board-feature, branding, and installer checks pass.
* [x] A single-board firmware build succeeds with reviewed warnings.
* [ ] USB flashing boots; serial reports the expected chip, flash, and PSRAM.
* [ ] Cold boot/reset remain powered on both USB and battery.
* [ ] Provision Wi-Fi, reconnect after AP loss, and retain settings after reboot.
* [ ] Successful OTA boots the updated image and preserves settings/files.
  Exercise a recoverable failed upload without leaving the portal unusable.

Do not call board bring-up complete using a placeholder driver. If no suitable
temporary display-disabled profile exists, implement the minimum Phase 2 driver
needed for the first real build, and record the cross-phase work explicitly.

## Phase 2: Display and touch

### Tasks

* [x] Add the selected Sticky display implementation behind `DisplayDriver`,
  returning `Buffered`, and include it through `display_drivers.cpp`.
* [x] Keep LVGL flush work to buffer updates. Follow the established `present()`
  path for waveforms, change coalescing, and configured presentation cadence.
* [x] Implement first/full refresh, changed-content B&W partial refresh,
  periodic/manual full cleanup, and four-level grayscale full refresh.
* [x] Preserve previous/current controller frames correctly. Partial waveform
  support does not imply rectangular-only SPI transfer; transfer full comparison
  frames where required by the selected controller.
* [x] Use existing threshold/dither and persisted render-mode settings.
  Default to B&W; apply mode changes using the current Save/Reboot contract.
* [x] Handle clipping, source stride, byte alignment, rotation, and gray mapping.
  Use the existing 1-/4-bpp snapshot contract if snapshots are implemented.
* [x] Check PSRAM allocation failures, release every buffer/resource, and bound
  BUSY waits. Report a failed/unavailable panel rather than recording success.
* [x] Reuse GT911 with source-verified address/reset/power sequencing and coordinate
  transformation. Avoid double-applying the vendor's rotation.
* [x] Keep no-change polls distinct from release/error and prevent stuck presses.
* [x] Honor OTA activity before new presentation work and at owned safe points;
  do not suspend another subsystem or block OTA transport.
* [x] Disable animations and expose no backlight capability. Preserve existing
  interactive e-paper presentation controls rather than adding a parallel UI.
* [x] Add only focused coverage for new risks, using existing display/touch
  test seams. Register any necessary new driver test in CMake.

Likely files: a new display driver pair, `board_config.h`, the display
aggregator, board overrides, and narrowly required GT911 changes.
Avoid changes to shared managers unless the HAL cannot express a present need.

### Acceptance and checks

* [x] Host checks cover changed conversion/packing, alignment, first-full versus
  subsequent-partial behavior, forced full refresh, and failure cleanup.
* [x] Existing CrowPanel/IT8951 and GT911 checks pass if shared paths change.
* [x] Recovery regressions cover acknowledging an invalid touch frame before
  accepting release/new input, panel power cleanup during OTA, power-rail
  cutoff on cleanup timeout, and manual full-refresh retry after an error.
* [ ] On glass, an 800 x 480 chart has correct orientation, corner labels, and
  distinct black/dark-gray/light-gray/white regions in grayscale mode.
* [ ] Repeated small B&W changes preserve unchanged content; manual full refresh
  clears visible ghosting. Record 50 small updates as a baseline trial.
* [ ] No-change content produces no additional physical presentations after
  pending work settles.
* [ ] Tap the center and four corners, press/hold/release a pad button, and
  navigate pads without mirrored coordinates, duplicate actions, or stuck input.
* [ ] Measure full/partial refresh times and input-to-action behavior. Do not
  promise LCD latency or grayscale partial refresh.
* [ ] Record each tested controller variant separately; untested hardware stays
  explicitly unverified.
* [ ] On SSD2677 hardware, interrupt power-on/refresh with OTA and verify
  power-off cleanup; verify the rail cutoff/restart and manual full-refresh
  recovery paths after a recoverable panel fault.

## Phase 3: Physical buttons and existing tone feedback

### Tasks

* [x] Declare three `HW_BUTTON_DEFS` entries with source-verified labels/polarity.
  Reuse existing debounce, tap/hold actions, portal configuration, and persistence.
* [x] Leave all three runtime tap/hold assignments empty. Configure Up/GPIO5
  for the existing startup configuration-mode check; document it separately.
  Do not implement a separate Sticky action dispatcher.

The two declarations above were explicitly included in the Phase 1 request.
They do not start Phase 3 buzzer work or establish physical-button acceptance.
* [ ] Implement the approved GPIO48 PWM backend using existing feedback/alert
  APIs, parser, queueing, cancellation, and ownership mechanisms.
* [ ] Support multi-step tone patterns and silence gaps, one-shot/looping alerts,
  stop, completion callbacks, and mute/volume behavior as agreed in Phase 0.
  Use the KISS single-pitch PWM path: active alerts take priority over feedback;
  discard overlapping feedback rather than replaying it later.
* [ ] Preserve the existing tap-feedback suppression for actions that themselves
  produce audio, and existing hold-feedback behavior.
* [ ] Keep `HAS_SOUND_PLAYER` false. Ensure tone-only capability does not expose
  MP3/Music controls or unsupported amplifier controls.
* [ ] Leave the buzzer silent after stop, failure, reboot cleanup, and idle.
  Avoid blocking delays in input or LVGL code.
* [ ] Extend existing audio-worker tests with a buzzer profile or equivalent
  narrow backend coverage; do not build a second pattern parser/test framework.

Likely files: board overrides, `audio.cpp`, `audio_output_driver.h`,
`audio_output_drivers.cpp`, the selected PWM backend, and existing audio tests.
Apply the native-tone worker seam and overlap policy recorded in D3.

### Acceptance and checks

* [ ] Each physical button produces one tap or hold action as configured,
  including debounced release, without a duplicate tap after a hold.
* [ ] Saved button assignments and feedback settings survive reboot.
* [ ] Touch and physical-button tap feedback reach the same buzzer backend.
* [ ] Existing alert-tone patterns play correct pitches, durations, and gaps.
  Looping alerts stop promptly and obey the approved overlap policy.
* [ ] Disabled feedback and zero volume remain silent.
* [ ] Existing disabled/beep/MP3 worker profiles still pass after shared changes.
* [ ] Audible feedback works during e-paper activity without blocking action
  dispatch or creating runaway playback.

## Phase 4: SHT40 and BQ27220 sensors

### Tasks

* [ ] Reuse the board-owned sensor bus; initialize it once and serialize any
  cross-task access. Do not reinitialize touch's I2C controller.
* [ ] Register SHT40 temperature and humidity using `SensorCallbacks` and the
  existing `temperature`/`humidity` keys, units, and discovery conventions.
* [ ] Register BQ27220 `battery_percentage` and `battery_voltage`, plus verified
  signed current with an explicit unit and reliable status fields. Current
  remains unavailable until its scale is established; do not blindly divide
  an already calibrated gauge reading by ten.
* [ ] Use gauge percentage rather than the ADC voltage-to-percentage curve.
  Validate read lengths, signed decoding, scaling, and plausible ranges.
* [ ] Expose GPIO9 as `external_power_present`; do not advertise active charging
  from external-power presence or the gauge's non-discharging state.
* [ ] Keep measurement reads free of gauge configuration/calibration writes.
  Do not add remaining-time or learned-capacity estimates to this MVE.
* [ ] Reuse sensor sampling cadence, API/MQTT caches, sensor bindings, and
  Home Assistant discovery. Keep sensor I/O out of render/resolver callbacks.
* [ ] Preserve null before any valid sample and approved last-good behavior
  after failed reads, with actionable low-rate diagnostics and recovery.
* [ ] Let existing OTA-aware sampling defer nonessential measurements.
* [ ] Register board sources through `sensors.cpp` and `BOARD_SENSOR_SOURCE`.
  Reuse one board-owned Wire0 bus for both devices.
* [ ] Add focused decoding/error/registration coverage in the existing suite.
  A new hardware-adapter test is justified only for uncovered I2C/error contracts.

Likely files: `sticky_sensors.cpp`, board hardware source, board overrides,
`arduino-libraries.txt`, and existing sensor tests. Add the two Sensirion
dependencies selected in D4; no new generic sensor flags/framework.

### Acceptance and checks

* [ ] API, MQTT, `[sensor:...]` bindings, and Home Assistant discovery expose
  the same approved keys and units without extra hardware reads per request.
* [ ] SHT40 values are plausible and respond to environmental changes; allow for
  enclosure/device heating rather than inventing calibration offsets.
* [ ] Gauge percentage is within 0-100 and voltage uses volts consistently.
* [ ] Test current decoding with positive, negative, and zero values and the
  verified resistor/gauge scaling. Confirm actual USB/battery direction on device.
* [ ] Missing-device, NACK, short-read, invalid-value, and later recovery cases
  follow the approved cache/error contract and never fabricate zero readings.
* [ ] Sampling both sensors does not disrupt touch, Wi-Fi, audio, or OTA.

## Phase 5: Integrated acceptance and documentation

### Tasks

* [ ] Create a representative pad using touch actions, physical navigation,
  temperature/humidity, battery bindings, feedback, and an alert-tone action.
* [ ] Run it in B&W and grayscale, changing render mode through the existing
  portal and rebooting as required.
* [ ] Stress OTA while sensor sampling, tone playback, and display work are
  active. Check recovery from an interrupted upload and successful reboot.
* [ ] Run a 30-minute mixed-use session and record minimum free heap/stack
  observations, presentation timing, faults, and any resource growth.
  This is functional integration evidence, not a battery-life benchmark.
* [ ] Reconcile the complete scoped change set and generated metadata.
* [ ] Add the board README and hardware-index link. Document actual tested panel
  revisions, flashing/recovery, USB serial, render limitations, three buttons,
  tone-only buzzer, sensor fields, and the deferred features.
* [ ] Update existing interactive-e-paper, trigger, audio, and sensor guidance
  only where generic behavior or user tasks change. Do not duplicate catalogs.
* [ ] Update display/touch and audio developer references for changed HAL
  behavior; update build/library references only if their workflows change.
* [ ] Review README/API references only for changed public contracts, and
  reconcile release notes when warranted by the project's release workflow.
* [ ] Regenerate affected driver tables and feature-flag references. Do not
  defer runtime registration, schema, or required fixture updates to this phase.
* [ ] Report remaining unverified behavior and obtain acceptance. Do not commit,
  push, release, or flash a device without the relevant authorization.

### Completion criteria

* [ ] All agreed MVE functions work together on the available hardware.
* [ ] All selected focused automated checks and the Sticky build pass.
* [ ] Existing shared audio/display/sensor behavior is preserved.
* [ ] No deferred peripheral or unsupported feature is advertised.
* [ ] Documentation links, generated metadata, and capability lists agree.
* [ ] Results identify exactly which controller/hardware variants were tested.

## Validation command menu

Run only checks relevant to the changed phase. Commands here are planned,
not executed. Configure once, build only selected test targets, and run matching
CTest selectors. Confirm new target/test names after registration.

```bash
cmake -S . -B build/host-tests

# Phase 1 and any changed flag classification
python3 tests/test_board_feature_overrides.py
python3 tests/test_firmware_size_report.py
bash tests/test_branding_mirror.sh
bash tests/test_esp_web_tools_site.sh

# Phase 2 shared display/touch checks
cmake --build build/host-tests --parallel --target test_crowpanel_epaper_driver test_it8951_reterminal-e1003-interactive
ctest --test-dir build/host-tests --output-on-failure -R '^(test_crowpanel_epaper_driver|test_it8951_reterminal-e1003-interactive|test_gt911_touch|test_touch_driver_samples|test_touch_manager_snapshots)$'
bash tests/test_epaper_macro_contract.sh
node tests/test_portal_epaper_config.js

# Phase 3 shared audio behavior
cmake --build build/host-tests --parallel --target test_audio_worker_disabled test_audio_worker_beep test_audio_worker_mp3 test_tone_alert_overlay
ctest --test-dir build/host-tests --output-on-failure -R '^(test_audio_worker_(disabled|beep|mp3)|test_tone_alert_overlay)$'
node tests/test_portal_audio_config.js

# Phase 4 shared sensor/cache behavior
cmake --build build/host-tests --parallel --target test_sensor_manager
ctest --test-dir build/host-tests --output-on-failure -R '^test_sensor_manager(_duty_cycle)?$'

# Generated references, when their sources changed
python3 tools/generate-board-driver-table.py --update-drivers-readme
python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md

# Single-board firmware verification after the target exists
./build.sh reterminal-sticky-interactive
```

Add the new Sticky display, buzzer, and sensor test selectors to the relevant
phase's recorded commands once their actual names are known. Check action
catalog completeness or binding-schema parity only if their metadata changes;
no new action type or binding scheme is currently required.

Before firmware verification, follow the repository's preferred-board procedure
and confirm the Sticky target for this work. Do not run bare `./build.sh` or
expand to all-board builds without an explicit request. Build shared host
profiles for regression coverage; additional firmware boards need a reason
and approval under the repository rules.

There is no repository-local Markdown lint configuration or installed
`markdownlint` executable identified during planning. Use existing tooling if
available during implementation; do not install a documentation framework for
this temporary plan.

## Decisions and open risks

### D1: Display implementation and source provenance

Approved research decision, signed off 2026-11-10:

* Target both SSD1677 and SSD2677 with one buffered Sticky HAL driver.
* Use the pinned Seeed_GFX2 MIT-licensed controller/detection source as the
  low-level reference/subset under
  `src/boards/reterminal-sticky-interactive/display/sticky_epaper/`.
  Retain its license and attribution beside any adopted source and identify
  local changes and the upstream revision.
* Do not import the dashboard UI, touch task, sensor stack, or ESP-IDF
  application/component architecture.
* Do not install the complete Seeed_GFX2 library for this target. Its product
  board factory enables the SD power rail, it duplicates drawing/framebuffer
  responsibilities already owned by our HAL, and the controller paths need
  local allocation/error/lifecycle hardening.
* Do not copy the demo archives' application/display code into production:
  no governing license was found for those portions of the supplied archives.
  The button component has its own license; that does not license the rest.
  These archives remain hardware/protocol references only.

Evidence:

* [Library license](../../build/temp/sticky-phase0-references/Seeed-Studio-Seeed_GFX2-0e8bac1/LICENSE)
  covers the original Seeed library under MIT and separately identifies
  third-party portions. Adopt only the needed, license-verified files.
* [Board factory](../../build/temp/sticky-phase0-references/Seeed-Studio-Seeed_GFX2-0e8bac1/src/board/boards/reTerminal_ePaper_Boards.h)
  drives `sdEnablePin()` high during `begin()`. The
  [dashboard board initialization](../../build/temp/sticky-phase0-references/Sticky_dashboard_demo/main/board/board.cpp)
  also drives SD enable high despite a comment saying the slot is off.
  For our MVE, deselect SD with GPIO8 high and keep its active-high GPIO10
  rail enable low. No SD initialization.
* [SSD1677 baseline allocation](../../build/temp/sticky-phase0-references/Seeed-Studio-Seeed_GFX2-0e8bac1/src/driver/epaper/Driver_SSD1677.cpp)
  uses a 48,000-byte `malloc` buffer without reporting allocation failure.
* [SSD2677 implementation](../../build/temp/sticky-phase0-references/Seeed-Studio-Seeed_GFX2-0e8bac1/src/driver/epaper/Driver_SSD2677.cpp)
  allocates two 48,000-byte comparison buffers. Failure can leave partial
  refresh inactive without an allocation error; the inspected class declares
  no destructor to release those buffers.
* [Driver interface](../../build/temp/sticky-phase0-references/Seeed-Studio-Seeed_GFX2-0e8bac1/src/core/Driver.h)
  provides bounded BUSY waits, defaulting to 30 seconds. Our driver must
  propagate failures and avoid prolonged work that prevents OTA progress.
* [SSD2677 interface](../../build/temp/sticky-phase0-references/Seeed-Studio-Seeed_GFX2-0e8bac1/src/driver/epaper/Driver_SSD2677.h)
  establishes the 800 x 680 internal scan geometry versus 800 x 480 visible
  area, four-level mapping, and old/new full-frame partial comparison.

Required implementation boundaries:

* New board-local `display/sticky_epaper_driver.h/.cpp`, a driver selector, and
  the existing display aggregator entry; any adopted vendor files stay
  driver-scoped. Board-local placement was approved after the combined-phase
  implementation; it does not change the HAL or controller behavior.
* Allocate large drawing/comparison/snapshot buffers in PSRAM, fail explicitly,
  and release them on teardown/error. Avoid duplicate baseline ownership.
* Preserve controller-specific reset timing, waveform selection, and BUSY
  polarity. Log the detected controller and any fallback.
* Detection must not treat a floating 0x00/0xFF MISO result as a proven ID.
  Review the vendor BUSY fallback; do not claim a disconnected panel is valid.
* Implement B&W full/partial and Gray4 full refresh only. Maintain a valid
  baseline after successful presentation, and recover with full refresh after
  reset/failure or incompatible mode state.
* Keep existing presentation cadence, manual cleanup, threshold/dither, and
  snapshot contracts. Do not add a second graphics framework or global task
  suspension mechanism.

### D2: Board bring-up, buses, and build selection

Approved research decision, signed off 2026-11-10:

* Use `esp32:esp32:esp32s3` with `FlashSize=32M`, `PSRAM=opi`,
  `FlashMode=qio`, `PartitionScheme=ota_8mb_32MB`, and `CDCOnBoot=default`.
* Reuse the existing
  [32 MB partition layout](../../partitions/partitions_ota_8mb_32MB.csv):
  two 8 MiB application slots plus NVS, OTA data, coredump, and internal
  filesystem storage. No new layout or native Extension region is required.
* The installed and setup-pinned Espressif core is 3.3.12. Vendor IDF demo
  validation at 5.4 is a reference, not a requirement to migrate our toolchain.
* Use CH343P UART0 logging/flashing on GPIO43/44, not native USB CDC/HID.
* Configure touch on `Wire1` with SDA3/SCL2; sensors on `Wire` with SDA1/SCL0,
  both at 400 kHz. Physical pins remain unchanged from Seeed's map.
* This software bus-number swap is intentional:
  [existing GT911 guidance](../../src/app/drivers/gt911_touch_driver.h)
  favors I2C1 for ESP32-S3 touch/Wi-Fi coexistence. Sensor Wire0 transactions
  can use the existing shared bus-0 lock.
* Create the sensor bus once in board initialization. Sensor adapters reuse it;
  do not call default `Wire.begin()` or rebind either controller elsewhere.

The existing `board_hardware_init()` follows power-mode setup, logging, a
startup delay, and telemetry. A power-latch device should not depend on these
completing first. Add one narrowly gated early board power hook at the start
of `setup()` for Sticky's hold/lock sequence; retain the normal hook for bus
and peripheral initialization. No generic power-policy framework.

Likely shared edits: `app.ino`, `board_hardware.h/.cpp`, board configuration,
and the new board source/header. Other boards retain their existing initialization
order. Verify any bootloader-to-setup limitation on hardware; an early application
hook cannot guarantee power retention before application startup.

Sequence:

1. Establish power hold high, then lock high, with checked GPIO operations.
2. Keep buzzer silent, microphone GPIO38 low, SD CS high, and SD power low.
3. Configure the normal hardware-owned charger-enable baseline, GPIO39 low,
   without a charging policy or runtime control.
4. Initialize the sensor bus and external-power input.
5. Enable touch GPIO42 before its reset/address sequence; allow the documented
   250 ms settle time. Enable display GPIO47 and allow the documented startup
   settle time before controller probing.

Touch coordinates remain a physical acceptance gate. The Arduino example
assumes a portrait controller configuration; the IDF demo
[initializes/programs the GT911 differently](../../build/temp/sticky-phase0-references/Sticky_dashboard_demo/main/devices/sticky_touch.cpp).
Reuse our driver, inspect the reported resolution, and apply a board-configured
swap/inversion only if needed. No unconditional transform change for other boards.

### D3: KISS buzzer integration

User confirmed the KISS path; simultaneous feedback/alert output is exceptional.
Use hardware PWM on GPIO48 with no simultaneous mixer, secondary worker,
deferred-feedback queue, or automatic replay.

Keep the existing `audio_feedback()`, `audio_submit_tone()`, stop, alarm-session,
generation/epoch, and completion contracts. Extend `AudioOutputDriver` with
the smallest optional native-tone capability and add a board-selected PWM backend.
The existing worker owns segment timing, command arbitration, and cancellation;
existing PCM drivers keep their current render/write path.

* Reuse parsed `ToneAlertOverlay` segments rather than another pattern parser.
* Drive one pitch or silence at a time; pace segments with bounded worker waits
  and monotonic timing. Do not infer frequency from PCM.
* When an alert is active, consume/discard overlapping feedback. Starting an
  alert also clears an in-progress feedback cue; do not resume stale feedback.
* Preserve the same-button suppression already present in
  [action_list_has_audio()](../../src/app/action_list.cpp),
  [hardware-button feedback](../../src/app/hw_buttons.cpp), and
  [pad-button feedback](../../src/app/screens/pad_screen_events.cpp).
  It recognizes Sound alert, Alarm tone, and Alarm MP3 action types.
* A physical button with no assigned action currently produces no feedback;
  preserve this behavior rather than adding beeps to unassigned controls.
* Zero volume/mute means silent output. Positive volume maps to a bounded PWM
  duty range, accounting for the existing quieter feedback gain; loudness is
  not assumed linear or equivalent to speaker volume.
* Gaps, idle, stop, OTA entry, failure, and reboot cleanup must silence output.
* Do not apply I2S DMA/starvation accounting or amplifier warm-up requirements
  to the native-tone backend. Do not advertise physical amplifier controls.
* Keep `HAS_AUDIO=true`, `HAS_SOUND_PLAYER=false`, and `HAS_AUDIO_INPUT=false`.

The installed core declares `ledcAttach`, `ledcWriteTone`, and `ledcDetach`.
Use checked core APIs; avoid hardcoded LEDC channel allocation taken from the
vendor demo. The vendor's blocking 60 ms beep is a pin/tone reference, not
the playback implementation.

### D4: Sensors, current scaling, and status

Research decision:

* Use `BOARD_SENSOR_SOURCE` with a proposed `sticky_sensors.cpp` registering
  separate environmental and battery callbacks; no new generic sensor feature
  flags are needed for this board-only MVE.
* Use `Sensirion I2C SHT4x@1.1.2` and `Sensirion Core@0.7.3`, verified in the
  Arduino registry. Add them to the dependency manifest only during approved
  implementation; no installation occurred in Phase 0.
* Reuse the initialized sensor Wire0 bus. Keep the SHT40 heater off; use normal
  measurements and the library's CRC/error handling. A 5-second environmental
  sampling interval is sufficient for initial bring-up.
* Implement narrow, checked, read-only BQ27220 standard-command reads through
  the same board bus. The demo's comprehensive gauge driver contains
  configuration/unseal functionality that this MVE must not import.
* Read voltage at 0x08/0x09 in mV and expose `battery_voltage` in V.
  Read state of charge at 0x2C/0x2D as `battery_percentage` in percent.
* Current at 0x0C/0x0D is a signed value in calibrated mA. Negative means
  discharge; positive means charge. If verified, expose `battery_current`
  in A with that sign convention.
* The schematic confirms R46 is 100 mOhm. The Arduino example applies 0.1
  to readings based on a 10 mOhm default, but the IDF helper returns the
  register value directly. TI states CC Gain sets the current scale.
  Neither source proves the calibration on an unseen shipping unit.
* Do not blindly choose either current scale or write gauge calibration.
  Keep current unavailable until read-only configuration/vendor evidence or a
  physical reference measurement establishes the correct scale. Percentage
  and voltage reporting must not be blocked by this optional field.
* Expose GPIO9 as `external_power_present`, not `battery_charging`.
  The schematic also shows a CHARGE_STATE net on GPIO40, but actual charging
  status is unnecessary for this MVE and remains deferred until verified.
* Do not add estimated runtime, remaining capacity, capacity learning, or
  calibration UI. Do not substitute voltage-derived percentage.

Use the existing metadata/discovery pipeline for API, MQTT, bindings, and
Home Assistant. A proposed 2-second gauge sampling interval matches the
current sensor-manager default. Sample validity is per field; an unavailable
optional current value must not erase valid voltage/percentage readings.

### D5: Sensor failure/cache contract

Keep the existing tested initial-null/last-good behavior. A transient failed
sample does not replace the previous valid cached value. No global
sensor-manager invalidation or new freshness framework is part of this MVE.

Log actionable read errors at low rate, retain explicit unavailable state before
the first valid sample, and recover on subsequent scheduled reads. Do not
manufacture zero measurements. Document that cached values are last-good
readings, not proof of fresh hardware data after a fault.

Evidence:
[sensor cache](../../src/app/sensors/sensor_manager.cpp) and
[existing failure assertions](../../tests/test_sensor_manager.cpp).

### D6: Buttons, recovery, and capability gates

User-confirmed decisions:

* All three runtime tap/hold assignments are empty by default.
* Up/GPIO5 held through the application's 1.5-second startup check enters
  configuration mode, without erasing settings.
* This recovery shortcut is separate from runtime actions. It reuses existing
  behavior also used by CrowPanel, StopWatch, and other board targets.
* Do not map AI/Power hold to sleep or shutdown.

Set `HAS_BUTTON=true`, `NUM_HW_BUTTONS=3`, and label AI/OK, Up, Down with
GPIO4/5/6 active low. Configure `BUTTON_PIN=5` and
`HAS_CONFIG_MODE_BUTTON=true` for recovery. Do not install navigation defaults.
The Up recovery path provides portal access when, for example, saved Wi-Fi is
unreachable and the device would otherwise continue offline.

Keep display, touch, e-paper, grayscale, filesystem, Wi-Fi, MQTT, MCP, and
tone-only audio available. Disable SD/storage routing, microphone, generic
ADC battery sensing, BLE/BLE HID, native USB HID, native Extensions, image-fetch,
and image-library capabilities for this target. No new `IS_*` device class.
Keep board metadata synchronized with these actual capabilities.

### D7: Hardware baseline and evidence limits

User reports no hardware yet and asks to assume the latest revision.
Use the latest published source available on the research date and Rev 01
schematic as the implementation baseline. Do not present this as confirmation
of a newer unseen PCB or as proof of either panel variant working.

Deferred hardware acceptance:

* Cold battery startup, hold/lock timing, reset, and Up boot recovery
* Actual GT911 configuration and corner mapping
* Both panel controllers, floating-MISO detection, grayscale quality, BUSY
  polarity, refresh times, and ghosting cleanup
* Gauge current calibration and accurate charging-status interpretation
* PWM audible range/volume and concurrent Wi-Fi, display, sensors, and OTA

These are later-phase acceptance conditions, not reasons to fabricate Phase 0
test results. Source-level implementation can proceed under the approved design.

For each resolved decision, append the date, choice, evidence, and consequences.
If a vendor example conflicts with the schematic or observed hardware, stop
that integration path and resolve the discrepancy rather than guessing.

## Execution log

### Combined Phase 1 and 2 approval: 2026-11-10

* User approved combining Phases 1 and 2 after reviewing the display prerequisite.
* Use the normal interactive e-paper Macropad pattern, not a temporary Headless
  profile, class override, unrelated driver, or placeholder initialization.
* Implement the real dual-controller buffered display and GT911 integration
  alongside board bring-up. Phase 3 buzzer and Phase 4 sensors remain excluded.
* Hardware-only acceptance remains pending for both phases.
* User requested no "experimental" target label. The completed plan aims for
  production-ready support; pending hardware acceptance remains explicit.

### Planning checkpoint: 2026-11-10

* Completed: agreed scope captured; linked Arduino/ESP-IDF guides and selected
  upstream controller sources inspected; repository integration paths traced
* Created: this ignored temporary plan only
* Validation: no firmware build, host tests, dependency installation, or flashing
* Hardware: not accessed
* Remaining: Phase 0 gates and detailed-plan approval

### Phase 0 research checkpoint: 2026-11-10

* Status: research complete; Blocked pending researched-design approval before
  Phase 1. Hardware acceptance remains deferred, not passed.
* Production changes: none. Updated this temporary plan and retained downloaded
  references beside it; the tracked working tree remains clean.
* Decisions: D1-D7 recorded above; user confirmed KISS buzzer integration, empty
  runtime button assignments, Up boot recovery, and no hardware availability.
* Static sources reviewed: the schematic text, both demos' relevant board/device
  implementations, pinned Seeed_GFX2 controllers/board/panel code, TI gauge
  documents, and repository audio/touch/sensor/build paths.

Commands and results:

* `arduino-cli core list`: failed because the CLI is not on the shell PATH.
  Retried using the existing repository-local binary; no tool installation.
* `./bin/arduino-cli core list`: passed; Espressif 3.3.12 installed, matching
  `setup.sh`. Inkplate core also present but unrelated.
* `./bin/arduino-cli board details --fqbn
  'esp32:esp32:esp32s3:FlashSize=32M,PSRAM=opi,PartitionScheme=ota_8mb_32MB,CDCOnBoot=default,FlashMode=qio'
  --format json`: passed; JSON confirmed all five selected options.
* `./bin/arduino-cli lib search 'Sensirion I2C SHT4x' --format json`:
  passed; version 1.1.2 and Sensirion Core dependency found.
* `./bin/arduino-cli lib search 'Sensirion Core' --format json`:
  passed; version 0.7.3 found.
* `curl --fail --location` downloads: passed for the two demo ZIPs, pinned
  graphics ZIP, schematic, TI datasheet, and TI technical reference.
* ZIP validation/extraction: passed; member paths/symlinks and total selected
  sizes checked before extraction. No extracted source executed.
* `pdftotext -layout` and targeted source/text inspection: passed.
  Schematic connectivity conclusions remain subject to physical validation.
* `git status --short`: clean.
* Firmware/host builds, flashing, dependency installation, and physical tests:
  not run; Phase 0 changed no production code.

Reference fingerprints:

* Peripheral ZIP:
  `2872c5c10629e8eff55f8c3c1c6a6026dd511bd313f04385b1fae4cb0a49d7cc`
* Dashboard ZIP:
  `f52918c42411f73375db1125b4c5ee0fc8691d8b8523c26d3c8aab7e8a256101`
* Pinned Seeed_GFX2 ZIP:
  `3e8b0ae78bb388d45ad2624501bd48f40947fb8ebfee5fdadc2844df90a810fc`
* Schematic PDF:
  `32232320af8a733b9eb2c8c85877aa233a025b28096fabea1954d428e4aaefde`
* TI datasheet PDF:
  `9fffe0b632cd287c9210aa1d2e71af1816b31764c11f1b62425de3a4a7cfe5f4`
* TI technical reference PDF:
  `44779f229423d649e410439d1d1a3a65026d5919cdd43112ee3a3d8d9811ead9`

The approval gate recorded at this checkpoint was resolved by the signoff below.

### Phase 0 signoff: 2026-11-10

* Status: Done. User explicitly signed off Phase 0 and requested a handover
  prompt to implement Phase 1.
* D1-D7 are approved as the implementation baseline; hardware-only acceptance
  remains deferred, not passed.
* Phase 1 remains Not started. This handoff does not execute implementation,
  builds, dependency installation, flashing, or later phases.
* Next action: implement Phase 1 against the approved design and record exact
  focused-check/build results and outstanding hardware acceptance.

### Per-phase result template

Copy this block for each phase or meaningful checkpoint:

```text
Phase / checkpoint:
Date:
Status: Not started | In progress | Blocked | Done
Repository revision / local changes:
Hardware revision and panel controller:
Completed task IDs or checklist items:
Files changed:
Decisions resolved and source references:
Commands run and exact pass/fail results:
Hardware procedure, observations, and evidence paths:
Refresh timing / resource measurements, if applicable:
Failures, fixes, and retest results:
Remaining acceptance criteria:
Blockers or approval needed:
Next action:
```

Store large logs, photos, and measurements in ignored `build/temp/`
when available, and link them from the corresponding phase entry. Do not
substitute a log path for a pass/fail summary or mark unperformed tests passed.

### Combined Phase 1 and 2 implementation checkpoint: 2026-11-10

Status: In progress for both phases. Implementation and focused software checks
pass; every unperformed hardware acceptance item remains unchecked.
Baseline: `eb96ccd6`, with the local changes listed below. No hardware revision
or panel controller has been physically tested.

#### Completed implementation

* Added the Macropad target with 32 MB QIO flash, 8 MB OPI PSRAM, the existing
  two-slot OTA partition layout, and CH343P UART configuration.
* Added the gated first-in-`setup()` power-latch hook, checked hold-before-lock
  sequencing, safe deferred-peripheral states, sensor Wire0 initialization
  under the existing lock, and GT911-owned Wire1 initialization.
* Configured Up/GPIO5 for existing startup recovery. All three runtime button
  tap/hold assignments remain empty.
* Added the real buffered SSD1677/SSD2677 driver, controller-specific frame
  packing, B&W partial/full cleanup, four-level full refresh, snapshots, bounded
  BUSY handling, checked resource lifecycle, and cooperative OTA safe points.
* Added board-gated GT911 reported-resolution mapping without changing other
  boards' default touch behavior.
* Reused existing provisioning, Wi-Fi, portal, persistence, rendering controls,
  and OTA paths. No board-specific replacements or new dependencies.
* Updated truthful installer capabilities, generated flag/driver references,
  hardware guidance, build configuration, display architecture, and release notes.
  Existing interactive e-paper and recovery guides remain authoritative; no
  duplicate user topic or portal API change was needed.

#### Changed files

New board files:

* `src/boards/reterminal-sticky-interactive/board_overrides.h`
* `src/boards/reterminal-sticky-interactive/metadata.json`
* `src/boards/reterminal-sticky-interactive/sticky_hardware.h`
* `src/boards/reterminal-sticky-interactive/sticky_hardware.cpp`
* `src/boards/reterminal-sticky-interactive/README.md`

New display and test files:

* `src/boards/reterminal-sticky-interactive/display/sticky_epaper_driver.h`
* `src/boards/reterminal-sticky-interactive/display/sticky_epaper_driver.cpp`
* `src/boards/reterminal-sticky-interactive/display/sticky_epaper/panel.h`
* `src/boards/reterminal-sticky-interactive/display/sticky_epaper/panel.cpp`
* `src/boards/reterminal-sticky-interactive/display/sticky_epaper/LICENSE`
* `tests/test_sticky_epaper_driver.cpp`
* `tests/test_sticky_hardware.cpp`
* `tests/sticky_driver_test_overrides/SPI.h`
* `tests/sticky_driver_test_overrides/Wire.h`
* `tests/sticky_driver_test_overrides/driver/gpio.h`

Modified integration files:

* `config.sh`
* `src/app/app.ino`
* `src/app/board_config.h`
* `src/app/board_hardware.h`
* `src/app/display_drivers.cpp`
* `src/app/display_manager.cpp`
* `src/app/drivers/gt911_touch_driver.h`
* `src/app/drivers/gt911_touch_driver.cpp`
* `tests/CMakeLists.txt`

Modified documentation and generated references:

* `README.md`
* `CHANGELOG.md`
* `docs/user/hardware.md`
* `docs/dev/build-and-release-process.md`
* `docs/dev/display-touch-architecture.md`
* `docs/compile-time-flags.md`
* `src/app/drivers/README.md`
* This implementation plan (initially ignored; tracked on the feature branch)

#### Commands and results

The following focused commands passed:

```bash
bash -n config.sh
python3 tests/test_board_feature_overrides.py
python3 tests/test_firmware_size_report.py
bash tests/test_branding_mirror.sh
bash tests/test_esp_web_tools_site.sh
bash tests/test_epaper_macro_contract.sh
node tests/test_portal_epaper_config.js
python3 tools/generate-board-driver-table.py --update-drivers-readme
python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md
cmake -S . -B build/host-tests
cmake --build build/host-tests --parallel 4 --target test_sticky_epaper_driver test_sticky_hardware test_crowpanel_epaper_driver test_it8951_reterminal-e1003-interactive
ctest --test-dir build/host-tests --output-on-failure -R '^(test_sticky_epaper_driver|test_sticky_hardware|test_crowpanel_epaper_driver|test_it8951_reterminal-e1003-interactive|test_gt911_touch|test_touch_driver_samples|test_touch_manager_snapshots)$'
./build.sh reterminal-sticky-interactive
BOARD_FILTER=reterminal-sticky-interactive bash tools/build-esp-web-tools-site.sh /home/jan/dev/esp32-macropad/build/temp/sticky-phase12-site
git diff --check
```

Results and evidence:

* Board-feature combinations, firmware-size ownership (28 tests), branding,
  installer smoke fixtures, macro taxonomy, and portal configuration all passed.
* Final selected host run: 7/7 passed, 11.43 seconds. Includes both Sticky
  production-source targets, existing CrowPanel/IT8951, and command-driven GT911
  and touch snapshot profiles. See
  [host build](../../build/temp/sticky-phase12-host-final-build.log) and
  [host results](../../build/temp/sticky-phase12-host-final-tests.log).
* Regression coverage exercises latch/bus failures and ownership, both controller
  protocols, conversion/packing/rotation, frame baselines, allocation cleanup,
  absent/stuck BUSY detection, OTA cancellation during stream/cadence/BUSY waits,
  and GT911 dimensions/mapping/release/error handling.
* Final single-board firmware build passed after the last production edit.
  No compiler warning/error diagnostics were present in the complete
  [final build log](../../build/temp/sticky-phase12-firmware-final.log).
  Sketch: 3,139,511 bytes (37% of the 8 MiB application slot).
  Static RAM: 77,748 bytes (23%). These are compile-time measurements, not
  evidence of runtime PSRAM availability or memory stability.
* Actual built firmware was discovered by the unchanged installer generator.
  [Installer log](../../build/temp/sticky-phase12-installer-final.log) and
  [manifest](../../build/temp/sticky-phase12-site/manifests/reterminal-sticky-interactive.json)
  show ESP32-S3 with offsets 0, 32768, 57344, and 65536.
  The generated Macropad listing advertises only MCP and MQTT capability badges.
  OTA SHA-256 and size match the built binary, 3,139,808 bytes.
* An inline Node `fs.existsSync` check passed for all six local links in the
  new board README; final label, capability, and wireless metadata assertions
  passed. No Markdown tooling was installed.

Exact final installer assertions (all passed):

```bash
jq -e '.name == "ESP32 Macropad reTerminal Sticky Interactive" and (.builds | length == 1) and (.builds[0] | .chipFamily == "ESP32-S3" and [.parts[].offset] == [0,32768,57344,65536])' build/temp/sticky-phase12-site/manifests/reterminal-sticky-interactive.json
grep -Fq 'data-board="reterminal-sticky-interactive" data-chip="ESP32-S3" data-class="macropad"' build/temp/sticky-phase12-site/flash.html
grep -Fq '../flash.html?board=reterminal-sticky-interactive' build/temp/sticky-phase12-site/devices/macropad.html
test "$(jq -r '.sha256' build/temp/sticky-phase12-site/ota/reterminal-sticky-interactive.json)" = "$(sha256sum build/reterminal-sticky-interactive/app.ino.bin | cut -d ' ' -f 1)"
test "$(jq -r '.size' build/temp/sticky-phase12-site/ota/reterminal-sticky-interactive.json)" = "$(stat -c %s build/reterminal-sticky-interactive/app.ino.bin)"
```

#### Failures, fixes, and deviations

* The user approved combining both phases rather than introducing a temporary
  Headless identity exception. No unrelated or placeholder display was selected.
* Pinned Seeed_GFX2 revision
  `0e8bac1188cb915d5ea26ea16d538aed13b3cf48` supplies the licensed low-level
  reference/subset. Archive SHA-256 was rechecked:
  `3e8b0ae78bb388d45ad2624501bd48f40947fb8ebfee5fdadc2844df90a810fc`.
  The MIT license/attribution is retained. Demo application code was not copied.
* The vendor's unwired-MISO fallback can accept a static BUSY level. This driver
  instead requires an observed bounded BUSY transition when ID probing is invalid,
  preventing absent hardware from reporting initialization success. This is a
  deliberate detection hardening; both real controller variants still need
  physical detection/orientation/refresh acceptance.
* `/memories/board-preferences.md` was absent and creation failed with EACCES.
  The explicit Sticky verification-board selection was instead recorded in
  `/home/jan/.copilot/session-state/ee33e708-a683-44d9-ab29-d25d2db7cab3/files/board-preferences.md`.
  The protected path was not bypassed.
* Initial native target invocation preceded CMake reconfiguration and failed.
  Reconfiguration registered the new targets; selected builds then passed.
* Initial host compilation found the test stub `board_config.h` instead of the
  production header and reported a nested-ternary warning under `-Werror`.
  Explicit production-header inclusion and a typed local fixed both; retests pass.
* Flag-reference generation initially rejected undocumented board hooks.
  Hook descriptions were added and generation then passed. An initial shell
  command without `set -e` masked that individual failure; the later individual
  generator success and final firmware generation check are the accepted results.
* A final verification attempt used CTest command names as native build targets:
  `cmake --build build/host-tests --parallel 4 --target test_sticky_epaper_driver test_sticky_hardware test_crowpanel_epaper_driver test_it8951_reterminal-e1003-interactive test_gt911_touch test_touch_driver_samples test_touch_manager_snapshots`.
  It failed because `test_gt911_touch` is a Python command test, not a build
  target. The corrected four-native-target build and seven-test CTest run above
  passed; the command-driven tests execute against current source.
* The first firmware build passed, but one panel timing edit overlapped it.
  A second build after production sources stabilized passed and is the final
  recorded result.
* No charging policy, sleep/shutdown behavior, buzzer backend, sensors, new
  dependencies, all-board build, flashing, commit, or push was performed.

#### Remaining hardware acceptance and next action

No hardware was available. Both phases remain In progress, not Done.
Compilation and host doubles do not establish any of these results:

* USB flashing/boot, serial chip/flash/PSRAM report, cold USB/battery latch
  retention, reset, and Up/GPIO5 startup recovery
* Wi-Fi provisioning, AP-loss reconnect, persistence across reboot, successful
  OTA with retained settings/files, and recoverable failed upload
* Separate SSD1677 and SSD2677 detection, BUSY polarity, on-glass orientation,
  grayscale levels, previous/current-frame integrity, and ghosting cleanup
* Fifty small B&W updates, settled no-change presentation suppression, refresh
  timing, and input-to-action behavior
* GT911 center/corners, hold/release, pad navigation, and combined touch/display
  behavior without duplicated actions or stuck presses
* OTA contention/stress on real hardware and runtime allocation stability

Hardware next action: perform the outstanding Phase 1/2 procedures and record
each controller/revision separately. Neither phase is Done without that evidence.
The software handoff gate is updated by the recovery-fix checkpoint below.

### Board-local driver placement approval: 2026-11-10

The user approved keeping Sticky-specific production implementation in the
Sticky board directory. This is a placement-only refactor: move the display
driver and its licensed panel subset into
`src/boards/reterminal-sticky-interactive/display/`, retain the existing
`DisplayDriver` interface, selector, factory branch, and sketch-root aggregator,
and update host-test paths and the architecture reference.

Shared GT911 and rendering helpers remain shared. Host tests remain in `tests/`.
No new registration framework, HAL abstraction, or build hook is introduced.
Removing a board still requires removing its explicit integration references;
folder ownership is not automatic unregistration.

The five implementation/license files were moved; the old driver directory was
removed after it became empty. Updated files outside the board are
`src/app/display_drivers.cpp`, `src/app/display_manager.cpp`,
`tests/CMakeLists.txt`, `tests/test_sticky_epaper_driver.cpp`,
`docs/dev/display-touch-architecture.md`, and this plan. Includes use the
existing firmware `src/` include root; the Sticky host target now includes that
same root. No behavior, driver selector, capability, or HAL interface changed.

The following commands passed:

```bash
cmake -S . -B build/host-tests
cmake --build build/host-tests --parallel 4 --target test_sticky_epaper_driver test_sticky_hardware
ctest --test-dir build/host-tests --output-on-failure -R '^(test_sticky_epaper_driver|test_sticky_hardware)$'
bash tests/test_epaper_macro_contract.sh
./build.sh reterminal-sticky-interactive
git diff --check
```

* Focused production-source tests: 2/2 passed, 0.38 seconds; see
  [configuration](../../build/temp/sticky-board-local-cmake.log),
  [host build](../../build/temp/sticky-board-local-host-build.log), and
  [test results](../../build/temp/sticky-board-local-host-tests.log).
* The single-board firmware build passed with unchanged reported sketch and
  static RAM sizes: 3,139,511 and 77,748 bytes. No compiler warning/error
  diagnostics were present in the
  [build log](../../build/temp/sticky-board-local-firmware.log).
* `rg -n 'drivers/sticky_epaper' src tests docs build/temp/sticky-implementation-plan.md`
  found no obsolete paths before this command was recorded here. The
  architecture reference and current implementation inventory now name the
  board-local location.
* No new test harness, dependencies, other-board migration, flashing, commit, or
  push. Phase 1/2 hardware acceptance remains pending.

#### Deferred board-local ownership cleanup

This follow-up is separate from the current Sticky move and does not authorize
editing other boards during this task.

* [ ] Apply the same ownership pattern to the CrowPanel-specific display driver
  and any exclusively owned supporting source/license files. Keep the existing
  HAL, compilation aggregator, and normal test-suite registration.
* [ ] Audit other board-named or board-specific drivers, such as Inkplate and
  Tab5, for exclusive board ownership. Move only implementations actually tied
  to one board or board family; do not infer exclusivity from a filename.
* [ ] Leave reusable controller drivers and shared protocol/rendering helpers
  central. Do not add a generic plugin/factory framework or migrate shared code
  merely to make every board removable by deleting one directory.
* [ ] Update affected includes, test registrations, documentation paths, and
  attribution references; validate each migration with focused tests and the
  repository's applicable single-board build procedure.

### Phase 1/2 recovery fixes and Phase 3 handoff: 2026-11-10

Status: all three P2 review findings fixed and software-validated. Phases 1/2
remain In progress solely for outstanding hardware acceptance. Phase 3 is
ready for its software implementation handoff, not physically accepted or started.

#### Fixes and regression evidence

* [x] Acknowledge a fully read GT911 frame with invalid coordinates before
  returning Error. Do not publish invalid contacts or change the default path
  for other boards. The existing hardware test now checks acknowledgement,
  subsequent release, and a valid new contact without reinitialization.
* [x] Finish an owned SSD2677 drive session on cancellation/error. During OTA,
  only bounded ready/power-off cleanup continues; new refresh work stays
  deferred. If BUSY times out, cut the display rail and allow 100 ms off.
  The next full retry restores power and waits 100 ms before initialization.
* [x] Register the display-power pin with Arduino GPIO before using
  `digitalWrite`; the initial native board GPIO configuration alone does not
  register the pin with the Espressif core's peripheral manager.
* [x] Preserve initialization/resource availability after a transient
  presentation failure. Keep the snapshot invalid and request a full retry,
  but allow the shared manual full-refresh API to reach the driver.
  Genuine initialization/allocation failures remain unavailable.
* [x] Extend the existing display test, without a new harness, for cancellation
  at power-on, refresh, and power-off; cleanup timeouts at power-on/power-off;
  power-rail restoration; and a manual full retry without another pixel flush.
  Both controller variants retain timeout/manual-recovery coverage.

Before production fixes, the extended tests failed as expected: 14 display
assertions and two touch assertions. The final focused and shared checks pass.
Snapshot validity is retained only when refresh and power-off cleanup complete;
interrupted/failed presentations require full refresh.

Changed production files:

* `src/app/drivers/gt911_touch_driver.cpp`
* `src/boards/reterminal-sticky-interactive/display/sticky_epaper_driver.cpp`
* `src/boards/reterminal-sticky-interactive/display/sticky_epaper/panel.h`
* `src/boards/reterminal-sticky-interactive/display/sticky_epaper/panel.cpp`

Changed checks/documentation:

* `tests/test_sticky_hardware.cpp`
* `tests/test_sticky_epaper_driver.cpp`
* `docs/dev/display-touch-architecture.md`
* This implementation plan (initially ignored; tracked on the feature branch)

#### Validation

The following commands passed after the final production edit:

```bash
cmake --build build/host-tests --parallel 4 --target test_sticky_epaper_driver test_sticky_hardware test_crowpanel_epaper_driver test_it8951_reterminal-e1003-interactive
ctest --test-dir build/host-tests --output-on-failure -R '^(test_sticky_epaper_driver|test_sticky_hardware|test_crowpanel_epaper_driver|test_it8951_reterminal-e1003-interactive|test_gt911_touch|test_touch_driver_samples|test_touch_manager_snapshots)$'
node tests/test_portal_epaper_config.js
bash tests/test_epaper_macro_contract.sh
./build.sh reterminal-sticky-interactive
git diff --check
```

* Host checks: 7/7 passed, 11.48 seconds; see
  [recovery host results](../../build/temp/sticky-recovery-host-tests.log).
* Single-board firmware build: passed; no compiler warnings/errors.
  Sketch 3,139,819 bytes (37%), static RAM 77,748 bytes (23%); see
  [recovery firmware build](../../build/temp/sticky-recovery-firmware.log).
* Used the previously explicit Sticky verification-board selection. The
  unavailable `/memories/board-preferences.md` limitation is unchanged.
* No capability, API schema, dependency, driver placement, or generated
  metadata change was required. The firmware build confirmed that the
  generated compile-time flag reference is unchanged.
* No physical tests, flashing, all-board build, commit, or push.

#### Next software phase

Implement Phase 3 only, using D3's approved native-tone worker seam and GPIO48
PWM backend. Reuse existing button dispatch/configuration, keep runtime defaults
empty and Up boot recovery unchanged, and preserve same-button audio-action
feedback suppression. Keep active-alert priority with no mixer or deferred
feedback replay. Preserve the existing PCM profiles and add focused PWM coverage.

Do not implement Phase 4 sensors or enable deferred peripherals as part of that
handoff. Carry Phase 1/2 hardware acceptance, including the new recovery checks,
forward explicitly; no untested controller/revision becomes accepted by building.

### Feature-branch checkpoint: 2026-11-11

The user approved creating `feature/reterminal-sticky` from `release/1.48.0`
and committing the current implementation locally so Sticky work can continue
without changing the release branch. The plan moved from ignored
`build/temp/sticky-implementation-plan.md` to tracked
`docs/dev/sticky-implementation-plan.md`; retained archives and logs were not
added to Git. Historical commands above retain their original paths.

* [ ] Before merging the completed feature branch into `release/1.48.0`, delete
  this tracked plan and remove any branch-only incoming links. Carry lasting
  hardware guidance into the authoritative board/user/developer documentation,
  not into a permanent execution diary.
* [ ] Complete remaining implementation and hardware acceptance before treating
  Sticky support as accepted. This checkpoint does not complete Phases 1/2 or
  start the deferred CrowPanel/other-board ownership cleanup.

No push, merge, flashing, or all-board build is part of this checkpoint.

Pre-commit reconciliation and validation passed:

* Reviewed the aggregate tracked/untracked implementation and affected board,
  user-index, build, and display-architecture documentation. Shared setup,
  rendering-control, and recovery guides remain authoritative and unchanged.
* `cmake --build build/host-tests --parallel 4 --target test_sticky_epaper_driver test_sticky_hardware test_crowpanel_epaper_driver test_it8951_reterminal-e1003-interactive`:
  passed.
* `ctest --test-dir build/host-tests --output-on-failure -R '^(test_sticky_epaper_driver|test_sticky_hardware|test_crowpanel_epaper_driver|test_it8951_reterminal-e1003-interactive|test_gt911_touch|test_touch_driver_samples|test_touch_manager_snapshots)$'`:
  7/7 passed, 11.93 seconds.
* `python3 tools/generate-board-driver-table.py --update-drivers-readme` and
  `python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md`:
  passed; generated references reconciled.
* Inline Node local-link and code-fence check: passed for all 51 local links in
  this relocated plan and all six in the board README. Ignored research/log
  evidence remains local and is not part of the commit.
* `git diff --check`: passed. No firmware source changed during this checkpoint,
  so the retained successful recovery firmware build remains the applicable
  single-board evidence; no new firmware build was required.
