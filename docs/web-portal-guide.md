# Web Portal Guide

The ESP32 Macropad includes a built-in web portal for configuring every aspect of your device — from Wi-Fi settings to pad layouts — all from your browser.

## Accessing the Portal

| Mode | When | URL |
|------|------|-----|
| **AP Mode** (first boot / factory reset) | Wi-Fi not configured | Connect to the device's Wi-Fi, then go to `http://192.168.4.1` |
| **Full Mode** (normal operation) | Connected to your Wi-Fi | `http://<device-name>.local` or the device's IP address |

In AP mode, only the Network page is available. In Full mode, all three pages are accessible.

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

The Home page is where you configure the device's behavior and pad layouts.

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

### Pad Editor

The pad editor is the heart of ESP32 Macropad — this is where you design your touch screen layouts.

#### Pad-Level Settings

| Setting | Description |
|---------|-------------|
| **Pad selection** | Switch between Pad 1–8 |
| **Pad Name** | Optional name for this pad page (shown in Home Assistant and on-device) |
| **Columns / Rows** | Grid size, up to 8×8 |
| **Wake Screen** | Which screen to show when the screensaver wakes up |
| **Background** | Pad background color |

#### Button Editor

Click any cell in the grid preview to open the button editor dialog:

**Labels**
- **Top / Center / Bottom label** — static text or live data via binding templates
- Binding syntax: `[mqtt:topic;json_path;format]`, `[health:key;format]`, `[time:format;timezone]`
- A built-in help dialog explains all available bindings
- **Label style override** — click the **Aa** button next to any label to reveal an advanced style input. Use a compact `key:value;...` syntax to fine-tune individual labels:
  - `font:24` — override font size (12, 14, 18, 24, 32, or 36 px)
  - `align:left` / `right` / `center` — horizontal text alignment
  - `y:-2` — vertical pixel offset
  - `mode:clip` / `scroll` / `dot` / `wrap` — text overflow behavior (`dot` adds "..." for long text)
  - `color:#FF0` — override the label's text color
  - Example: `font:36;align:left;mode:dot` — large left-aligned text with ellipsis
  - A **?** button next to the style input opens a reference with all properties and examples

**Icons**
- **Icon Type** — None, Emoji, or Material Symbol
- **Icon Size %** — scale the icon (0 = auto)
- Emoji icons render in full color; Material Symbols can be tinted with the text color

**Widget Type**
- **Normal Button** — standard button with labels, icon, and colors
- **Bar Chart** — vertical bar visualization driven by MQTT data; includes configurable thresholds and color zones

**Styling**
- **Background / Text / Border colors** — full color picker with quick swatches
- **Border width and corner radius** — fine-tune the look
- **Column span / Row span** — make buttons span multiple grid cells

**Actions**
- **Tap Action** — what happens when the button is pressed: navigate to a screen, go back, or publish an MQTT message
- **Long-Press Action** — same options, triggered on a long press
- For MQTT actions, configure the topic and payload

**Colors** (collapsible)
- **Background / Text / Border** — pick a static `#hex` color, or enter a binding expression like `[expr:[mqtt:topic;path]>50?"#ff0000":"#00ff00"]` for dynamic coloring
- **Default color** — fallback color used when a binding hasn't resolved yet or returns an error

**Background Image** (optional)
- **Image URL** — HTTP URL to a JPEG or PNG image
- **Auth User / Password** — credentials for protected images (e.g., security cameras)
- **Refresh Interval** — how often to re-fetch (ms, 0 = fetch once)
- **Letterbox** — fit the image with black bars instead of cropping to fill

#### Pad Actions (More Menu)

The **More ▾** dropdown provides bulk operations:

| Action | Description |
|--------|-------------|
| **Fill Pad** | Fill all empty cells with the copied button |
| **Copy Pad** | Copy the entire pad layout to clipboard |
| **Paste Pad** | Overwrite the current pad with the clipboard |
| **Export Pad** | Download the current pad as a JSON file |
| **Import Pad** | Load a pad from a JSON file |
| **Export Device Config** | Download all settings + all 8 pads as a single JSON file |
| **Import Device Config** | Restore settings + pads from an exported JSON (triggers reboot) |
| **Clear Pad** | Remove all buttons from the current pad |

> **Tip**: Use Export/Import Device Config to back up your entire setup, or to clone settings from one device to another.

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

The floating footer at the bottom of every page offers three actions:

| Button | What it does |
|--------|--------------|
| **Save and Reboot** | Saves all settings and restarts the device (required for WiFi/network changes) |
| **Save** | Saves settings without rebooting (applied on next restart) |
| **Reboot** | Restarts without saving |

Each page only saves the fields shown on that page — saving on the Home page won't clear your Network settings.

After a reboot, the portal shows an automatic reconnection dialog. If it can't reconnect (e.g., the device name changed), it provides a manual link with the new address.

---

## Tips & Tricks

- **Bookmark your device**: After setup, bookmark `http://<device-name>.local` for quick access
- **Back up your config**: Use **Export Device Config** regularly — it saves everything into a single JSON file
- **Clone devices**: Export from one device, import on another to duplicate your setup
- **Copy/paste buttons**: The button editor has Copy and Paste buttons to quickly duplicate button settings across cells
- **Binding templates**: Mix static text with live data — e.g., `Solar: [mqtt:home/solar;power;%.0f W]` shows "Solar: 3500 W"
- **Security**: Enable HTTP Basic Auth on devices accessible from outside your home network
