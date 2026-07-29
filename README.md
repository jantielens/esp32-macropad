# ESP32 Macropad

Turn your ESP32 display board into a powerful, fully customizable smart home control panel with no coding required.

ESP32 Macropad is open-source firmware that transforms affordable ESP32 development boards into configurable control surfaces and dashboards. Interactive boards use a touch-first pad UI. E-paper boards, such as the Inkplate 5V2, use a battery-focused image carousel + hourly schedule model with deep sleep between refreshes. Both are configured from the same web portal and integrate with Home Assistant over MQTT.

## ✨ Features

### Pads, buttons & widgets
- **Up to 16 pads** with configurable grids (up to 8×8, board-dependent), per-pad backgrounds, and multi-cell button spans
- **Rich button styling** — colors, borders, corner radius, icons (emoji + Material Symbols) with configurable icon position, background images, icon + center label co-display, and a per-label style DSL (font family, size, alignment, overflow)
- **Widgets inside buttons** — gauge (multi-ring with target zones), bar chart (vertical/horizontal, up to 4 bars with per-bar captions and gauge-style scale options), sparkline (multi-line with reference markers), table, list (scrollable item picker), **rocker** (split-button up/down or left/right), and **numeric rocker** (4-zone fine/coarse adjustment)
- **Smooth animations** — gauges, bars, and needles ease into new values instead of jumping
- **Template pads & device-wide button defaults** — define appearance once, inherit everywhere
- **Building blocks** — drop pre-built button groups such as System Info into any pad with a single click
- **Custom fonts** — DSEG7 (7-segment), Bebas, and Doto pixel font, in addition to the default Montserrat
- **Screen saver** with backlight fade, pixel-shift burn-in prevention, panel hardware sleep, LVGL throttle, and per-pad wake redirect

### Live data with bindings
A simple `[scheme:params]` syntax pulls live data into any label, color, or widget — with format strings, fallbacks, and inline expressions.

- **`[mqtt:topic;path]`** — any MQTT topic, with JSON path extraction
- **`[health:key]`** — CPU, memory, PSRAM, WiFi/BLE status, IP, uptime, and 30+ more device metrics
- **`[time:format;tz]`** — NTP-synced clocks with timezone and sub-second precision
- **`[expr:...]`** — math, comparisons, ternary, and a `threshold()` helper for multi-zone color logic
- **`[pad:name]`** — name a binding once per pad and reuse it everywhere
- **`[timer:N]`** — render on-device timer values in any format
- **Dynamic colors** — background, text, border, and widget colors all accept binding expressions
- **Dynamic state** — `enabled` / `disabled` / `hidden` per button via the same binding system
- **Real-time syntax validator** in the pad editor catches typos and invalid expressions as you type
- **Action values** — MQTT payloads, BLE key sequences, beep patterns, volume/brightness values, and timer values all resolve bindings at dispatch time

### Inputs & automation
- **Multi-action buttons** — chain up to 3 actions per tap and per long-press (publish MQTT, play sound, navigate, send keystrokes, etc.)
- **Swipe gestures** — configure left/right/up/down on any screen with the same actions as buttons
- **Hardware buttons** — map a board's physical GPIO buttons to tap and long-press action chains (works on headless boards too)
- **Boot actions** — run a sequence of actions automatically when the device starts
- **MQTT triggers** — dispatch action chains when a matching MQTT message arrives, no button or screen required (works on headless boards with a physical button too)
- **On-device timers** — 3 independent timers whose Start and Toggle actions carry their stopwatch or countdown mode and duration, with per-slot expire actions and live `[timer:N]` bindings
- **Notification bubble** — display a floating message via a button action or remotely from Home Assistant

### Audio & feedback
- **Beeps & sound files** — pattern DSL (`500:40 60 800:40`) for short cues and MP3 file playback (≤512 KB) for longer sounds
- **Audio feedback** for taps, long-press, and timer expiry, with per-button or per-action overrides
- **Volume & brightness actions** — adjust device volume or backlight from any button
- **Hardware-accelerated audio** on ESP32-P4 boards via the ES8311 codec

### Smart home & connectivity
- **MQTT with Home Assistant auto-discovery** — registers as a full HA device with sensors, buttons, siren, volume, screen selector, and notification entities (no YAML needed)
- **Home Assistant service buttons** — call any HA service (toggle lights, run scenes, open covers) directly over the REST API from a button, swipe, or boot action
- **MCP server for AI assistants** — a built-in [Model Context Protocol](docs/mcp-guide.md) endpoint lets a local LLM client (Claude Desktop, Cursor, VS Code Copilot Chat) inspect and control the device through chat; off by default, token-secured, with separate read and control permissions
- **Bluetooth HID keyboard** (ESP32-P4) — send keystrokes, modifier combos, media keys, and multi-step sequences to any paired host with single-owner pairing
- **Remote control from HA** — switch screens, trigger beeps, play tones, set volume, send notifications
- **Resilient WiFi** — event-driven tiered reconnect keeps the display responsive through outages, with gateway-ping liveness detection
- **Live camera feeds** — JPEG, PNG, and **MJPEG streaming** support (8–15 fps, with hardware JPEG decode + PPA scaling on ESP32-P4)
- **OTA updates** with rollback protection — flash from the web portal or the online installer

### Web configuration portal
- **Visual pad editor** — drag-to-move and drag-to-resize buttons, live preview matching your device's aspect ratio
- **Copy/paste & import/export** — clone buttons, pads, or entire device configurations to JSON
- **Browser-based setup** — Wi-Fi, MQTT, security, and all device settings, no tools needed
- **Real-time health dashboard** — CPU, memory, temperature, WiFi signal, MQTT and BLE status
- **Optional HTTP Basic Auth** for portal access

## 💡 What Can You Build?

- **Home Assistant control panel** — lights, switches, scenes, climate — all one tap away
- **Energy monitor dashboard** — real-time solar, battery, and grid power visualization
- **Battery-powered e-paper dashboard** — rotate through one or more images, apply hourly refresh windows, refresh, and go back to sleep
- **Media controller** — play, pause, skip, volume for your media players
- **Bluetooth macro keyboard** — launch apps, paste snippets, control presentations, or trigger shortcuts on your PC or Mac
- **Smart home remote** — replace a drawer full of remotes with one touch screen
- **Status display** — show weather, time, sensor readings, or system stats
- **Security camera viewer** — live camera thumbnails on your desk

## 📱 Supported Devices

ESP32 Macropad runs on these ESP32 development boards:

| Board | Chip | Display | Resolution | Shape |
|-------|------|---------|------------|-------|
| **Guition ESP32-S3-4848S040** | ESP32-S3 | 4.0" IPS LCD | 480 × 480 | Square |
| **Guition JC3248W535** | ESP32-S3 | 3.5" IPS LCD | 480 × 320 | Landscape |
| **JC3636W518** | ESP32-S3 | 3.6" IPS LCD | 360 × 360 | Round |
| **Waveshare ESP32-P4 Touch LCD 4B** | ESP32-P4 | 4.0" IPS LCD | 720 × 720 | Square |
| **Guition JC4880P433** | ESP32-P4 | 4.3" IPS LCD | 800 × 480 | Rectangle |
| **Guition JC1060P470C** | ESP32-P4 | 7.0" IPS LCD | 1024 × 600 | Rectangle |
| **Soldered Inkplate 5V2** | ESP32 | 5.17" 3-bit grayscale e-paper | 720 × 1280 | Portrait |
| **Soldered Inkplate 6FLICK** | ESP32 | 6.0" 3-bit grayscale e-paper | 1024 × 758 | Landscape |
| **Seeed reTerminal E1003** | ESP32-S3 | 10.3" 16-level grayscale e-paper | 1404 × 1872 | Portrait |

Most boards feature capacitive touch and are widely available from AliExpress and similar retailers. The Inkplate 5V2, Inkplate 6FLICK, and Seeed reTerminal E1003 are the current non-touch e-paper targets.

### Device Classes

The firmware auto-detects a device class at build time based on board capability flags. The class drives mDNS naming, captive-portal SSID, Home Assistant model strings, the default device name, and how each board is presented on the flash page.

| Device Class | Detection | Brand Prefix | SSID Format | Boards |
|---|---|---|---|---|
| **Macropad** | `HAS_DISPLAY` (default) | `ESP32 Macropad` | `ESP32-MACROPAD-XXXXXX` | All touch-screen boards listed above |
| **E-Paper** | `HAS_EPAPER` | `ESP32-MP E-Paper` | `ESP32-MP-EPAPER-XXXXXX` | Inkplate 5V2, Inkplate 6FLICK, Seeed reTerminal E1003 |
| **Headless** | `!HAS_DISPLAY` | `ESP32-MP Headless` | `ESP32-MP-HEADLESS-XXXXXX` | Sensor-only boards (e.g. `esp32c3-withsensors`) |
| **Shutter Tester** | `IS_SHUTTER_TESTER` | `ESP32-MP Shutter Tester` | `ESP32-MP-SHUTTER-XXXXXX` | `jc4880p433-shutter` — see [docs/device-classes/shutter-tester/](docs/device-classes/shutter-tester/README.md) |
| **Coffee Scale** | `IS_COFFEE_SCALE` | `ESP32-MP Coffee Scale` | `ESP32-MP-SCALE-XXXXXX` | `jc4880p433-nau7802`, `jc4880p433-hx711` — see [docs/device-classes/coffee-scale/](docs/device-classes/coffee-scale/README.md) |
| **Darkroom Timer** | `IS_DARKROOM_TIMER` | `ESP32-MP Darkroom Timer` | `ESP32-MP-DARKROOM-XXXXXX` | `jc4880p433-darkroom` — see [docs/device-classes/darkroom-timer/](docs/device-classes/darkroom-timer/README.md) |

`XXXXXX` is the last six hex digits of the ESP32 chip ID. Per-board metadata (label, description, specs) lives in `src/boards/<board>/metadata.json` and is consumed by the flash page generator.

> **More boards welcome!** The firmware has a modular driver architecture that makes adding new boards straightforward. Check the [developer docs](docs/dev/display-touch-architecture.md) if you'd like to contribute.

## 🚀 Getting Started

### Install Firmware

The easiest way to flash ESP32 Macropad is through the **online installer** — no tools or compilers needed:

**👉 [ESP32 Macropad Firmware Installer](https://jantielens.github.io/esp32-macropad/)**

1. Open the installer in **Chrome** or **Edge** (WebSerial required)
2. Connect your board via USB
3. Select your board and click **Install**
4. Done! The firmware is flashed and ready to go

Already running ESP32 Macropad? You can also update over Wi-Fi (OTA) directly from the device's web portal or the installer site.

### First-Time Setup

After flashing, the device creates its own Wi-Fi hotspot for initial configuration:

1. Connect to the device's Wi-Fi network (`esp32-macropad-XXXXXX`)
2. A captive portal opens — configure your Wi-Fi credentials
3. The device reboots and joins your network
4. Access the configuration portal at `http://<device-name>.local`

**📖 [Detailed first-time setup guide →](docs/first-time-setup.md)**

## 🎛️ Web Configuration Portal

Everything is configured from your browser — no flashing or coding needed after the initial install. The portal is a single-page app with a responsive sidebar organizing settings into 8 categories:

- **Device** — operating mode, boot actions, timers, and swipe gestures
- **Display** — brightness, on-demand screen preview, screen saver, and button defaults
- **Pads** — visual pad editor for designing button layouts
- **Actions** — boot actions, swipe gestures, hardware buttons, and MQTT triggers
- **Connectivity** — Wi-Fi, MQTT, BLE, device name, static IP, and security
- **Audio** — volume, beep patterns, and sound files
- **Sensors** — sensor configuration for boards with sensor hardware
- **Firmware** — OTA updates, manual upload, and factory reset

The pad editor lets you design button layouts visually: drag-to-move and drag-to-resize buttons, pick icons, set colors, add MQTT bindings for live data, configure tap actions — then save and see it instantly on your device. Dark and light mode follow your OS preference.

**📖 [Full web portal guide →](docs/web-portal-guide.md)**

## 📖 Documentation

| Guide | Description |
|-------|-------------|
| [First-Time Setup](docs/first-time-setup.md) | Initial configuration after flashing |
| [Web Portal Guide](docs/web-portal-guide.md) | Complete guide to all portal features |
| [MCP Server Guide](docs/mcp-guide.md) | Connect a local AI assistant to inspect and control the device over MCP |
| [E-Paper Guide](docs/epaper-guide.md) | Detailed guide for the e-paper device class, carousel/schedule model, wake behavior, and status semantics |
| [Home Assistant Integration](docs/ha-integration-guide.md) | HA entity reference, audio control, and automation examples |
| [Home Assistant + MQTT (dev)](docs/dev/home-assistant-mqtt.md) | MQTT topic structure and HA auto-discovery internals |

### Developer Documentation

Building from source, contributing, or adding new board support? See the [developer docs](docs/dev/).

### Running Tests

Host-native unit and integration tests run on the development machine (no ESP32 needed):

```bash
./tests/run_tests.sh
```

## 📄 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

---

Made with ❤️ for ESP32 by [@jantielens](https://github.com/jantielens)
