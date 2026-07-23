---
title: Web Configuration Portal
description: Developer reference for the ESP32 Macropad web portal architecture, pages, and REST APIs
---

The ESP32 template includes a full-featured web portal for device configuration, monitoring, and firmware updates. The portal uses an async web server with captive portal support for initial setup.

## Overview

The web portal provides:
- WiFi configuration via captive portal
- Real-time device health monitoring
- Over-the-air (OTA) firmware updates
- REST API for programmatic access
- Optional HTTP Basic Authentication (Full Mode only)
- Responsive web interface (desktop & mobile)

## Portal Modes

### Core Mode (AP with Captive Portal)

**When it runs:**
- WiFi credentials not configured
- Configuration reset
- WiFi connection failed

**Access:**
- SSID: `ESP32-XXXXXX` (where XXXXXX is chip ID)
- IP: `http://192.168.4.1`
- Captive portal auto-redirects to configuration page

**Features:**
- One-page setup wizard (`setup` component, fragment `setup.fragment.html`) that handles WiFi SSID + password (required), friendly name, optional portal basic auth, and optional static IP. The wizard is the only Device-category nav item visible in AP mode and is selected automatically via the `ap_mode` + `primary.fragment` fields on `/api/portal/nav`.
- WiFi SSID and password setup
- Device name configuration
- Fixed IP settings (optional)

### Full Mode (WiFi Connected)

**When it runs:**
- Device successfully connected to WiFi network

**Access:**
- Device IP address (displayed in serial monitor)
- mDNS hostname: `http://<device-name>.local`

**Features:**
- All Core Mode features
- Real-time health monitoring
- OTA firmware updates
- Device reboot

**Optional Authentication:**
- If HTTP Basic Auth is enabled in configuration, the portal UI pages and all REST API endpoints require credentials.
- Authentication is only enforced in Full Mode.

## Device Discovery

When connected to WiFi, devices can be discovered using multiple methods:

### WiFi Hostname (DHCP)

The device sets its WiFi hostname using `WiFi.setHostname()` before connecting. This hostname appears in:
- Router DHCP client lists
- Network scanning tools (Fing, WiFiMan, etc.)
- DHCP server logs

**Format:** Sanitized device name (e.g., "ESP32 1234" → `esp32-1234`)

### mDNS (Bonjour)

The device advertises itself via mDNS with enhanced service records:

**Access:** `http://<hostname>.local` (e.g., `http://esp32-1234.local`)

**Platforms:**
- ✅ macOS (native support)
- ✅ Linux (requires `avahi-daemon`)
- ✅ iOS/iPadOS (native support)
- ✅ Android (native support in modern versions)
- ⚠️ Windows (requires Bonjour service)

**Service TXT Records:**
- `version` - Firmware version
- `model` - Chip model (ESP32/ESP32-C3/etc)
- `mac` - Last 4 hex digits of MAC address
- `ty` - Device type ("iot-device")
- `mf` - Manufacturer ("ESP32-Tmpl")
- `features` - Capabilities ("wifi,http,api")
- `note` - Description ("WiFi Portal Device")
- `url` - Configuration URL ("http://hostname.local")

**Discovery Example:**
```bash
# macOS/Linux
avahi-browse -rt _http._tcp

# Output includes TXT records:
# esp32-1234._http._tcp
#   hostname = [esp32-1234.local]
#   txt = ["version=0.0.1" "model=ESP32-C3" "mac=1234" "ty=iot-device" 
#          "mf=ESP32-Tmpl" "features=wifi,http,api" "note=WiFi Portal Device" 
#          "url=http://esp32-1234.local"]
```

### Router Discovery

Most routers display connected devices with their hostnames:
- Check router web interface → DHCP clients
- Look for device name (e.g., "esp32-1234")
- Note the assigned IP address

### Network Scanning Apps

Recommended apps for finding devices:
- **Fing** (iOS/Android) - Shows hostname and MAC
- **WiFiMan** (iOS/Android) - Network scanner
- **Angry IP Scanner** (Desktop) - Fast network scanner
- **nmap** (Linux/macOS) - Command-line scanner

## User Interface

### Multi-Page Architecture

The web portal is organized into three separate pages for better organization and user experience:

| Page | URL | Description | Available In |
|------|-----|-------------|--------------|
| **Home** | `/` or `/home.html` | Additional/custom settings and welcome message | Full Mode only |
| **Network** | `/network.html` | WiFi, device, and network configuration | Both modes |
| **Firmware** | `/firmware.html` | Online update, manual upload, and factory reset | Full Mode only |

**Navigation:**
- Tabbed navigation at top of page
- Active page highlighted in white
- In AP mode (Core Mode), only Network tab is visible

**Responsive Design:**
- Mobile (<768px): All sections stack vertically
- Desktop (≥768px): Related sections displayed side-by-side in 2-column grid
  - Home page: Hello World + Sample Settings
  - Network page: WiFi Settings + Device Settings (side-by-side), Network Config (full-width)
- Container max-width: 900px

### Header Badges

The portal displays 7 real-time device capability and status badges with optimized loading states:

| Badge | Color | Placeholder | Example | Description |
|-------|-------|-------------|---------|-------------|
| Firmware | Purple | `Firmware v-.-.-` | `Firmware v0.0.1` | Firmware version |
| Chip | Orange | `--- rev -` | `ESP32-C3 rev 4` | Chip model and revision |
| Cores | Green | `- Core` | `1 Core` / `2 Cores` | Number of CPU cores |
| Frequency | Yellow | `--- MHz` | `160 MHz` | CPU frequency |
| Flash | Cyan | `-- MB Flash` | `4 MB Flash` | Flash memory size |
| PSRAM | Teal | `No PSRAM` | `No PSRAM` / `2 MB PSRAM` | PSRAM status |
| Health | Orange (distinct) | `● CPU --` | `● CPU 45% ⋮` | Real-time CPU usage (click to expand) |

**Loading Optimization:**
- Badges show format placeholders on initial load (e.g., `--- MHz` instead of `Loading...`)
- Fixed widths prevent layout shift when data loads
- Minimal visual changes when actual data arrives

**Health Badge Features:**
- Green breathing dot (pulses on updates every 10 seconds)
- Current CPU usage percentage
- Click badge or `⋮` icon to expand full health overlay
- Orange background for visibility against blue header

### Health Monitoring

Real-time device health monitoring integrated as a header badge with expandable overlay:

**Header Badge (Always Visible):**
- Green breathing dot (pulses on updates)
- Current CPU usage percentage
- Orange background for visibility
- Click to expand full health overlay
- Poll interval is configurable by firmware (see `GET /api/info`)

**Expanded Overlay:**
- Appears top-right when badge clicked
- **Uptime**: Device runtime
- **Reset Reason**: Why device last restarted
- **CPU Usage**: Percentage based on IDLE task (nullable when runtime stats unavailable)
- **Core Temp**: Internal temperature sensor (ESP32-C3/S2/S3/C2/C6/H2)
- **Internal Heap**: Free/min/largest block + fragmentation
- **PSRAM**: Free/min/largest block + fragmentation (when present)
- **Flash Usage**: Used firmware space
- **Filesystem**: FFat presence/mounted/usage (nullable when no partition present)
- **MQTT**: Enabled/connected/publish age
- **Display**: FPS + timing (when display present)
- **RSSI / IP Address**: Network signal and IP (when connected)
- Click `✕` to close
- Same polling cadence as configured by firmware

**Sparklines (Optional):**
- If the firmware exposes device-side history, the portal fetches it from `GET /api/health/history` (only while the overlay is expanded).
- If device-side history is unavailable, the overlay shows point-in-time metrics only (no sparklines).

### Primary Category (Board Variants)

Board-specific firmware variants can promote a custom nav category to first position in the sidebar. This gives feature-specific content (e.g., a darkroom timer or coffee scale UI) top-level prominence instead of burying it inside a generic category like Actions or Sensors.

**How it works:**

1. Define four `PORTAL_PRIMARY_*` flags in the board's `board_overrides.h`:

   ```c
   #define PORTAL_PRIMARY_FRAGMENT "darkroom"       // Startup fragment ID
   #define PORTAL_PRIMARY_CATEGORY "darkroom"       // Custom category ID
   #define PORTAL_PRIMARY_LABEL    "Darkroom"       // Sidebar display name
   #define PORTAL_PRIMARY_ICON     "\xf0\x9f\x94\xb4"  // UTF-8 emoji (🔴)
   ```

2. Set each component's `.category` to match `PORTAL_PRIMARY_CATEGORY` (e.g., `"darkroom"`).

**Behavior when configured:**

- `GET /api/portal/nav` includes a `primary` object and injects the custom category as the first entry in `categories[]`.
- The SPA navigates to `#<primary_fragment>` on startup instead of `#welcome`.
- The welcome page renders a hero card linking to the primary fragment.
- The primary category section is expanded by default in the nav sidebar.

**Validation rules:**

- `PORTAL_PRIMARY_CATEGORY` must not collide with any hardcoded category ID (`device`, `display`, `pads`, `actions`, `connectivity`, `audio`, `sensors`, `firmware`). If it does, the primary configuration is ignored.
- `PORTAL_PRIMARY_FRAGMENT` must resolve to an item inside `PORTAL_PRIMARY_CATEGORY`. If it doesn't, the entire primary configuration is ignored.
- In AP mode, the primary category and `primary` object are suppressed entirely.

**When not configured (default):** all four flags default to `""` and the portal behaves exactly as before — no hero card, `#welcome` on startup, no extra nav category.

**Startup routing fallback chain:** URL hash (if present and item exists in nav) → primary fragment → `#welcome` → first visible item.

### Configuration Pages

#### Home Page (`/` or `/home.html`)

**Available In:** Full Mode only (redirects to Network page in AP mode)

**Sections:**
- **⚡ Operating Mode**: Mode selection, duty-cycle wake interval, Wi-Fi backoff cap, and the recovery-portal auto-sleep. MQTT publish interval and payload scope live on the Network page in the MQTT card.
- **BLE Advertising**: Burst timing controls (only shown when firmware enables BLE)
- **Sensor & Display settings**: Thresholds, brightness, on-demand screen preview, and screen saver configuration

**Layout:** Sections use 2-column grids on desktop (≥768px), stacked on mobile

**Purpose:** Device-level settings (operating mode, sensors, display)

#### Pads Page (`/pads.html`)

**Available In:** Full Mode only (redirects to Network page in AP mode)

**Sections:**
- **🎛️ Pad Editor** (only shown when firmware has display): Visual grid editor for pad pages
  - **Pad selection & naming**: Dropdown for Pad 1–16 with optional custom names (max 31 chars)
  - **Grid preview**: Click any cell to open the button editor dialog
  - **Button editor dialog**: Reorganized into collapsible card-like groups (Layout, Labels, Bar Chart, Gauge, Sparkline, Table, Actions, Icon, Image / Camera Feed, Appearance, State)
  - **Button action confirmation**: Optional per-button modal protects both normal tap and long-press action lists, supports custom prompt text, and auto-cancels after 10 seconds
  - **Table bindings**: Table widget data binding supports structured payloads from exact single-token bindings such as `[health:table]` and `[health:extended_table]`
  - **Button Defaults**: Collapsible section at the bottom of the Pads page for device-wide default appearance (colors, border, radius, content padding, label styles). Buttons on all pads inherit defaults unless overridden; reset-to-default ↩ links appear on overridden fields. Stored as a separate JSON file on LittleFS (`/config/button_defaults.json`) with a dedicated REST API (`GET/POST /api/button-defaults`)
  - **Template Pad**: Dropdown to inherit buttons from another pad into empty grid positions. Template buttons appear as ghost overlays in the editor. Merge includes bindings (target wins on conflict, no chaining)
  - **Building Blocks**: Pre-configured button groups available in the More ▾ menu under "━━ Blocks ━━". Select a block to enter placement mode — green/red ghost overlay shows valid/invalid positions. Blocks check grid dimensions, free cells, and 64-button limit. Uses extensible registration API (`pad_block_register()`) so feature branches add blocks independently. Catalog served by `GET /api/pad/blocks`
  - **Button copy/paste**: Copy button settings from one cell and paste into another; position-independent
  - **Pad actions via "More ▾" menu**: Fill Pad (fill all cells with copied button), Copy/Paste Pad (entire page), Export/Import Pad (JSON file), Export/Import Device Config (NVS + all 16 pad configs), Clear Pad
  - **Device config export/import**: Exports NVS settings (excluding network) plus all 16 pad pages to a single JSON file; import overwrites settings and reboots
- **Unsaved-changes protection**: Confirm dialog on page/pad switch and `beforeunload` event when edits are pending
- **Custom floating footer**: Save Pad, Show on Device, and More menu (not the shared `{{FOOTER}}` template)

**Layout:** Full-width pad grid with responsive button editor dialog

#### Network Page (`/network.html`)

**Available In:** Both Core Mode and Full Mode

**Sections:**
- **📶 WiFi Settings**: SSID, password
  - SSID required (max 31 characters)
  - Password optional (max 63 characters, leave empty for open networks)
- **🔧 Device Settings**: Device name, mDNS hostname
  - Device name required (max 31 characters, can include spaces)
  - mDNS name auto-generated and displayed (sanitized, lowercase, hyphens)
- **🌐 Network Configuration (Optional)**: Static IP settings
  - Fixed IP Address (optional, leave empty for DHCP)
  - Subnet Mask (required if fixed IP set)
  - Gateway (required if fixed IP set)
  - DNS Server 1 (optional, defaults to gateway)
  - DNS Server 2 (optional)
- **🔒 Security (Optional)**: HTTP Basic Authentication
  - Configure in Full Mode only (hidden/locked in Core Mode)
  - Enable, set username, and set/update password
- **📡 MQTT Settings (Optional)**: MQTT broker settings
  - Only shown when MQTT support is enabled in firmware (`HAS_MQTT`)
  - Host, port, username/password
  - Publish interval and payload scope
  - Periodic publish cadence follows `mqtt_publish_interval_seconds` (Always-On mode only; in Duty-Cycle, one publish per wake)

**Layout:** 
- WiFi + Device Settings side-by-side on desktop
- Network Configuration full-width on all screens

#### Firmware Page (`/firmware.html`)

**Available In:** Full Mode only (redirects to Network page in AP mode)

**Sections:**
- **🌐 Online Update (GitHub Pages)**: Opens the GitHub Pages updater in a new tab and passes the device URL via querystring.
  - The Pages site reads `device` and calls the device API to start the update.
  - Repo owner/name comes from build-time git remote detection (auto-generated into `src/app/repo_slug_config.h`).
- **📦 Manual Update (Upload)**: Upload .bin firmware file
  - Select .bin file from build directory
  - Upload progress bar
  - Automatic reboot and reconnection
- **🔄 Factory Reset**: Full wipe of NVS partition + filesystem (pads, icons, sounds, indexed stores, BLE pairings)
  - Confirmation required
  - Device reboots in AP mode after reset

### Configuration Form Behavior

**Partial Updates:**
The portal implements intelligent partial configuration updates:
- Each page only sends fields present on that page
- Backend only updates fields included in the request
- Prevents accidental clearing of settings from other pages
- Example: Saving on Home page doesn't affect Network page settings

**Validation:**
- Required fields checked before submission
- Fixed IP validation (subnet/gateway required if IP set)
- Visual feedback for errors

**Floating Action Footer:**
All pages include a fixed bottom footer with action buttons:
- **Save and Reboot**: Saves configuration and reboots device (applies WiFi changes)
- **Save**: Saves configuration without rebooting (settings applied on next reboot)
- **Reboot**: Reboots device without saving changes
- Footer stays attached to bottom, spans full page width (max 900px)
- Always visible while scrolling

**Header Reboot Button:**
The portal header also carries a 🔄 reboot button next to the dark/light theme toggle, available on every page (including pages without the Save/Reboot footer). Clicking it prompts for confirmation, calls `POST /api/reboot`, and shows the standard reboot dialog. It does not save first.

## Automatic Reconnection After Reboot

When the device reboots (after saving settings, firmware update, or manual reboot), the portal automatically attempts to reconnect and redirect you to the device.

### Reconnection Behavior

**Unified Dialog:**
All reboot scenarios (save config, OTA update, manual reboot) use a single dialog that:
- Shows current operation status
- Displays best-effort automatic reconnection messages
- Provides manual fallback address immediately
- Updates with real-time connection attempts

**Polling Strategy:**
- **Initial delay:** 2 seconds (device starts rebooting)
- **Polling interval:** 3 seconds between connection checks
- **Total timeout:** 122 seconds (2s initial + 40 attempts × 3s)
- **Endpoint used:** `/api/info` (lightweight health check)

**Progress Display:**
```
Checking connection (attempt 5/40, 17s elapsed)...
```

### Scenarios

#### Config Save / Manual Reboot
1. Dialog shows: *"Configuration saved. Device is rebooting..."*
2. Displays: *"Attempting automatic reconnection..."*
3. Shows manual fallback: *"If this fails, manually navigate to: http://device-name.local"*
4. Polls both new hostname (if device name changed) and current location
5. On success: Redirects automatically
6. On timeout: Provides clickable manual link with troubleshooting hints

#### OTA Firmware Update
1. Progress bar shows upload (0-100%)
2. At 95%+: *"Installing firmware & rebooting..."*
3. Transitions to reconnection polling after 2s delay
4. Same polling behavior as config save
5. Redirects to same location (firmware update doesn't change hostname)

#### Factory Reset
1. Dialog shows: *"Configuration reset. Device restarting in AP mode..."*
2. Message: *"You must manually reconnect to the WiFi access point"*
3. **No automatic polling** (user must manually reconnect to AP)
4. Dialog remains visible with instructions

### Timeout Handling

If reconnection fails after 122 seconds:

```
Automatic reconnection failed

Could not reconnect after 122 seconds.

Please manually navigate to:
http://device-name.local

Possible issues: WiFi connection failed, incorrect credentials, 
or device taking longer to boot.
```

### Best Practices

**For Users:**
- Keep the browser tab open during reboot (don't close immediately)
- If automatic reconnection fails, use the provided manual link
- For device name changes, bookmark the new address
- On AP mode reset, reconnect to the WiFi access point before accessing portal

**For Developers:**
- Automatic reconnection is best-effort (network conditions vary)
- Always provide manual fallback addresses
- DNS propagation for hostname changes may take additional time
- Some networks/browsers block cross-origin polling

## REST API Reference

All endpoints return JSON responses with proper HTTP status codes.

**Authentication (Optional):**
- If HTTP Basic Auth is enabled (Full Mode only), requests must include an `Authorization: Basic ...` header.
- Example: `curl -u username:password http://<device-ip>/api/info`
- In Core Mode (AP + captive portal), endpoints are intentionally unauthenticated to allow initial setup.

### Device Information

#### `GET /api/info`

Returns comprehensive device information.

**Response:**
```json
{
  "version": "0.0.1",
  "ap_active": false,
  "build_date": "Nov 25 2025",
  "build_time": "14:30:00",
  "board_name": "esp32-nodisplay",
  "chip_model": "ESP32-C3",
  "chip_revision": 4,
  "chip_cores": 1,
  "cpu_freq": 160,
  "flash_chip_size": 4194304,
  "psram_size": 0,
  "health_poll_interval_ms": 5000,
  "health_history_seconds": 300,
  "health_history_available": true,
  "health_history_period_ms": 5000,
  "health_history_samples": 60,
  "display_coord_width": 320,
  "display_coord_height": 240,
  "free_heap": 250000,
  "sketch_size": 1048576,
  "free_sketch_space": 2097152,
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "wifi_hostname": "esp32-1234",
  "mdns_name": "esp32-1234.local",
  "hostname": "esp32-1234",
  "has_display": true,
  "has_audio": true,
  "has_sound_player": true,
  "max_pads": 16,
  "max_grid_cols": 8,
  "max_grid_rows": 8,
  "display_coord_width": 480,
  "display_coord_height": 480,
  "available_screens": [
    {"id": "info", "name": "Info Screen"},
    {"id": "pad_0", "name": "Pad 1: Living Room"},
    {"id": "pad_1", "name": "Pad 2"}
  ],
  "current_screen": "pad_0"
}
```

**Discovery Fields:**
- `mac_address`: Device MAC address
- `wifi_hostname`: WiFi/DHCP hostname
- `mdns_name`: Full mDNS name (hostname + `.local`)
- `hostname`: Short hostname

**Portal Mode Field:**
- `ap_active`: `true` when the device is running in AP / captive-portal mode, `false` in full STA mode. Portal JS derives `portalMode` (`"core"` vs `"full"`) from this flag. (Replaces the removed `GET /api/mode` endpoint.)

**Display Fields** (only when `has_display` is `true`):
- `display_coord_width` / `display_coord_height`: Display resolution
- `available_screens`: Array of `{id, name}` objects; pad screens include custom names from config
- `current_screen`: ID of the currently displayed screen

**Health Widget Fields:**
- `health_poll_interval_ms`: Poll interval used by the portal health overlay
- `health_history_seconds`: History window length used by the portal sparklines

**Health History Capability Fields:**
- `health_history_available`: `true` when the device exposes `GET /api/health/history`
- `health_history_period_ms`: Sampling cadence for device-side history
- `health_history_samples`: Configured sample capacity for device-side history

**Display Fields (when `HAS_DISPLAY` enabled):**
- `display_coord_width`, `display_coord_height`: Display driver coordinate space dimensions

### Health Monitoring

#### `GET /api/health`

Returns real-time device health statistics.

**Response:**
```json
{
  "uptime_seconds": 3600,
  "reset_reason": "Power On",
  "cpu_usage": 15,
  "cpu_freq": 160,
  "cpu_temperature": 42,
  "heap_free": 250000,
  "heap_min": 240000,
  "heap_largest": 120000,
  "heap_internal_free": 200000,
  "heap_internal_min": 190000,
  "heap_internal_largest": 110000,
  "heap_dma_internal_free": 48000,
  "heap_dma_internal_min": 32000,
  "heap_dma_internal_largest": 24000,
  "heap_fragmentation": 5,
  "psram_free": 8388608,
  "psram_min": 8350000,
  "psram_largest": 8200000,
  "psram_fragmentation": 2,
  "flash_used": 1048576,
  "flash_total": 3145728,
  "fs_mounted": true,
  "fs_used_bytes": 123456,
  "fs_total_bytes": 987654,
  "mqtt_enabled": true,
  "mqtt_publish_enabled": true,
  "mqtt_connected": true,
  "mqtt_last_health_publish_ms": 1234567,
  "mqtt_health_publish_age_ms": 4000,
  "display_fps": 30,
  "display_lv_timer_us": 250,
  "display_present_us": 1200,

  "sensors": {
    "temperature": 21.7,
    "humidity": 39.6,
    "pressure": 995.4
  },

  "heap_internal_free_min_window": 195000,
  "heap_internal_free_max_window": 205000,
  "heap_internal_largest_min_window": 100000,
  "heap_fragmentation_max_window": 8,
  "psram_free_min_window": 8300000,
  "psram_free_max_window": 8388608,
  "psram_largest_min_window": 8100000,
  "psram_fragmentation_max_window": 4,

  "wifi_rssi": -45,
  "wifi_channel": 6,
  "ip_address": "192.168.1.100",
  "hostname": "esp32-1234"
}
```

**Notes:**
- `ble_status`: compact user-facing BLE status with values `disabled`, `ready`, `pairing`, `connected`, or `error`
- `ble_state`: detailed BLE status with values `disabled`, `idle`, `advertising`, `pairing`, `connecting`, `claimed`, `secured`, or `error`
- `ble_name`: current BLE keyboard name (same as the configured device name)
- `cpu_usage`: `null` when FreeRTOS runtime stats are unavailable/disabled
- `cpu_temperature`: `null` on chips without an internal temperature sensor
- `fs_mounted`: `null` when no filesystem partition is present; `false` when present but not mounted
- `wifi_rssi`, `wifi_channel`, `ip_address`: `null` when not connected
- `*_min_window` / `*_max_window`: sampled continuously by firmware and returned as a multi-client-safe snapshot (captures short-lived dips/spikes)
- `sensors`: object containing optional sensor values (empty object when no sensors are available)

#### `GET /api/health/history`

Returns device-side health history arrays for the portal sparklines.

**Notes:**
- Arrays are ordered oldest → newest.
- `uptime_ms` values are monotonic `millis()` at sample time (wraps after ~49.7 days).
- `cpu_usage` entries may be `null` when unavailable.

**Response (example):**
```json
{
  "available": true,
  "period_ms": 5000,
  "seconds": 300,
  "samples": 60,
  "count": 60,
  "capacity": 60,

  "uptime_ms": [120000, 125000, 130000],
  "cpu_usage": [12, 14, 18],
  "heap_internal_free": [190000, 189500, 189000],
  "heap_internal_free_min_window": [188000, 188000, 188500],
  "heap_internal_free_max_window": [195000, 194500, 194000]
}
```

### Configuration Management

#### `GET /api/config`

Returns current device configuration (passwords excluded).

**Response:**
```json
{
  "wifi_ssid": "MyNetwork",
  "wifi_password": "",
  "device_name": "esp32-device",
  "device_name_sanitized": "esp32-device",
  "fixed_ip": "",
  "subnet_mask": "",
  "gateway": "",
  "dns1": "",
  "dns2": "",

  "power_mode": "always_on",
  "duty_cycle_wake_seconds": 120,
  "mqtt_publish_interval_seconds": 120,
  "portal_idle_timeout_seconds": 120,
  "wifi_backoff_max_seconds": 900,
  "mqtt_publish_scope": "sensors_only",

  "basic_auth_enabled": false,
  "basic_auth_username": "",
  "basic_auth_password_set": false,

  "mcp_enabled": false,
  "mcp_control_enabled": false,
  "mcp_token_set": false,

  "backlight_brightness": 100,

  "screen_saver_enabled": false,
  "screen_saver_timeout_seconds": 300,
  "screen_saver_fade_out_ms": 800,
  "screen_saver_fade_in_ms": 400,
  "screen_saver_wake_on_touch": true,
  "screen_saver_wake_binding": "",

  "audio_volume": 50,
  "tap_beep": "",
  "lp_beep": "",

  "ha_url": "",
  "ha_token": ""
}
```

**Notes:**
- Some fields are build-time gated.
  - Display-related fields (backlight + screen saver) are present when `HAS_DISPLAY` is enabled.
  - Audio-related fields (`audio_volume`, `tap_beep`, `lp_beep`) are present when `HAS_AUDIO` is enabled.
  - Other feature-specific fields may be present depending on firmware configuration.
- `ha_url` is the Home Assistant base URL used by the **Home Assistant Service** button action. `ha_token` (the long-lived access token) is never returned by `GET /api/config` — it is always reported as an empty string.
- MCP fields (`mcp_enabled`, `mcp_control_enabled`, `mcp_token_set`) are present when `HAS_MCP` is enabled. The MCP bearer token itself is never returned — only `mcp_token_set` (boolean) indicates whether one has been generated. A `caps.mcp` flag in the capability map reflects the build flag so the portal can hide the MCP card when compiled out.

#### `POST /api/config`

Save new configuration. Device reboots after successful save.

**Request Body:**
```json
{
  "wifi_ssid": "NewNetwork",
  "wifi_password": "password123",
  "device_name": "my-esp32",
  "fixed_ip": "192.168.1.100",
  "subnet_mask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns1": "8.8.8.8",
  "dns2": "8.8.4.4",

  "power_mode": "duty_cycle",
  "duty_cycle_wake_seconds": 120,
  "mqtt_publish_interval_seconds": 120,
  "portal_idle_timeout_seconds": 120,
  "wifi_backoff_max_seconds": 900,
  "mqtt_publish_scope": "sensors_only",

  "basic_auth_enabled": true,
  "basic_auth_username": "admin",
  "basic_auth_password": "change-me",

  "mcp_enabled": true,
  "mcp_control_enabled": false,
  "mcp_generate_token": true,

  "backlight_brightness": 70,

  "screen_saver_enabled": true,
  "screen_saver_timeout_seconds": 300,
  "screen_saver_fade_out_ms": 800,
  "screen_saver_fade_in_ms": 400,
  "screen_saver_wake_on_touch": true,
  "screen_saver_wake_binding": "[mqtt:devices/node/presence/state]",

  "audio_volume": 50,
  "tap_beep": "800:80",
  "lp_beep": "600:40 40 600:40",

  "ha_url": "http://192.168.1.50:8123",
  "ha_token": "eyJhbGciOi..."
}
```

**Response:**
```json
{
  "success": true,
  "message": "Configuration saved"
}
```

**Notes:**
- Only fields present in request are updated
- Password field: empty string = no change, non-empty = update
- `ha_token` follows the same rule: empty string = keep current, non-empty = update. `ha_url` is always updated when present.
- Basic Auth password is never returned by `GET /api/config`.
- `mcp_enabled` / `mcp_control_enabled` are applied live (no reboot needed). Sending `mcp_generate_token: true` mints a new bearer token server-side (hardware RNG); the plaintext token is returned **once** in this POST response as `mcp_token` and never again. Post with `?no_reboot=1` (the portal does) so toggling MCP does not reboot the device.
- In Core Mode (AP mode), Basic Auth settings cannot be changed via `POST /api/config`.
- Device automatically reboots after successful save
- Web portal automatically polls for reconnection (see [Automatic Reconnection](#automatic-reconnection-after-reboot))

#### `DELETE /api/config`

Reset configuration to factory defaults. Device reboots after reset.

**Response:**
```json
{
  "success": true,
  "message": "Configuration reset"
}
```

**Notes:**
- Device reboots into AP mode after reset
- **No automatic reconnection** - user must manually reconnect to WiFi access point

### Portal Navigation

#### `GET /api/portal/nav`

Returns the navigation tree used by the SPA sidebar. Categories contain sorted component items.

**Response:**
```json
{
  "categories": [
    {
      "id": "device",
      "display_name": "Device",
      "icon": "⚙️",
      "items": [
        { "id": "wifi", "display_name": "WiFi" },
        { "id": "device-name", "display_name": "Device Name" }
      ]
    }
  ]
}
```

When a board defines `PORTAL_PRIMARY_CATEGORY` (non-empty), the response includes an additional `primary` object and the custom category appears first in `categories[]`:

```json
{
  "primary": {
    "fragment": "darkroom",
    "category": "darkroom",
    "label": "Darkroom",
    "icon": "🔴"
  },
  "categories": [ ... ]
}
```

**Notes:**
- In AP mode, only the `device` category is returned; `primary` is omitted.
- Empty categories are skipped.
- Items within each category are sorted by `nav_order`.

### Portal Mode

Portal mode is exposed via the `ap_active` field on [`GET /api/info`](#get-apiinfo). The standalone `GET /api/mode` endpoint was removed to reduce HTTP request count during portal boot (see [Concurrent Request Throttling](#concurrent-request-throttling)).

### System Control
- `width`/`height` must match the device's display coordinate-space resolution (see `GET /api/info` fields `display_coord_width`/`display_coord_height`)

Reboot the device without saving configuration changes.

**Response:**
```json
{
  "success": true,
  "message": "Rebooting device..."
}
```

**Notes:**
- Web portal automatically polls for reconnection (see [Automatic Reconnection](#automatic-reconnection-after-reboot))

### OTA Firmware Update

#### `POST /api/update`

Upload new firmware binary for over-the-air update.

**Request:**
- Content-Type: `multipart/form-data`
- File field: firmware `.bin` file

**Response (Success):**
```json
{
  "success": true,
  "message": "Update successful! Rebooting..."
}
```

**Response (Error):**
```json
{
  "success": false,
  "message": "Error description"
}
```

**Notes:**
- Only `.bin` files accepted
- File size must fit in OTA partition
- Device automatically reboots after successful update
- Progress logged to serial monitor
- Web portal shows upload progress bar, then automatically polls for reconnection (see [Automatic Reconnection](#automatic-reconnection-after-reboot))

### GitHub Pages Online Update

The GitHub Pages updater initiates OTA by POSTing a firmware URL to the device. The device downloads the firmware directly (app-only `.bin`) and flashes it.

#### `POST /api/firmware/update`

Start a background download+flash task using a provided URL.

**Request Body:**
```json
{
  "url": "https://<owner>.github.io/<repo>/firmware/<board>/app.bin",
  "version": "0.0.2",
  "sha256": "<optional>",
  "size": 1215439
}
```

**Response (Success):**
```json
{
  "success": true,
  "update_started": true,
  "version": "0.0.2"
}
```

#### `GET /api/firmware/update/status`

Get current progress/state of the online update task.

**Response (Example):**
```json
{
  "in_progress": true,
  "state": "writing",
  "progress": 262144,
  "total": 1215439,
  "version": "0.0.2",
  "error": ""
}
```

**CORS:**
- The device responds with `Access-Control-Allow-Origin: https://<owner>.github.io`.
- Allowed headers: `Authorization`, `Content-Type`.

### Display Control (HAS_DISPLAY enabled)

These endpoints are only available when the firmware is compiled with `HAS_DISPLAY`.

#### `PUT /api/display/brightness`

Set backlight brightness immediately (does not persist to NVS). Accepts 0–100. Brightness 0 is allowed for programmatic blanking (e.g., the pad editor's blank-on-save sequence on MIPI-DSI boards); the `MIN_USER_BRIGHTNESS` floor is not enforced on this transient endpoint. Routes through the screen saver manager so the screensaver's internal brightness tracking stays consistent — the device wakes to this brightness after a sleep cycle. If the screensaver is active, this triggers a wake to the new level.

**Request Body:**
```json
{ "brightness": 80 }
```

#### `GET /api/display/sleep`

Get screen saver status.

**Response:**
```json
{
  "enabled": true,
  "state": 0,
  "current_brightness": 100,
  "target_brightness": 100,
  "seconds_until_sleep": 42
}
```

`state` values:
- `0` = Awake
- `1` = FadingOut
- `2` = Asleep
- `3` = FadingIn

#### `POST /api/display/sleep`

Force the screen saver to sleep now (fade backlight to 0).

#### `POST /api/display/wake`

Force wake now (fade backlight back to configured brightness).

#### `POST /api/display/activity`

Reset the idle timer; optionally request wake.

- `POST /api/display/activity` (just resets timer)
- `POST /api/display/activity?wake=1` (resets timer + wake)

#### `PUT /api/display/screen`

Switch the active runtime screen (no persistence).

**Request Body:**
```json
{ "screen": "info" }
```

**Notes:**
- Screen-affecting actions count as user activity and will reset the screen saver timer.
- When the screen saver is dimming/asleep/fading in, touch input is intentionally suppressed to avoid “wake gestures” clicking through into the UI. A second tap may be required after wake.



---

### MCP Server API

Requires `HAS_MCP` (default on). Off by default; enabled and tokened from the portal's **MCP** card. STA-mode only. See the user-facing [MCP Server Guide](../mcp-guide.md) for client setup.

#### `POST /mcp`

Single Model Context Protocol endpoint — JSON-RPC 2.0 over the MCP **Streamable HTTP** transport (protocol `2025-06-18`, JSON-only responses, stateless). Methods: `initialize`, `tools/list`, `tools/call`, `ping`; JSON-RPC notifications return `202`.

- **Auth:** every request must carry `Authorization: Bearer <token>`. Fails closed: when `mcp_enabled` is true but no token has been generated, all requests are refused (`401`). Independent of portal Basic Auth.
- **Availability:** returns `404` when `mcp_enabled` is false or the device is in AP/setup mode. Performs Origin validation (any browser `Origin` → `403`; native clients send none) and does not alter the global CORS allow-list.
- **Tools:** read tools are always available once enabled; control tools (`press_button`, `set_screen`, `set_backlight`, `wake`, `system_command`) are hidden from `tools/list` and refused by `tools/call` unless `mcp_control_enabled` is true. Display tools are absent on `!HAS_DISPLAY` boards. Control tools run on the main loop, never on the async web task.
- **Body cap:** request bodies above 8 KB are rejected with JSON-RPC `-32600`.
- **Implementation note:** served by a custom `AsyncWebHandler` (not `server->on`) because MCP clients send `Accept: application/json, text/event-stream`, which the stock callback handler rejects via `isHTTP()`. `GET`/`DELETE /mcp` return `405` (no SSE stream offered) so client transport negotiation succeeds.

**Example (`initialize`):**
```bash
curl -X POST http://<device-ip>/mcp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <token>" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
```



---

### Screenshot API

Requires `HAS_DISPLAY`. Gated by Basic Auth when enabled.

#### `GET /api/screenshot`

Capture the current display contents as an image. ESP32-P4 boards default to a
hardware-encoded JPEG; other boards retain the 24-bit BMP default.

- **Query parameters:** `format=bmp|jpg` overrides the format. `quality=1..100` controls JPEG quality and defaults to `85`.
- **Response:** `image/jpeg` for JPEG or `image/bmp` for a 24-bit uncompressed BMP (RGB888, bottom-up row order).
- **Mechanism:** Uses LVGL `lv_snapshot_take()` to render the active screen to a temporary RGB565 buffer. On ESP32-P4, the hardware JPEG encoder consumes RGB565 directly with YUV420 subsampling. The BMP path converts RGB565 to BGR888 and streams the result as a chunked HTTP response.
- **Memory:** Per-request snapshot, conversion, and JPEG buffers are released as soon as the final response chunk is produced. Interrupted responses release any remaining buffers when their response context is destroyed. On ESP32-P4, the lazily initialized JPEG encoder and its synchronization primitive remain allocated after first use.
- **Thread safety:** Acquires the LVGL mutex with a 1-second timeout and serializes access to the hardware JPEG encoder. Returns `503 Display busy` if the LVGL mutex cannot be acquired.
- **Fallback:** If hardware JPEG encoding fails, the endpoint transparently returns BMP with `Content-Type: image/bmp`. Requesting `format=jpg` on a non-P4 board returns `400`.

**Example:**

```bash
# Default on ESP32-P4
curl -u user:pass http://<device-ip>/api/screenshot -o screenshot.jpg

# Default on non-P4 display boards
curl -u user:pass http://<device-ip>/api/screenshot -o screenshot.bmp

# Explicit format and JPEG quality
curl -u user:pass 'http://<device-ip>/api/screenshot?format=bmp' -o screenshot.bmp
curl -u user:pass 'http://<device-ip>/api/screenshot?format=jpg&quality=70' -o screenshot.jpg
```

**Notes:**

- Image dimensions match the device's display resolution.
- BMP is uncompressed, so BMP files range from ~253 KB (360×360) to ~1.2 MB (1024×600) depending on the board.
- The portal exposes this endpoint under **Display > Screen Preview**. Opening the fragment does not capture an image; **Capture Preview** and **Refresh Preview** request a fresh framebuffer.

---

### Swipe Actions API

All swipe endpoints require `HAS_DISPLAY` and are gated by Basic Auth when enabled.

#### `GET /api/swipe-actions`

Returns the current swipe action configuration.

- **Response:** JSON object with `swipe_left`, `swipe_right`, `swipe_up`, `swipe_down` keys, each containing a `ButtonAction` object (`type`, `screen_id`, `mqtt_topic`, `mqtt_payload`, `key_sequence`).
- Default: `swipe_right` has `type: "back"`, all others are empty.

#### `POST /api/swipe-actions`

Save swipe action configuration to LittleFS.

- **Body:** JSON object with the same structure as the GET response.
- **Response:** `{"success": true}` on success; JSON error on failure.
- Actions are applied immediately without reboot.

---

### Boot Actions API

All boot-actions endpoints require `HAS_DISPLAY` and are gated by Basic Auth when enabled. Boot actions are stored on LittleFS at `/config/boot_actions.json` and dispatched once after the first screen is shown during boot.

#### `GET /api/boot-actions`

Returns the current boot action configuration.

- **Response:** JSON object with an `actions` array containing up to 3 `ButtonAction` objects (same schema as button/swipe actions: `type`, `target`, `topic`, `payload`, `sequence`, `beep_pattern`, `beep_volume`, `sound_file`, `sound_volume`, `notify_text`, `notify_duration_ms`, `notify_text_color`, `notify_bg_color`, `notify_border_color`, `notify_opacity`, `notify_font_size`, `notify_location`, `system_command`, `volume_mode`, `volume_value`, `brightness_mode`, `brightness_value`, `timer_id`, `timer_command`, `timer_value`, etc.).
- Default (no file saved): `{"actions": []}`.

#### `POST /api/boot-actions`

Save boot action configuration to LittleFS.

- **Body:** JSON object with an `actions` array of `ButtonAction` objects. Trailing empty actions are trimmed by the web UI.
- **Response:** `{"ok": true}` on success; JSON error on failure.
- Changes take effect on next boot.

---

### Hardware Buttons API

Available when the board declares physical buttons. The board's `board_overrides.h` must set `HAS_BUTTON true` explicitly (it defaults to `false` and is not auto-derived) and define `NUM_HW_BUTTONS` plus the `HW_BUTTON_DEFS[]` table. Independent of `HAS_DISPLAY` — works on headless boards. Gated by Basic Auth when enabled. Per-button tap/hold action lists are stored on the `Storage` facade at `/config/hw_buttons.json` and applied immediately without reboot.

#### `GET /api/component/hw-buttons/config`

Returns the declared buttons and their configured action lists.

- **Response:** JSON object with a `buttons` array, one entry per declared button (in `HW_BUTTON_DEFS[]` order), each containing `label` (string), `pin` (GPIO number), `tap_actions` (array of up to 3 `ButtonAction` objects), and `hold_actions` (array of up to 3 `ButtonAction` objects). `ButtonAction` uses the same schema as button/swipe/boot actions.
- Default (no file saved): each button reports empty `tap_actions` and `hold_actions`.

#### `POST /api/component/hw-buttons/config`

Save hardware button action configuration.

- **Body:** JSON object with a `buttons` array of `{ tap_actions, hold_actions }` objects, positional by button index. Entries beyond `NUM_HW_BUTTONS` are ignored.
- **Response:** standard component save response on success; JSON error on failure.
- On screenless boards, display-only action types (screen, brightness, notify, visual_alert, timer) parse and store normally but log a no-op when dispatched.

---

### MQTT Triggers API

Available when `HAS_MQTT && (HAS_DISPLAY || HAS_BUTTON)` (so headless boards with a physical button are included, while display-less and button-less boards are excluded). Gated by Basic Auth when enabled. Triggers are stored on the `Storage` facade at `/config/mqtt_triggers.json` and applied immediately without reboot; subscriptions are (re)established on every MQTT (re)connect.

A trigger pairs a topic with an optional exact-value filter and up to 3 sequential `ButtonAction` objects. When a subscribed topic receives a message whose payload equals the filter (or the filter is empty, matching any payload), the trigger's actions are dispatched. Capacity is `MAX_MQTT_TRIGGERS` (default 8, lowered to 3 on non-PSRAM boards).

#### `GET /api/component/mqtt-triggers/config`

Returns the configured triggers and the device's capacity.

- **Response:** JSON object with `max` (number, `MAX_MQTT_TRIGGERS`) and a `triggers` array. Each entry contains `topic` (string), `value` (string, exact-match filter; empty = match any), and `actions` (array of up to 3 `ButtonAction` objects using the same schema as button/swipe/boot actions). Empty (unconfigured) slots are omitted.
- Default (no file saved): `max` with an empty `triggers` array.

#### `POST /api/component/mqtt-triggers/config`

Save the MQTT trigger configuration.

- **Body:** JSON object with a `triggers` array of `{ topic, value, actions }` objects. At most `MAX_MQTT_TRIGGERS` entries are accepted. Wildcard topics (containing `#` or `+`) are rejected with an error — use exact topic names.
- **Response:** standard component save response on success; JSON error on failure (e.g. wildcard topic, too many triggers, payload too large).
- On screenless boards (with a button), display-only action types parse and store normally but log a no-op when dispatched.

---

### Button Defaults API

All button-defaults endpoints require `HAS_DISPLAY` and are gated by Basic Auth when enabled. Button defaults are stored on LittleFS at `/config/button_defaults.json`.

#### `GET /api/button-defaults`

Returns the current device-level button defaults.

- **Response:** JSON object with only the fields that have been explicitly set. Possible fields: `bg_color`, `fg_color`, `border_color`, `border_width`, `corner_radius`, `content_pad`, `label_top_style`, `label_center_style`, `label_bottom_style`.
- Default (no file saved): empty JSON object `{}`.

#### `POST /api/button-defaults`

Save device-level button defaults to LittleFS.

- **Body:** JSON object with any subset of the fields listed above.
- **Response:** `{"ok": true}` on success; JSON error on failure.
- All pad caches are rebuilt immediately so changes take effect without reboot.

---

### Timer Config API

Device-level timer configuration. Compile-time gated by `HAS_DISPLAY`.

#### `GET /api/timers`

Returns the current timer configuration for all 3 timers.

- **Response:** JSON object with keys `"1"`, `"2"`, `"3"`. Each timer object contains:
  - `mode` — `"up"` or `"down"`
  - `countdown` — seconds (countdown mode only, omitted if 0)
  - `expire_actions` — array of ButtonAction objects (omitted if empty)

```json
{
  "1": { "mode": "down", "countdown": 300, "expire_actions": [{ "type": "sound", "sound_file": "alarm" }] },
  "2": { "mode": "up" },
  "3": { "mode": "down", "countdown": 60, "expire_actions": [{ "type": "beep", "beep_pattern": "1000:500" }] }
}
```

#### `POST /api/timers`

Save timer configuration to LittleFS and apply immediately.

- **Body:** Same JSON format as GET response.
- **Response:** `{"ok": true}` on success; JSON error on failure.
- Timer engine is updated immediately (mode, countdown preset, expire actions).

---

### Icon Store API

All icon endpoints require `HAS_DISPLAY` and are gated by Basic Auth when enabled.

#### `POST /api/icons/install?id=<id>&kind=<0|1>`

Upload a PNG icon and install it into the icon store.

- **Query Parameters:** `id` (required, `[a-z0-9_]`), `kind` (optional, `0`=color default, `1`=mono)
- **Body:** Raw PNG bytes (`Content-Type: application/octet-stream`)
- **Max size:** `ICON_MAX_PNG_SIZE` (128 KB)
- **Response:** `{"success": true}` on success; JSON error on failure
- The PNG is saved to LittleFS at `/icons/<id>.png` and decoded into a PSRAM-cached ARGB8888 draw buffer for immediate use.

#### `DELETE /api/icons/page?page=<N>`

Delete all icon files for a given pad page.

- **Query Parameters:** `page` (required, `0`–`7`)
- **Response:** `{"success": true}`

#### `GET /api/icons/installed`

List all installed icon IDs (from LittleFS `/icons/` directory).

**Response:**
```json
{ "icons": ["pad_0_0_0", "pad_0_1_2"], "cache_count": 2 }
```

#### `GET /api/icons/files`

List all files in `/icons/` with names and sizes (debug endpoint).

**Response:**
```json
{ "files": [{"name": "pad_0_0_0.png", "size": 4096}] }
```

#### `GET /api/icons/cache`

Dump in-memory cache entries with dimensions and data sizes (debug endpoint).

**Response:**
```json
{ "entries": [{"id": "pad_0_0_0", "kind": 0, "w": 128, "h": 128, "data_size": 65536}], "count": 1 }
```

#### `GET /api/icons/file?name=<filename>`

Download a raw icon file from `/icons/`. Returns `image/png` for `.png` files.

- **Query Parameters:** `name` (required, no path separators or `..`)

#### `DELETE /api/icons/file?name=<filename>`

Delete a specific icon file from `/icons/`.

- **Query Parameters:** `name` (required, no path separators or `..`)
- **Response:** `{"success": true}`

---

### Sound File API

All sound endpoints require `HAS_SOUND_PLAYER` (defaults to `HAS_AUDIO`) and are gated by Basic Auth when enabled. Sound files are stored on LittleFS at `/sounds/<name>.mp3`.

#### `POST /api/sounds/upload?name=<name>`

Upload an MP3 sound file.

- **Query Parameters:** `name` (required, `[a-zA-Z0-9_-]`, max 31 chars)
- **Body:** Raw MP3 bytes (`Content-Type: application/octet-stream`)
- **Max size:** 512 KB
- **Validation:** Rejects files that do not start with a valid MP3 frame sync word or ID3v2 tag header.
- **Response:** `{"status": "ok"}` on success; JSON error on failure

#### `GET /api/sounds/list`

List all uploaded sound file names (without `.mp3` extension).

- **Response:** JSON array of name strings, e.g. `["alert", "chime", "doorbell"]`

#### `DELETE /api/sounds?name=<name>`

Delete a sound file.

- **Query Parameters:** `name` (required)
- **Response:** `{"status": "ok"}` on success; `404` if not found

#### `POST /api/sounds/play?name=<name>`

Play a sound file immediately (for testing).

- **Query Parameters:** `name` (required)
- **Response:** `{"status": "ok"}` on success; `404` if not found

---

#### `GET /api/pad/tile_sizes?cols=<N>&rows=<N>`

Compute tile dimensions for a given grid layout. Used by the icon picker to render correctly sized icons.

- **Query Parameters:** `cols` (required, `1`–`8`), `rows` (required, `1`–`8`)
- **Response:**
```json
{
  "display_w": 720, "display_h": 720,
  "tile_w": 234, "tile_h": 234,
  "gap": 4, "padding": 4,
  "pixel_shift_margin": 4,
  "font_small_h": 16
}
```


#### `GET /api/pad/blocks`

Return the building block catalog — pre-configured button groups that can be inserted into a pad. Each block contains positional button definitions (relative offsets), minimum grid requirements, and optional pad-level bindings.

- **Response:**
```json
[
  {
    "id": "countdown_timer",
    "name": "Countdown Timer",
    "desc": "3 rockers (H/M/S) + timer display + start/pause/reset",
    "icon": "⏱️",
    "min_cols": 3,
    "min_rows": 2,
    "min_free": 6,
    "buttons": [
      {
        "col_offset": 0, "row_offset": 0, "col_span": 1, "row_span": 1,
        "label_center": "H",
        "widget_type": "rocker"
      }
    ],
    "bindings": {
      "timer1": "[timer:1]",
      "timer2": "[timer:2]",
      "timer3": "[timer:3]"
    }
  }
]
```

Each button's `col_offset` / `row_offset` is relative to the placement anchor cell. The editor adds the anchor position to compute absolute grid coordinates.


#### `POST /api/pad/resolve`

Resolve `[scheme:params]` binding tokens against the device's **live** data and return the resolved text — used by the pad editor's **Preview live values** button to show what a binding renders to without saving. Resolution runs on the main loop (LVGL task), so the request is deferred through the shared main-loop bridge; it resolves **values** only (not a pixel render). Nothing is persisted. Requires the same portal auth as the other `/api/pad*` routes. Gated `HAS_DISPLAY && HAS_MQTT`. Shares one resolver with the MCP `resolve_bindings` tool.

- **Request:**
```json
{
  "screen": "pad_0",
  "bindings": ["[time:%H:%M]", "[health:heap_free]"],
  "button": { "label_center": "[mqtt:home/solar;power;%.0f]W", "fg_color": "[expr:[net:any]?\"#22c55e\":\"#334155\"]" }
}
```
  `screen` (optional) supplies that pad's `[pad:name]` context. At least one of `bindings` / `button` is required; a `button` object resolves its bindable `label_*` / `*_color` / `btn_state` / `widget_data_binding[_2..4]` fields.

- **Response:**
```json
{
  "resolved": [ { "input": "[time:%H:%M]", "value": "14:32" }, { "input": "[health:heap_free]", "value": "182 KB" } ],
  "button": { "label_center": "1240W", "fg_color": "#22c55e" }
}
```
  Errors: `400` (bad params / invalid JSON), `503` (busy — another resolve/control job is in flight, retry; or out of memory), `500` (internal).

> **Pad save validation.** `POST /api/pad` validates the submitted pad through the shared `pad_validate()` (the same validator the MCP write tools use): grid bounds, span overflow, widget types/config caps, colors, action arrays, binding tokens (unknown scheme, bad health key, …), and the one-level `[pad:name]` rule. Buttons that fall outside a shrunken grid are tolerated (hidden, and reappear when the grid grows).


## Implementation Details

### Architecture

**Backend (C++):**
- `web_portal.cpp/h` - ESPAsyncWebServer with REST endpoints
- `web_portal_icons.cpp/h` - Icon store REST API (upload, delete, list, debug endpoints)
- `config_manager.cpp/h` - NVS (Non-Volatile Storage) for configuration
- `web_assets.h` - PROGMEM embedded HTML/CSS/JS (gzip compressed) (auto-generated)
- `project_branding.h` - `PROJECT_NAME` / `PROJECT_DISPLAY_NAME` defines (auto-generated)
- `log_manager.cpp/h` - Print-compatible logging with nested blocks (serial output only)

**Frontend (HTML/CSS/JS):**
- `web/home.html` - Home page (custom settings)
- `web/network.html` - Network configuration page
- `web/firmware.html` - Firmware update and factory reset page
- `web/bootstrap.min.css` - Bootstrap CSS framework (vendor, bundled into `portal-all.css`)
- `web/portal-custom.css` - Custom portal styles and responsive overrides (bundled into `portal-all.css`)
- `web/portal.js.bundle` - Bundle manifest listing all JS modules in concatenation order
- `web/portal.js` - Entry point (last in bundle); all JS served as a single bundled asset
- `web/portal_*.js` - Feature modules and fragments (see [Asset Bundle System](#asset-bundle-system))
- `web/portal-all.css` - Primary CSS file (bundle target) — served at `/portal-all.css`
- `web/portal-all.css.bundle` - CSS bundle manifest (see [CSS Bundle](#css-bundle))
- `web/_portal_*.css` - Feature CSS fragments (bundled into primary CSS at build time)
- `web/_*.html` (e.g. `_binding_help.html`, `_widget_*.html`, `_style_help.html`,
  `_health_widget.html`, `_reboot_overlay.html`) - Shared HTML partials inlined
  into pages and fragments by `tools/_render_html_template.py`

**Asset Compression:**
- All web assets are automatically minified and gzip compressed during build
- Reduces flash storage and bandwidth by ~80%
- Assets served with `Content-Encoding: gzip` header
- Browser automatically decompresses (transparent to user)

### Concurrent Request Throttling

The portal is served by ESPAsyncWebServer / AsyncTCP, whose TX buffers come from DMA-internal SRAM. On boards using ESP-Hosted SDIO (ESP32-P4 + ESP32-C6 co-processor) this pool is small and fragments easily — a parallel storm of large HTTP responses can cause a `copy_buff` NULL assert (`transport_drv_sta_tx`).

To keep concurrent in-flight HTTP requests bounded, the frontend applies several measures:

1. **`window.fetch` cap of 2** — `portal_core.js` wraps the native `fetch` with a small queue (`MAX_INFLIGHT = 2`). Excess calls wait until an in-flight request completes.
2. **`getDeviceInfo()` session cache** — `portal_core.js` exposes `getDeviceInfo(forceRefresh)` that issues at most one `GET /api/info` per page session and caches the result in `deviceInfoCache`. Concurrent first-time callers share a single in-flight promise. Pass `forceRefresh = true` after writes that change reported fields (e.g., saving a pad changes `available_screens`).
3. **`fetchHealthOnce()` shared snapshot** — `portal_health.js` keeps the most recent `/api/health` response in `latestHealth` (updated by every widget poll) and exposes `fetchHealthOnce()` for other fragments. The welcome and version-info fragments call it instead of issuing their own `/api/health` request: if a snapshot exists they get it immediately, otherwise concurrent first-time callers share a single in-flight promise. The widget's periodic poll loop is unchanged and continues to issue `GET /api/health` at the configured interval.
4. **Single CSS asset** — `bootstrap.min.css` + `portal-custom.css` are concatenated into `/portal-all.css` at build time (see [CSS Bundle](#css-bundle)), saving one request and ~1.5 KB through shared gzip dictionary.
5. **Inline favicon** — `shell.html` sets `<link rel="icon" href="data:,">` to suppress the browser's automatic `/favicon.ico` lookup (no extra request, no 404).
6. **`/api/mode` folded into `/api/info`** — the legacy mode endpoint was removed; portal JS derives `portalMode` from `ap_active` on the cached `/api/info` response.
7. **Chunked static asset streaming** — the gzipped PROGMEM asset handler in `web_portal_pages.cpp` (shell HTML, `portal-all.css`, `portal.js`, fragment HTML) uses a length-aware `AwsResponseFiller` callback with `HTTP_STREAM_CHUNK_SIZE = 4 KB` instead of `beginResponse_P()`. AsyncTCP only calls the filler when LWIP TX buffer space is available, so the response is paced segment by segment and the DMA-internal pbuf pool recycles between chunks. This matters because the browser issues `/portal-all.css` (~38 KB gz) and `/portal.js` (~53 KB gz) **in parallel** as `<link>` / `<script>` tags — those requests are not subject to the `window.fetch` cap and would otherwise queue the full ~90 KB of payload into the DMA-internal pool at once.

The result of these measures is that a fresh portal load on the `release/1.16.0` baseline issues approximately: `GET /`, `GET /portal-all.css`, `GET /portal.js`, `GET /api/info`, `GET /api/health`, `GET /api/portal/nav`, `GET /api/section/welcome` — plus the periodic `GET /api/health` poll. With the fetch cap, at most two are in flight at any moment.

### CPU Usage Calculation

CPU usage is calculated using FreeRTOS IDLE task monitoring:

```cpp
TaskStatus_t task_stats[16];
uint32_t total_runtime;
int task_count = uxTaskGetSystemState(task_stats, 16, &total_runtime);

uint32_t idle_runtime = 0;
for (int i = 0; i < task_count; i++) {
    if (strstr(task_stats[i].pcTaskName, "IDLE") != nullptr) {
        idle_runtime += task_stats[i].ulRunTimeCounter;
    }
}

float cpu_percent = 100.0 - ((float)idle_runtime / total_runtime) * 100.0;
```

### Temperature Sensor

Internal temperature sensor is available on newer ESP32 variants:
- ESP32-C3, ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C6, ESP32-H2

Code uses compile-time detection:
```cpp
#if SOC_TEMP_SENSOR_SUPPORTED
    // Use driver/temperature_sensor.h
#else
    // Return null for original ESP32
#endif
```

### Configuration Storage

Device configuration is stored in NVS (Non-Volatile Storage):
- Namespace: `device_config`
- Survives reboots and power cycles
- Factory reset available via REST API or button (if implemented)

### Captive Portal

DNS server redirects all requests to device IP in AP mode:
- Listens on port 53
- Wildcard DNS: `*` → `192.168.4.1`
- Works with most mobile OS captive portal detection

## Development Workflow

### Modifying the Web Interface

1. Edit files in one of the web asset roots:
  - Shared portal assets: `src/app/web/`
  - Device-class assets: `src/app/device_classes/<class>/web/`

  The bundler scans both locations. Use `src/app/web/` for shared, cross-board
  portal behavior. Use `src/app/device_classes/<class>/web/` for feature files
  that only make sense for one device class (for example, the e-paper page,
  its fragment init, and any device-class-specific JS state management).

  Under `src/app/web/`:
   - Shared HTML partials (inlined by `tools/_render_html_template.py`):
     - `_binding_help.html`, `_widget_*.html`, `_style_help.html`,
       `_health_widget.html`, `_reboot_overlay.html`
   - Page files:
     - `home.html` - Home page structure
     - `network.html` - Network configuration
     - `firmware.html` - Firmware update and reset
   - Styling and logic:
     - `bootstrap.min.css` - Bootstrap CSS framework (vendor, bundled)
     - `portal-custom.css` - Custom portal styles and overrides (bundled)
     - `portal-all.css` - Primary CSS file served by the device (bundle output)
     - `portal-all.css.bundle` - CSS bundle manifest
     - `_portal_*.css` - Feature CSS fragments (bundled into primary CSS)
     - `portal.js.bundle` - Bundle manifest (module load order)
     - `portal_*.js` - JS feature modules and fragments
     - `portal.js` - Entry point (must be last in bundle)

2. Rebuild to embed assets:
   ```bash
   ./build.sh
   ```
   
   This automatically:
  - Replaces `{{HEADER}}`, `{{NAV}}`, `{{FOOTER}}`, `{{BINDING_HELP}}` placeholders in HTML pages
   - Minifies HTML (removes comments, collapses whitespace)
   - Minifies CSS using `csscompressor`
   - Concatenates JS modules per `portal.js.bundle` manifest, then minifies using `rjsmin`
   - Concatenates CSS fragments per `*.css.bundle` manifests (if present), then minifies using `csscompressor`
   - Gzip compresses all assets (level 9)
  - Generates `src/app/web_assets.h` with embedded byte arrays
  - Generates `src/app/project_branding.h` with `PROJECT_NAME` / `PROJECT_DISPLAY_NAME` defines
   
   The build script shows compression statistics:
   ```
   Asset Summary (Original → Minified → Gzipped):
     HTML home.html:     5234 → 3891 → 1256 bytes (-76% total)
     HTML network.html:  8912 → 6543 → 1987 bytes (-78% total)
     HTML firmware.html: 4231 → 3124 → 1098 bytes (-74% total)
     CSS  portal-all.css:      287228 → 274208 → 38475 bytes (-87% total)
     JS   portal.js:   329575 → 220746 → 50066 bytes (-85% total)
   ```

3. Upload and test:
   ```bash
   ./upload.sh
   ./monitor.sh
   ```

### Asset Bundle System

The minifier supports `.bundle` manifests for both JavaScript and CSS. A bundle manifest lists source files that should be concatenated into a single asset at build time, reducing HTTP requests, route handlers, and flash usage on the ESP32.

The same three shared functions (`discover_bundle_manifests`, `filter_bundle_fragments`, `concatenate_bundle`) handle both JS and CSS bundles in `tools/minify-web-assets.sh`.

#### JavaScript Bundle

All JavaScript source files are concatenated into a single `portal.js` asset at build time.

**How it works:**

1. `src/app/web/portal.js.bundle` lists every JS module in dependency order (one filename per line, `#` comments ignored)
2. `tools/minify-web-assets.sh` resolves each manifest entry by checking `src/app/web/` first, then `src/app/device_classes/*/web/`
3. The minifier concatenates the resolved files, minifies the result, and gzip-compresses it into `web_assets.h`
4. Every HTML page loads a single `<script src="/portal.js"></script>`
5. One C++ handler (`handleJS`) serves the bundled asset

**Module organization:**

| Category | Files | Purpose |
|---|---|---|
| Core | `portal_core.js` | Navigation, mode detection, shared utilities |
| Shared libraries | `portal_binding_validator.js`, `portal_action_editor.js` | Reusable components used by multiple pages |
| Configuration | `portal_config_actions.js`, `portal_config.js` | Home page settings (fragments before main) |
| Firmware | `portal_firmware.js` | Firmware update page |
| Health | `portal_health_sparkline.js`, `portal_health.js` | Health widget (sparkline fragment before main) |
| Pad editor | `portal_pad_colors.js`, `portal_pad_io.js`, `portal_pad_blocks.js`, `portal_pad_icons.js`, `portal_pad_defaults.js`, `portal_pad_grid.js`, `portal_pad_dialog.js`, `portal_pad_editor.js` | Visual pad editor (fragments before main) |
| Entry point | `portal.js` | Must be last — bootstraps the application |

**Adding a new JS module:**

1. Create the module in the correct root:
  - Shared module: `src/app/web/portal_myfeature.js`
  - Device-class module: `src/app/device_classes/<class>/web/portal_myfeature.js`
2. Add the filename (basename only) to `portal.js.bundle` in the correct dependency position (before any file that calls its functions, after any file it depends on)
3. Run `./build.sh` - the module is automatically included in the bundle

**Fragment pattern:** Large modules are split into a main file and one or more fragment files. Fragments are listed before the main file in the bundle manifest so their functions are available when the main file executes. For example, `portal_health_sparkline.js` (fragment) appears before `portal_health.js` (main).

**Build-time validation:** The build automatically validates the bundle before minification:

- **Manifest check** — every file listed in the `.js.bundle` manifest must exist; missing files cause a hard build error
- **Syntax check** — every `.js` file is checked with `node --check` to catch missing braces, unterminated strings, and other parse errors before minification

> **Feature branches** may add additional modules (e.g., `portal_action_editor_darkroom.js`, `portal_brews.js`). These follow the same pattern: add the file, add it to the bundle manifest, no C++ changes needed.

#### CSS Bundle

CSS files can also be bundled using the same manifest pattern. This allows feature branches to keep their CSS in separate `_`-prefixed files that get concatenated into the primary stylesheet at build time — avoiding merge conflicts in shared CSS files.

**How it works:**

1. `src/app/web/portal-all.css.bundle` lists CSS files in cascade order (one filename per line, `#` comments ignored)
2. The minifier concatenates the listed files, minifies with `csscompressor`, and gzip-compresses the result into `web_assets.h`
3. The primary CSS file (e.g., `portal-all.css`) is served as a single asset — same as without a bundle

**Concatenation order matters:** CSS cascade rules mean later entries override earlier ones at equal specificity. List base/shared styles first and feature-specific overrides last.

**Adding feature CSS:**

1. Create `src/app/web/_portal_myfeature.css` (underscore prefix excludes it from individual serving)
2. Create or update `portal-all.css.bundle` with the filename before the primary CSS file
3. Run `./build.sh` — the feature CSS is automatically included in the bundle

**Build-time validation:** Every file listed in a `.css.bundle` manifest must exist; missing files cause a hard build error.

### Adding REST Endpoints

1. Add handler function in `web_portal.cpp`:
   ```cpp
   void handleNewEndpoint(AsyncWebServerRequest *request) {
       JsonDocument doc;
       doc["data"] = "value";
       String response;
       serializeJson(doc, response);
       request->send(200, "application/json", response);
   }
   ```

2. Register route in `web_portal_init()`:
   ```cpp
   server->on("/api/newpoint", HTTP_GET, handleNewEndpoint);
   ```

3. Update `web_portal.h` documentation comments

4. Document in this file (REST API section)

### Testing Portal Modes

**Test Core Mode:**
```bash
# Reset config to trigger AP mode
curl -X DELETE http://192.168.1.100/api/config
# Or erase flash
./upload-erase.sh
./upload.sh
```

**Test Full Mode:**
```bash
# Configure WiFi via portal, then access at device IP
curl http://192.168.1.100/api/info
```

## Troubleshooting

### Cannot Access Portal in AP Mode

- Check SSID: Should be `ESP32-XXXXXX`
- IP address: Always `192.168.4.1`
- Disable mobile data (can interfere with captive portal)

### Portal Not Responding After WiFi Config

- Check serial monitor for IP address
- Verify WiFi credentials are correct
- Check router DHCP settings
- Use fixed IP if DHCP fails

### WiFi Connection Issues After OTA or Reboot

If WiFi fails to connect after firmware update or reboot:
- Device has automatic reconnection with 10-second watchdog
- Hardware reset sequence clears stale WiFi state on each connection attempt
- Check serial logs for detailed connection status (SSID not found, wrong password, etc.)
- If persistent, use physical reset button to fully power-cycle WiFi hardware
- Auto-reconnect and event handlers ensure recovery from temporary drops

### OTA Update Fails

- Verify `.bin` file (not `.elf` or other format)
- Check file size vs available OTA partition space
- Ensure stable power supply during update
- Monitor serial output for error details

### Health Monitoring Shows "N/A"

- **Temperature N/A**: Normal on original ESP32 (no internal sensor)
- **WiFi stats N/A**: Normal when not connected to WiFi
- **CPU 0%**: Check FreeRTOS configuration

### Cannot Connect to mDNS Hostname

- Windows: Install Bonjour service
- Linux: Install `avahi-daemon`
- Some networks block mDNS (use IP instead)

### Entering Config Mode Without a Button

If `POWERON_CONFIG_BURST_ENABLED` is enabled at compile time, you can force Config Mode by power-cycling the device twice within ~10 seconds. This uses a small NVS counter and only triggers on `ESP_RST_POWERON`.
This is intended for boards **without a reliable user button**.

Behavior details:
- Counter increments only on `ESP_RST_POWERON`.
- Counter is cleared after ~10 seconds of uptime.
- Deep sleep and other reset reasons do not affect the counter.

## Security Considerations

**Current Implementation:**
- Optional HTTP Basic Authentication for the portal UI pages and all `/api/*` endpoints (Full Mode only)
- In Core Mode (AP + captive portal), authentication is intentionally disabled to allow initial provisioning
- Basic Auth credentials cannot be changed via the web UI/API while in Core Mode

**Limitations:**
- The portal uses plain HTTP by default; HTTP Basic Auth does not provide transport encryption. Use only on trusted networks, or put the device behind a VPN/reverse proxy/TLS terminator.

**Production Recommendations:**
- Enable HTTP Basic Auth when the device is reachable on a shared network
- Prefer HTTPS (with real certificate validation) when feasible
- Implement rate limiting on sensitive endpoints
- Add CSRF protection for POST/DELETE operations
- Whitelist allowed WiFi SSIDs (prevent evil twin attacks)

## Related Documentation

- [Script Reference](scripts.md) - Build and upload workflows
- [Library Management](library-management.md) - Adding dependencies
- [WSL Development](wsl-development.md) - Windows development setup
