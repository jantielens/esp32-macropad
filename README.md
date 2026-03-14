# ESP32 Macropad

Turn your ESP32 touch screen into a powerful, fully customizable smart home control panel — no coding required.

ESP32 Macropad is open-source firmware that transforms affordable ESP32 development boards with touch displays into beautiful, configurable macro pads. Design your own button layouts with live data, icons, and colors — all from a browser-based editor. Connect to Home Assistant via MQTT and put your smart home at your fingertips.

## ✨ Features

### Display & Touch
- **LVGL-powered UI** — smooth, modern interface on all supported displays
- **Customizable pad layouts** — up to 16 pads with configurable grids (up to 8×8)
- **Rich button styling** — colors, borders, corner radius, icons (emoji + Material Symbols), background images, and per-label style overrides (font size, alignment, text overflow mode)
- **Multi-span buttons** — buttons can span multiple columns and rows
- **Bar chart widgets** — visualize data with color-coded threshold bars
- **Gauge widgets** — arc-based gauges with ticks, needle, labels and color thresholds
- **Sparkline widgets** — mini trend lines with background data collection and auto-scaling
- **Screen saver** — automatic backlight fade with pixel-shift burn-in prevention

### Connectivity & Smart Home
- **MQTT with Home Assistant Discovery** — auto-registers as an HA device, no YAML needed
- **Live data bindings** — display MQTT topics, device health, or date/time directly on buttons
- **Button actions** — tap or long-press to publish MQTT messages or navigate screens
- **Toggle state** — buttons reflect on/off state from MQTT topics
- **Remote screen control** — switch pads from Home Assistant

### Web Configuration Portal
- **Browser-based setup** — configure everything over Wi-Fi, no tools needed
- **Visual pad editor** — drag-and-drop style grid editor with live preview
- **Copy/paste & import/export** — clone buttons, pages, or entire device configs
- **OTA firmware updates** — update over Wi-Fi from the portal or the online installer
- **Real-time health dashboard** — CPU, memory, temperature, Wi-Fi signal, MQTT status
- **HTTP Basic Auth** — optional password protection for the portal

### Background Images
- **Live camera feeds** — display JPEG/PNG streams from security cameras or other sources
- **Auto-refresh** — configurable refresh interval for dynamic images
- **Letterbox or cover** — choose how images fit the button

## 💡 What Can You Build?

- **Home Assistant control panel** — lights, switches, scenes, climate — all one tap away
- **Energy monitor dashboard** — real-time solar, battery, and grid power visualization
- **Media controller** — play, pause, skip, volume for your media players
- **Smart home remote** — replace a drawer full of remotes with one touch screen
- **Status display** — show weather, time, sensor readings, or system stats
- **Security camera viewer** — live camera thumbnails on your desk

## 📱 Supported Devices

ESP32 Macropad runs on these ESP32 development boards with touch screens:

| Board | Chip | Display | Resolution | Shape |
|-------|------|---------|------------|-------|
| **Guition ESP32-S3-4848S040** | ESP32-S3 | 4.0" IPS LCD | 480 × 480 | Square |
| **Guition JC3248W535** | ESP32-S3 | 3.5" IPS LCD | 480 × 320 | Landscape |
| **JC3636W518** | ESP32-S3 | 3.6" IPS LCD | 360 × 360 | Round |
| **Waveshare ESP32-P4 Touch LCD 4B** | ESP32-P4 | 4.0" IPS LCD | 720 × 720 | Square |
| **Guition JC4880P433** | ESP32-P4 | 4.3" IPS LCD | 800 × 480 | Rectangle |

All boards feature capacitive touch and are widely available from AliExpress and similar retailers.

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

Everything is configured from your browser — no flashing or coding needed after the initial install.

- **Home page** — operating mode, display settings, and sensor configuration
- **Pads page** — visual pad editor for designing button layouts
- **Network page** — Wi-Fi, MQTT, device name, static IP, and security
- **Firmware page** — OTA updates, manual upload, and factory reset

The pad editor lets you design button layouts visually: pick icons, set colors, add MQTT bindings for live data, configure tap actions — then save and see it instantly on your device.

**📖 [Full web portal guide →](docs/web-portal-guide.md)**

## 📖 Documentation

| Guide | Description |
|-------|-------------|
| [First-Time Setup](docs/first-time-setup.md) | Initial configuration after flashing |
| [Web Portal Guide](docs/web-portal-guide.md) | Complete guide to all portal features |
| [Home Assistant + MQTT](docs/dev/home-assistant-mqtt.md) | MQTT integration with HA auto-discovery |

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
