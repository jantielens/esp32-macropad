---
title: ESP32 Macropad
description: Turn ESP32 boards into shortcut panels, live dashboards, game controls, and specialized instruments.
---

Your favorite shortcut, one tap away. Your home's energy use, live on your desk.

**ESP32 Macropad** is open-source firmware for customizable touchscreen controls
and dashboards, with game controls and specialized instruments to explore too.
Flash a supported board, then build and configure it in your browser.
No coding required for everyday configuration.

## 🚀 Try It

1. Choose your exact board and firmware variant in the [online installer](https://jantielens.github.io/esp32-macropad/). Check [supported hardware](#-supported-hardware) if you're still choosing a board.
2. Connect it by USB and flash in **Chrome** or **Edge**. No compiler or command-line tools needed.
3. Join its setup hotspot, connect it to your Wi-Fi, and open the web portal to make your first button.

Start with a Macropad build for touchscreen controls and dashboards.
[First-time setup](#-getting-started) walks you through a "Hello!" button that
needs no keyboard pairing or Home Assistant connection.

## 💡 What Can You Build?

### A Macropad: Your Shortcuts, Without the Finger Gymnastics

Put a complex keyboard combo behind one button. Add text snippets, presentation
controls, and media keys beside it. **USB and BLE keyboard macros** put your
favorite shortcuts on screen instead of making you memorize them.

On USB HID boards, add a **Mousepad** for pointing, clicking, and button-zone dragging,
a **Scrollpad**, or **gamepad controls** with two sticks, two triggers, a hat,
and 16 buttons. Multicontact drivers support button-zone dragging, simultaneous controls, and
two-finger Mousepad scrolling.

BLE supports keyboard control only; USB gamepad output is generic HID, not XInput.
See [keyboard macros](docs/pad-editor-guide.md#key-sequences),
[mouse controls](docs/pad-editor-guide.md#mousepad), and
[gamepad controls](docs/pad-editor-guide.md#gamepad-controls).

### A Smart Home Panel: Controls That Fit Your Routine

Lights, blinds, music, and a good-night scene belong together when you use them
together. Build a bedside remote that calls **Home Assistant services** or
publishes MQTT commands.

Auto-discovery exposes available sensors and controls without discovery YAML.
Your automations can switch screens, wake the display, and send sounds or
notifications back. MQTT works with other systems too; Home Assistant is optional.

[Connect to Home Assistant](docs/ha-integration-guide.md).

### A Live Dashboard: Your Data at a Glance

Is the house running on solar or drawing from the grid? Put the answer in a large
gauge, give battery charge its own bar, and show the day's trend in a sparkline.
Mix **gauges, bar charts, sparklines, tables, and scrollable lists** with ordinary
buttons and adjustment rockers. Advanced options include multi-ring gauges with
target zones and multi-line sparklines; supported builds can backfill history
from Home Assistant after reboot.

For a sensor publishing watts to `home/solar/power`, the label
`Solar: [mqtt:home/solar/power;;%.0f W]` shows the live reading with its unit.

Arrange up to **16 pads** with flexible grids (up to 8x8, board-dependent) and
buttons spanning multiple positions. Style them with icons, images, colors,
and aligned labels. Try DSEG7 for seven-segment readings, Doto for a pixel look,
or Bebas for bold labels. Smooth value animations bring readings to life;
idle pads, display sleep, fading, and pixel shifting handle downtime.

[Design your first pad](docs/pad-editor-guide.md#designing-a-pad).

### Live Bindings: A Dashboard That Reacts to Your Data

A gauge can turn red when consumption rises; a control can disappear when it
isn't available. **Bindings** connect data to labels, colors, widgets, button
states, and action values. Add `[health:cpu]%` for device load, or
`[time:%H:%M;Europe/Amsterdam]` for a local clock.

Combine MQTT values and JSON fields with clocks, timers, health, and data from
enabled media or camera features. Expressions, formatting, fallbacks, and named
bindings keep it reusable. The editor checks syntax and previews live values.

[Explore binding templates](docs/pad-editor-guide.md#binding-templates).

### Automation & Timers: One Tap Starts the Routine

Configure one persistent weekly alarm in the portal's **Alarm** settings. Choose
weekdays and local time, then up to three ring actions and three stop actions.
Use **Alarm Control** on pad buttons or MCP to Snooze and Cancel. **Loop Tone**
and **Loop MP3** are available for ring actions. The dedicated **Timezone**
settings offer grouped cities and a device-time preview; changes apply on Save.
The device-wide timezone also supplies the default for `[time:format]` bindings.
See the [alarm guide](docs/web-portal-guide.md#alarm-clock).

Start a focus timer, lower the brightness, and show a confirmation with one tap.
When time is up, play a sound and pulse the screen. **Three independent timers**
support countdowns, stopwatches, and expiry actions.

Chain up to three actions per tap or long-press, with delays and optional button
confirmations. Swipes, full-screen taps, physical buttons, boot, MQTT messages,
and camera motion can trigger actions where supported. Notifications, pulsing
alerts, beep patterns, and MP3 sounds provide the feedback.

[Configure actions](docs/pad-editor-guide.md#actions-tap-and-long-press) and
[timers](docs/pad-editor-guide.md#timer-actions).

### Images, Cameras & Music: More Than a Control Panel

Turn an unused display into a **JPEG/PNG slideshow**, advance photos with buttons,
or bring remote images and camera views into your dashboard.

Camera builds add preview, snapshots, a camera roll, and MJPEG streaming.
Local motion detection can trigger actions or keep the display awake without
cloud processing. It detects movement, not a person sitting still.

Audio builds can play your **MP3 library** with metadata, progress, and volume
controls. Supported music-analysis builds add audio-level and frequency-band
visualizations.

See [images](docs/web-portal-guide.md#image-library),
[camera workflows](docs/web-portal-guide.md#camera-snapshots), and
[music](docs/web-portal-guide.md#music-library).

## 🎨 Make It Your Own

### Build in Your Browser

Drag buttons into place, resize them, pick an icon, and preview the layout at your
device's aspect ratio. Save and see the result on your device. **Template pads**,
shared styling defaults, and building blocks let you reuse what works.

Offline widget type icons identify both local and inherited buttons in the pad
editor. Unlabeled widgets show a larger symbol and their name when space allows;
the grid remains a configuration overview rather than a live data preview.

Install parameterized recipes from your device-saved catalog, starting with the
repository's Pomodoro and Home Energy examples. Copy buttons or pads, and
import/export configurations as JSON to back up or share your work.

The portal also handles settings, media, health diagnostics, and screen previews,
with optional HTTP Basic Auth. Supported builds offer **over-the-air updates**
with rollback protection.

Builds with `HAS_REMOTE_LOG` offer **Device > Logs** with Full Mode access:
all configured ESP32-P4 boards and ESP32-S3 boards with more than 5 MB flash.
recent application logs, a retained startup snapshot, and browser copy/download.
Startup records include reset diagnostics and firmware identity. SDK-supported
flash coredumps appear separately with readable exception diagnostics, saved
registers, summary copy, and bounded download.
Access follows the device's authentication settings. Capture uses bounded PSRAM
storage and leaves serial output independent.
See the [log viewer guide](docs/web-portal-guide.md#device-logs) and
[log API reference](docs/dev/web-portal.md#remote-logs).

`POST /api/debug/crash` supports abort, assertion, and invalid-write tests,
with no web UI. `DEBUG_CRASH_API_ENABLED` defaults off on every board. Enable
it explicitly only in an isolated dev/test build; never ship it enabled in
production firmware. Remove the opt-in and rebuild after testing. See
[crash injection](docs/dev/web-portal.md#development-crash-injection) for
commands, authentication, and dump-overwrite risks.

[Open the pad editor guide](docs/pad-editor-guide.md) or
[tour the web portal](docs/web-portal-guide.md).

### Ask an AI Assistant to Help

"Create a pad with a clock, a CPU gauge, and a kitchen-lights button."

The built-in **Model Context Protocol (MCP) server** lets compatible clients such
as VS Code Copilot Chat, Cursor, and Claude Desktop inspect the device, resolve
bindings, control available features, and author pads and buttons.
It is off by default, requires a token, and separates read, control, and pad
authoring permissions. Enable it only on a trusted LAN; its HTTP transport is not
encrypted.

[Connect your assistant](docs/mcp-guide.md).

### Add Something the Built-In Widgets Cannot Do

A Nixie clock, a word clock, Matrix rain, or a clock with a game running inside it:
**native Extensions** add custom visualizations and interactions to your buttons.
Download a package for your device target, upload it through the portal, and
configure each button that uses it.

Extensions run native code, so install only modules you trust. Support and slot
capacity depend on the board; oversized packages are rejected.

[Browse the Extensions catalog](https://jantielens.github.io/esp32-macropad/extensions.html)
or [build your own](docs/dev/extensions.md).

## 🔬 Beyond the Macropad

### A Dashboard That Sleeps Between Updates

An **E-Paper Frame** wakes, fetches an image, refreshes the panel, and returns to
deep sleep. Rotate images, choose hourly refresh windows, and estimate battery
life in the portal. Supported reTerminal E1003 service setups can cache a batch
for offline refreshes between network syncs.

Want buttons instead? **Interactive E-Paper** runs the customizable pad UI with
black-and-white or grayscale rendering. It is a different firmware experience
from the sleep-first image frame.

[Choose an e-paper experience](docs/epaper-architectures.md) or
[set up an E-Paper Frame](docs/epaper-frame-guide.md).

### A Sensor Node Without a Screen

Report temperature, humidity, pressure, battery level, or presence with the
appropriate sensors and board. Run continuously or wake periodically to publish
over MQTT and go back to sleep. BLE telemetry builds can send **BTHome** sensor
advertisements without starting Wi-Fi on normal wakes. The browser portal still
handles configuration, even when there is no display.

### ☕ Your Next Brew, Measured

The **Coffee Scale** firmware combines a load cell with live weight and flow-rate
readings. Follow stage-based espresso or pour-over templates, then revisit the
recorded brew history. Calibration, templates, and brew logs live in the web portal.

[Build a Coffee Scale](docs/device-classes/coffee-scale/README.md).

### 🎞️ Tools for the Analog Darkroom

The **Darkroom Timer** handles enlarger exposures, f-stop test strips, light
metering, and print logs, with Shelly Wi-Fi relays controlling the enlarger and
safelight. The **Shutter Tester** measures camera shutter speeds and curtain timing
with photodiodes, and saves sessions for comparison.

Explore the [Darkroom Timer](docs/device-classes/darkroom-timer/README.md) and
[Shutter Tester](docs/device-classes/shutter-tester/user-guide.md).

### 🎙️ Voice as Another Input and Output

The **Voice Assistant** variant records speech, transcribes it through Azure,
and can publish the result over MQTT. Speak binding-driven text using cloud
text-to-speech for audible replies. It provides the voice pieces for your own
automation, not a standalone conversational assistant; cloud credentials and an
external reply workflow are required for that experience.

[Set up the Voice Assistant](docs/web-portal-guide.md#voice-assistant).

## 📱 Supported Hardware

From a compact round display to a 7-inch control panel or a battery-powered
e-paper frame, choose the board that fits the job. Features depend on the exact
board and firmware variant; the [installer](https://jantielens.github.io/esp32-macropad/)
helps you choose the right combination.

Persistent storage uses internal flash or a MicroSD card, depending on the
target. It holds pad configurations, images, sounds, and recorded data such as
brew logs or shutter-test sessions. SD-primary variants give media and recordings
more room without using the board's internal flash for those files.

| Board | Chip | Display | Resolution | Shape |
|-------|------|---------|------------|-------|
| **Cheap Yellow Display (CYD) v2** | ESP32 | 2.8" TFT LCD | 320 x 240 | Landscape |
| **Guition ESP32-S3-4848S040** | ESP32-S3 | 4.0" IPS LCD | 480 × 480 | Square |
| **Guition JC3248W535** | ESP32-S3 | 3.5" IPS LCD | 480 × 320 | Landscape |
| **Guition JC4827W543C** | ESP32-S3 | 4.3" IPS LCD | 480 x 272 | Landscape |
| **JC3636W518** | ESP32-S3 | 3.6" IPS LCD | 360 × 360 | Round |
| **Waveshare ESP32-P4 Touch LCD 4B** | ESP32-P4 | 4.0" IPS LCD | 720 × 720 | Square |
| **Guition JC4880P433** | ESP32-P4 | 4.3" IPS LCD | 480 x 800 | Portrait |
| **Guition JC1060P470C** | ESP32-P4 | 7.0" IPS LCD | 1024 × 600 | Rectangle |
| **Soldered Inkplate 5V2** | ESP32 | 5.17" 3-bit grayscale e-paper | 720 × 1280 | Portrait |
| **Soldered Inkplate 6FLICK** | ESP32 | 6.0" e-paper, Frame or Interactive firmware | 1024 x 758 | Landscape |
| **Seeed reTerminal E1003** | ESP32-S3 | 10.3" e-paper, Frame or Interactive firmware | 1404 x 1872 | Portrait |
| **ESP32-C3 Super Mini** | ESP32-C3 | Headless sensor node | N/A | N/A |
| **DFRobot FireBeetle 2 ESP32-C6** | ESP32-C6 | Headless AHT10 sensor node | N/A | N/A |

### Pick the Right Variant

| Capability | What to check |
|------------|---------------|
| USB keyboard, mouse, and gamepad | JC3248W535, JC1060P470C, JC4880P433, JC3636W518, their `-sd` variants, and ESP32-P4 LCD 4B Macropad builds |
| BLE keyboard | Board-dependent; both JC3636W518 variants disable BLE |
| Native Extensions | Supported ESP32-P4 and ESP32-S3 builds, plus Inkplate 6FLICK Interactive; slot capacity varies |
| Audio, microphones, and cameras | Hardware and firmware support vary; check the exact installer target |
| SD primary storage | JC1060P470C, JC3636W518, and JC4880P433 `-sd` builds require a FAT32 card and do not fall back to internal flash |
| OTA updates | Not available on every target, including FireBeetle 2 AHT10 and Inkplate 6FLICK Interactive |

Specialized targets include `jc4880p433-nau7802` and `jc4880p433-hx711`
(Coffee Scale), `jc4880p433-darkroom`, `jc4880p433-shutter`, and
`esp32-p4-lcd4b-voice`. Sensors and other external hardware must be added as
described in their setup guides. See [device classes](docs/device-classes/README.md)
for the build-time selection and branding model.


More boards are welcome. The [modular display and touch drivers](docs/dev/display-touch-architecture.md)
provide a starting point for adding yours.

## 🚀 Getting Started

### Install Firmware

Use the [online firmware installer](https://jantielens.github.io/esp32-macropad/).
No compiler or command-line tools are needed.

1. Choose a device class, then select your exact board and firmware variant.
2. Open the linked USB flash page in **Chrome** or **Edge** (WebSerial required).
3. Connect your board via USB, click **Connect**, and follow the installer prompts.

Already running an OTA-capable build? [Update over Wi-Fi](https://jantielens.github.io/esp32-macropad/update.html)
from the site or use the device's web portal.

### First-Time Setup

After flashing, the device creates its own Wi-Fi hotspot for initial configuration:

1. Connect to the device's Wi-Fi hotspot. Macropad builds use `ESP32-MACROPAD-XXXXXX`; other device classes use their own prefix.
2. Configure your Wi-Fi credentials in the captive portal.
3. After the device reboots, open `http://<device-name>.local` on your network.

On a Macropad build, make your first button do something you can see:

1. Open **Pads**, choose a pad, and add a button labeled `Hello`.
2. Add a **Show notification** tap action with the message `Hello!`.
3. Save, open that pad on the device, and tap the button to see the message.

No Home Assistant connection or HID setup is needed for this first test.

For HID controls, select your transport in **Connectivity > Keyboard, Mouse & Gamepad**,
save, and reboot. Output defaults to Off. On dual-USB-C P4 boards, use native
USB/OTG for HID and USB-UART for flashing and logs; see
[USB connector and power guidance](docs/web-portal-guide.md#keyboard).

[Follow the first-time setup guide](docs/first-time-setup.md).

## 📚 Documentation

| Guide | Description |
|-------|-------------|
| [First-Time Setup](docs/first-time-setup.md) | Initial configuration after flashing |
| [Web Portal Guide](docs/web-portal-guide.md) | Complete guide to all portal features |
| [Pad Editor Guide](docs/pad-editor-guide.md) | Layouts, widgets, actions, bindings, and worked examples |
| [MCP Server Guide](docs/mcp-guide.md) | AI-assisted inspection, control, configuration, and pad authoring |
| [E-Paper Frame Guide](docs/epaper-frame-guide.md) | Image sources, scheduling, battery use, and wake behavior |
| [Home Assistant Integration](docs/ha-integration-guide.md) | HA entity reference, audio control, and automation examples |
| [Device Classes](docs/device-classes/README.md) | Specialized firmware variants and their guides |
| [Home Assistant + MQTT (dev)](docs/dev/home-assistant-mqtt.md) | MQTT topic structure and HA auto-discovery internals |
| [Extension Developer Guide](docs/dev/extensions.md) | Build, install, and author trusted native ESP32-P4, ESP32-S3, and classic ESP32 Extensions |

### Developer Documentation

Building from source, contributing, or adding new board support? See the [developer docs](docs/dev/).
For portal-only UI work, use the [device-free portal development server](docs/dev/web-portal.md#local-device-free-development) to work against production assets without flashing a board. Its default P4 profile includes memory-backed pad editor fixtures and failure scenarios.

### Running Tests

Host-native unit and integration tests run on the development machine (no ESP32 needed). They require CMake 3.20 or later. If CMake is installed through ESP-IDF but is not on `PATH`, use its toolchain directory for the commands below:

```bash
export PATH="$HOME/.espressif/tools/cmake/<version>/bin:$PATH"
```

```bash
cmake -S . -B build/host-tests
cmake --build build/host-tests --parallel
ctest --test-dir build/host-tests --output-on-failure
```

## 📄 License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

---

Made with ❤️ for ESP32 by [@jantielens](https://github.com/jantielens)
