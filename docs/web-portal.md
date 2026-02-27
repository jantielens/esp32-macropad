# Web Configuration Portal

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

### Configuration Pages

#### Home Page (`/` or `/home.html`)

**Available In:** Full Mode only (redirects to Network page in AP mode)

**Sections:**
- **👋 Hello World**: Welcome message with customization tip
- **⚙️ Sample Settings**: Example configuration field (dummy_setting)
- **⚡ Operating Mode & Cadence**: Mode selection, transport, cycle interval, portal idle timeout, WiFi backoff cap, and MQTT payload scope
- **BLE Advertising**: Burst timing controls (only shown when firmware enables BLE)

**Layout:** Sections use 2-column grids on desktop (≥768px), stacked on mobile

**Purpose:** Demonstrates how to add custom application-specific settings

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
  - Periodic publish cadence follows `cycle_interval_seconds` in Power Settings

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
- **🔄 Factory Reset**: Reset all configuration to defaults
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
  "hostname": "esp32-1234"
}
```

**Discovery Fields:**
- `mac_address`: Device MAC address
- `wifi_hostname`: WiFi/DHCP hostname
- `mdns_name`: Full mDNS name (hostname + `.local`)
- `hostname`: Short hostname

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
  "dummy_setting": "",

  "power_mode": "always_on",
  "publish_transport": "ble",
  "cycle_interval_seconds": 120,
  "portal_idle_timeout_seconds": 120,
  "wifi_backoff_max_seconds": 900,
  "ble_adv_burst_ms": 900,
  "ble_adv_gap_ms": 1100,
  "ble_adv_bursts": 2,
  "ble_adv_interval_ms": 100,
  "mqtt_publish_scope": "sensors_only",

  "basic_auth_enabled": false,
  "basic_auth_username": "",
  "basic_auth_password_set": false,

  "backlight_brightness": 100,

  "screen_saver_enabled": false,
  "screen_saver_timeout_seconds": 300,
  "screen_saver_fade_out_ms": 800,
  "screen_saver_fade_in_ms": 400,
  "screen_saver_wake_on_touch": true
}
```

**Notes:**
- Some fields are build-time gated.
  - Display-related fields (backlight + screen saver) are present when `HAS_DISPLAY` is enabled.
  - BLE advertising fields are only used when `HAS_BLE` is enabled.
  - Other feature-specific fields may be present depending on firmware configuration.

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
  "dummy_setting": "value",

  "power_mode": "duty_cycle",
  "publish_transport": "ble_mqtt",
  "cycle_interval_seconds": 120,
  "portal_idle_timeout_seconds": 120,
  "wifi_backoff_max_seconds": 900,
  "ble_adv_burst_ms": 900,
  "ble_adv_gap_ms": 1100,
  "ble_adv_bursts": 2,
  "ble_adv_interval_ms": 100,
  "mqtt_publish_scope": "sensors_only",

  "basic_auth_enabled": true,
  "basic_auth_username": "admin",
  "basic_auth_password": "change-me",

  "backlight_brightness": 70,

  "screen_saver_enabled": true,
  "screen_saver_timeout_seconds": 300,
  "screen_saver_fade_out_ms": 800,
  "screen_saver_fade_in_ms": 400,
  "screen_saver_wake_on_touch": true
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
- Basic Auth password is never returned by `GET /api/config`.
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

### Portal Mode

#### `GET /api/mode`

Returns current portal operating mode.

**Response:**
```json
{
  "mode": "core",
  "ap_active": true
}
```

**Modes:**
- `"core"`: AP mode with captive portal
- `"full"`: WiFi connected mode

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

Set backlight brightness immediately (does not persist to NVS).

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


## Implementation Details

### Architecture

**Backend (C++):**
- `web_portal.cpp/h` - ESPAsyncWebServer with REST endpoints
- `config_manager.cpp/h` - NVS (Non-Volatile Storage) for configuration
- `web_assets.h` - PROGMEM embedded HTML/CSS/JS (gzip compressed) (auto-generated)
- `project_branding.h` - `PROJECT_NAME` / `PROJECT_DISPLAY_NAME` defines (auto-generated)
- `log_manager.cpp/h` - Print-compatible logging with nested blocks (serial output only)

**Frontend (HTML/CSS/JS):**
- `web/_header.html` - Shared HTML head template (DRY)
- `web/_nav.html` - Shared navigation and loading overlay template (DRY)
- `web/_footer.html` - Shared form buttons template (DRY)
- `web/home.html` - Home page (custom settings)
- `web/network.html` - Network configuration page
- `web/firmware.html` - Firmware update and factory reset page
- `web/portal.css` - Minimalist card-based design with gradients and responsive grid
- `web/portal.js` - Vanilla JavaScript with multi-page support (no frameworks)

**Asset Compression:**
- All web assets are automatically minified and gzip compressed during build
- Reduces flash storage and bandwidth by ~80%
- Assets served with `Content-Encoding: gzip` header
- Browser automatically decompresses (transparent to user)

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

1. Edit files in `src/app/web/`:
   - Template fragments (shared):
     - `_header.html` - HTML head section
     - `_nav.html` - Navigation tabs and loading overlay
     - `_footer.html` - Form buttons
   - Page files:
     - `home.html` - Home page structure
     - `network.html` - Network configuration
     - `firmware.html` - Firmware update and reset
   - Styling and logic:
     - `portal.css` - Styles and responsive grid
     - `portal.js` - Client logic with multi-page support

2. Rebuild to embed assets:
   ```bash
   ./build.sh
   ```
   
   This automatically:
   - Replaces `{{HEADER}}`, `{{NAV}}`, `{{FOOTER}}` placeholders in HTML pages
   - Minifies HTML (removes comments, collapses whitespace)
   - Minifies CSS using `csscompressor`
   - Minifies JavaScript using `rjsmin`
   - Gzip compresses all assets (level 9)
  - Generates `src/app/web_assets.h` with embedded byte arrays
  - Generates `src/app/project_branding.h` with `PROJECT_NAME` / `PROJECT_DISPLAY_NAME` defines
   
   The build script shows compression statistics:
   ```
   Asset Summary (Original → Minified → Gzipped):
     HTML home.html:     5234 → 3891 → 1256 bytes (-76% total)
     HTML network.html:  8912 → 6543 → 1987 bytes (-78% total)
     HTML firmware.html: 4231 → 3124 → 1098 bytes (-74% total)
     CSS  portal.css:   14348 → 10539 → 2864 bytes (-81% total)
     JS   portal.js:    32032 → 19700 → 4931 bytes (-85% total)
   ```

3. Upload and test:
   ```bash
   ./upload.sh
   ./monitor.sh
   ```

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
