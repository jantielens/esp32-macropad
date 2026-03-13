# Web Portal Guide

The ESP32 Macropad includes a built-in web portal for configuring every aspect of your device — from Wi-Fi settings to pad layouts — all from your browser.

## Accessing the Portal

| Mode | When | URL |
|------|------|-----|
| **AP Mode** (first boot / factory reset) | Wi-Fi not configured | Connect to the device's Wi-Fi, then go to `http://192.168.4.1` |
| **Full Mode** (normal operation) | Connected to your Wi-Fi | `http://<device-name>.local` or the device's IP address |

In AP mode, only the Network page is available. In Full mode, all four pages are accessible.

## Header & Health Monitoring

The portal header shows real-time device info at a glance:

- **Firmware version** — currently installed version
- **Chip** — model and silicon revision (e.g., ESP32-S3 rev 2)
- **Cores** — number of CPU cores
- **CPU frequency** — in MHz
- **Flash** — flash memory size
- **PSRAM** — external RAM status and size

### Health Badge

The orange **CPU** badge in the header shows real-time CPU usage with a breathing green dot that pulses on each update.

**Click the badge** to expand a full health overlay showing:

- **Uptime** — how long the device has been running
- **Reset reason** — why the device last restarted
- **CPU usage** — percentage (based on FreeRTOS IDLE task measurement)
- **Core temperature** — internal chip temperature sensor
- **Heap memory** — free, minimum, largest block, and fragmentation
- **PSRAM** — same metrics for external RAM (when present)
- **Flash usage** — firmware size
- **Filesystem** — LittleFS partition usage (for icons)
- **MQTT** — connection status and publish timing
- **Display** — FPS and render timing
- **Wi-Fi signal** — RSSI and IP address

---

## Home Page

*Available in Full mode only.*

The Home page provides a welcome overview with quick links to the other pages, plus device behavior settings.

### Operating Mode & Cadence

Controls how the device operates:

| Setting | Description |
|---------|-------------|
| **Operating Mode** | **Always-On** keeps all services running continuously. **Duty-Cycle** wakes periodically, publishes data, then goes back to sleep to save power |
| **Transport Mode** | Choose which protocols publish telemetry: BLE, MQTT, or both |
| **Cycle Interval** | How often the device wakes and publishes in duty-cycle mode (seconds) |
| **Portal Idle Timeout** | Auto-sleep timeout when in config/AP mode (seconds, 0 = disabled) |
| **WiFi Backoff Max** | Maximum delay between WiFi reconnection attempts (seconds) |

#### MQTT Transport Settings

| Setting | Description |
|---------|-------------|
| **MQTT Payload Scope** | What to include in MQTT publishes: sensor data only, diagnostics only, or everything |

#### BLE Transport Settings

| Setting | Description |
|---------|-------------|
| **Burst Duration** | How long each BLE advertising burst lasts (ms) |
| **Gap Duration** | Pause between bursts (ms) |
| **Bursts** | Number of bursts per cycle (1–10) |
| **Adv Interval** | BLE advertising interval within a burst (ms) |

### Display Settings

*Shown only on boards with a display.*

| Setting | Description |
|---------|-------------|
| **Backlight Brightness** | Slider (0–100%). Changes take effect immediately; save to persist across reboots |
| **Current Screen** | Switch the active screen on the device. This is a live control, not saved — resets on reboot |

#### Screen Saver (Burn-in Prevention)

Protects your LCD from burn-in by turning off the backlight after a period of inactivity. A built-in pixel-shift mechanism moves content slightly each sleep cycle to prevent ghosting.

| Setting | Description |
|---------|-------------|
| **Enable screen saver** | Turn on/off automatic sleep |
| **Idle Timeout** | Seconds of inactivity before sleep (0 = disabled) |
| **Fade Out** | How long the backlight fade-out takes (ms, 0 = instant) |
| **Fade In** | How long the backlight fade-in takes on wake (ms, 0 = instant) |
| **Wake on touch** | Wake the display by touching the screen |
| **Wake on MQTT Binding** | Binding expression that keeps the screen awake while it resolves to ON (e.g. `[mqtt:devices/node/presence/state]`) |

---

## Pads Page

*Available in Full mode only.*

The Pads page is the heart of ESP32 Macropad — this is where you design your touch screen layouts. It supports up to 8 independent pads, each with a configurable grid of buttons that can display live data, trigger MQTT actions, and change color dynamically.

The Pads page has its own floating footer with **Save Pad**, **Show on Device**, and a **More** menu for bulk operations (Fill, Copy/Paste Pad, Export/Import). This is completely separate from the device config Save & Reboot footer on other pages.

Label fields in the button editor support explicit line breaks with `\n` (for example, `Line 1\nLine 2`). This applies to button labels (Top/Center/Bottom) and gauge start labels.

Switching between pads or navigating away with unsaved changes shows a confirmation dialog to prevent accidental data loss.

For the complete guide — including binding template syntax, widget configuration (bar charts, gauges, sparklines), label styling, dynamic colors, pad bindings (named data sources), and real-world examples (including a dual-binding gauge power-balance setup) — see the **[Pad Editor Guide](pad-editor-guide.md)**.

---

## Network Page

*Available in both AP and Full mode.*

### WiFi Settings

| Setting | Description |
|---------|-------------|
| **WiFi SSID** | Your wireless network name (required, max 31 characters) |
| **WiFi Password** | Network password (leave empty for open networks) |

### Device Settings

| Setting | Description |
|---------|-------------|
| **Device Name** | A friendly name for your device (e.g., "Kitchen Pad"). Used in the web portal, Home Assistant, and browser discovery |
| **mDNS Name** | Auto-generated from the device name. This is the `.local` address you use to access the portal (shown as read-only) |

### Network Configuration (Optional)

For assigning a static IP instead of using DHCP:

| Setting | Description |
|---------|-------------|
| **Fixed IP Address** | Leave empty for DHCP (recommended for most setups) |
| **Subnet Mask** | Required if using a fixed IP |
| **Gateway** | Required if using a fixed IP |
| **DNS Server 1 / 2** | Optional; DNS 1 defaults to gateway if not set |

### MQTT Settings (Optional)

*Shown only when MQTT support is enabled in the firmware.*

| Setting | Description |
|---------|-------------|
| **MQTT Host** | Broker hostname or IP address. Leave empty to disable MQTT |
| **MQTT Port** | Default: 1883 |
| **MQTT Username / Password** | Credentials for your broker (optional) |

### Security (Optional)

| Setting | Description |
|---------|-------------|
| **Enable HTTP Basic Auth** | Require a username and password to access the web portal (Full mode only — AP mode stays open for initial setup) |
| **Username / Password** | Credentials for portal access |

---

## Firmware Page

*Available in Full mode only.*

### Online Update (GitHub Pages)

Opens the ESP32 Macropad Firmware Installer site in a new tab, pre-filled with your device's address. From there you can update over Wi-Fi (OTA) with the latest release.

### Manual Update (Upload)

Upload a compiled `.bin` firmware file directly from your computer. A progress bar shows the upload status, and the device reboots automatically when complete.

### Factory Reset

Erases all configuration and restarts the device in AP mode. You'll need to go through the [first-time setup](first-time-setup.md) again.

> **Warning**: This cannot be undone. All settings, pad layouts, and stored icons will be lost.

---

## Saving & Rebooting

The Home and Network pages share a floating footer with three actions:

| Button | What it does |
|--------|--------------|
| **Save and Reboot** | Saves all settings and restarts the device (required for WiFi/network changes) |
| **Save** | Saves settings without rebooting (applied on next restart) |
| **Reboot** | Restarts without saving |

The **Pads** page has its own separate footer — see [Pads Page](#pads-page) above.

Each page only saves the fields shown on that page — saving on the Home page won't clear your Network settings.

After a reboot, the portal shows an automatic reconnection dialog. If it can't reconnect (e.g., the device name changed), it provides a manual link with the new address.

---

## Tips & Tricks

- **Bookmark your device**: After setup, bookmark `http://<device-name>.local` for quick access
- **Back up your config**: Use **Export Device Config** regularly — it saves everything into a single JSON file
- **Clone devices**: Export from one device, import on another to duplicate your setup
- **Copy/paste buttons**: The button editor has Copy and Paste buttons to quickly duplicate button settings across cells. Both keep the editor open so you can keep working, and column/row spans are preserved when space allows
- **Binding templates**: Mix static text with live data — e.g., `Solar: [mqtt:home/solar;power;%.0f W]` shows "Solar: 3500 W"
- **Security**: Enable HTTP Basic Auth on devices accessible from outside your home network
