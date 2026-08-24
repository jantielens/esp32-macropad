---
title: Web Portal Guide
description: Configure ESP32 Macropad device settings, pads, integrations, and firmware from the web portal
---

The ESP32 Macropad includes a built-in web portal for configuring every aspect of your device — from Wi-Fi settings to pad layouts and e-paper image scheduling — all from your browser.

## Accessing the Portal

| Mode | When | URL |
|------|------|-----|
| **AP Mode** (first boot / factory reset) | Wi-Fi not configured | Connect to the device's Wi-Fi, then go to `http://192.168.4.1` |
| **Full Mode** (normal operation) | Connected to your Wi-Fi | `http://<device-name>.local` or the device's IP address |

In AP mode, only the Network page is available. In Full mode, the standard four pages are accessible, and e-paper boards also expose a dedicated E-Paper page.

## Header & Health Monitoring

The portal header shows real-time device info at a glance:

- **Device name** — the configured device name, also used for the browser tab title
- **Device class** — the firmware's device type (for example, Macropad or Coffee Scale)
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
- **Heap memory** — free, minimum, largest block, and fragmentation. On MIPI-DSI boards, largest-block data can be up to 30 seconds old.
- **PSRAM** — free and minimum values for external RAM (when present). MIPI-DSI boards do not report PSRAM largest-block data because measuring it can disrupt display scan-out.
- **Flash usage** — firmware size
- **Filesystem** — active storage backend, mount state, and usage. SD primary-storage variants also report card type.
- **MQTT** — connection status and publish timing
- **Display** — FPS and render timing
- **Wi-Fi signal** — RSSI and IP address

---

## SD Primary Storage

The `jc1060p470c-sd`, `jc3636w518-sd`, and `jc4880p433-sd` firmware targets
store pad configurations, icons, sounds, and indexed data on a FAT32 MicroSD
card. They halt at startup when the card is missing or unreadable instead of
falling back to internal flash. The Health overlay reports `SDMMC` as the
filesystem backend after a successful mount.

## Storage Page

The **Storage** page in the **Device** category shows the active backend,
mount status, card type when applicable, total capacity, and used/free space.
It also provides a read-only folder browser. Folders appear before files and
entries are alphabetical within each group. Expand a folder to view its direct
contents, select **Open** beside a file to view supported media in your browser
or download other file types, or use **Refresh** to reload the storage summary
and root folder.

The general Storage page does not support file uploads, deletion, or
formatting. Feature-specific views may offer deletion for their own files; the
Camera **Snapshots** page can remove a saved image, a day's camera-roll folder,
or the latest snapshot copy.

## Music Library

Audio builds with the sound player enabled include a **Music Library** page. It lists
the device's CD: up to 32 MP3 files found under `/media`, sorted in a stable
order. When more files exist, the page shows the first 32 and an overflow
warning; delete a listed file and the catalog refreshes to reveal the next one.
Use **Upload** to add a new MP3 and **Delete** to remove a listed file.
Nested folders are supported. The page is for library management only and does
not contain playback, selection, reordering, refresh, or seeking controls.

When an MP3 includes ID3v2 metadata, Music Library displays its title and artist
rather than only its filename. Duration is read from Xing/Info, VBRI, or a CBR
bitrate estimate without decoding the complete track; estimated durations are
marked as such.

Upload and delete are unavailable while Music or an MP3 Alert is active. Tone
Alerts do not block library management. After a successful upload or delete,
the list refreshes automatically. The portal validates the destination path and
publishes the uploaded bytes without decoding the entire MP3 first.

---

## Home Page

*Available in Full mode only.*

The Home page provides a welcome overview with quick links to the other pages, plus device behavior settings.

### Operating Mode

Controls how the device operates:

| Setting | Description |
|---------|-------------|
| **Operating Mode** | **Always-On** keeps all services running continuously. **Duty-Cycle** wakes periodically, publishes data, then goes back to sleep to save power |
| **Duty-Cycle Wake Interval** | How often the device wakes in duty-cycle mode (seconds). Shown only when Duty-Cycle is selected. 0 = wake immediately after each cycle |
| **Recovery Portal Auto-Sleep** | Active only in Config / AP recovery mode (when Wi-Fi can't connect). After this many seconds of no portal activity, the device deep-sleeps for the same duration, then wakes to retry Wi-Fi. 0 = disabled |
| **Wi-Fi Backoff Max** | Maximum delay between Wi-Fi reconnection attempts (seconds) |

MQTT publish interval and payload scope live on the **Network** page under the **MQTT** card. The publish interval is only used in Always-On mode; in Duty-Cycle mode the device publishes once per wake.

### BLE Keyboard

*Shown only on boards with BLE HID support (ESP32-P4 boards). Not available on ESP32-S3 boards due to internal RAM constraints.*

The BLE Keyboard section lets you enable/disable the Bluetooth keyboard and manage pairing.

| Element | Description |
|---------|-------------|
| **Enable BLE Keyboard** | Checkbox to enable or disable BLE. Disabled by default to save ~70 KB RAM. Requires a reboot to take effect |
| **Status indicator** | Reflects the compact `ble_status`: disabled, ready, pairing, connected, or error |
| **Name** | Shows the current BLE keyboard name (same as the configured device name) |
| **Bonded / Encrypted badges** | Shown when a host is connected |
| **Peer address** | The connected host's Bluetooth address |
| **Pair New Device** | Clears the previous bond and opens a fresh 60-second pairing window — no reboot required |

You can also trigger pairing from a button on the device by assigning the `ble_pair` action.

The BLE keyboard always advertises with the configured device name and the chip's stable hardware address, so the host always sees the same device.

> **Re-pairing tip:** Before pairing a new host (or re-pairing the same host), remove the device from the old host's Bluetooth settings first. If you skip this step the old host may keep trying to reconnect with stale keys for a short while — this is normal BLE behavior and will eventually stop, but removing the device avoids the noise.

### Audio

*Shown only on boards with audio hardware.*

The **Audio** navigation category separates output level, button-feedback tones,
alert-sound files, and the Music Library.

#### Volume

| Element | Description |
|---------|-------------|
| **Volume** | Slider (0–100%) controlling the device audio volume. It applies to alerts, Music, and button feedback. Persisted in NVS and controllable from Home Assistant. |

#### Button Feedback

| Element | Description |
|---------|-------------|
| **Tap Feedback** | Tone pattern played after a button tap that has an action configured. Leave empty for no sound. |
| **Long-Press Feedback** | Tone pattern played after a button long-press that has a long-press action configured. Leave empty for no sound. |

**Tone pattern DSL:** Space-separated `freq:dur` pairs (Hz and milliseconds). A bare number is a silent gap. Examples: `800:80` (single click), `600:40 40 600:40` (double chirp), `1000:30 30 1200:30` (rising two-tone).

Buttons with no actions configured are completely inert — no visual tap flash and no audio cue. If any action in a button's sequence produces its own audio (a Sound Alert action), the device-level feedback beep is automatically suppressed to avoid overlapping audio. Swipe gestures also use the device-level tap beep with the same suppression logic.

When MQTT is connected, the device also registers audio entities in Home Assistant (siren, volume, beep buttons, and a custom tone text entity). See the [Home Assistant Integration Guide](ha-integration-guide.md) for details and automation examples.

### Voice Assistant

*Shown only on the `esp32-p4-lcd4b-voice` board variant.*

The **Voice Assistant** page is the primary portal category for this board
variant. It configures the Azure AI Foundry host, transcription deployment, and
API key. Set an optional two-letter ISO 639-1 language code, such as `en` or
`nl`, to select the transcription language; leave it blank for Azure
auto-detection. The API key is stored on the device as a write-only value: leave
the field blank to keep the saved key. The portal reports only whether the key
is configured. The public Azure CA certificate is versioned in the firmware.

For a one-tap capture button, configure **Record until silence** followed by a
Publish MQTT action whose payload is `[stt:text]`. Set the trailing silence in
milliseconds (default `1000`) and a speech-level threshold from 0 to 100
(default `2`), using the same RMS scale as `[audio:input.rms]`. It waits for
speech before starting the trailing-silence timer. Use **Stop and transcribe**
as a separate control to finish an active recording early, or **Cancel
recording** to discard an active recording without transcription. The first
release does not subscribe to LLM responses or attach correlation IDs, so reply
ordering is the responsibility of the MQTT automation.

The page also stores independent Azure Text-to-Speech host, speech deployment,
write-only API key, optional two-letter ISO 639-1 language, optional TTS
instructions, and default voice (`alloy`). The language is included as Azure
speech guidance. TTS instructions are passed verbatim to Azure and can guide
dialect, accent, or pronunciation. Add a **Speak text** Voice Assistant action to request an MP3 from a
`gpt-4o-mini-tts` deployment. Its text field supports normal binding templates;
an action can optionally override the configured voice and audio volume. A new
speech request stops the previous playback and supersedes an older request that
finishes downloading later. Voice overrides must name a supported Azure voice;
they are not language or locale fields.

Azure transcription waits up to 30 seconds and does not retry automatically.
If it fails, `[stt:status]` becomes `error`, `[stt:text]` contains the reason,
and remaining actions in that recording's list do not run. Correct the reported
problem and start another recording. Speak text is best-effort: it does not
pause its action list, and an Azure TTS failure skips that speech request while
recording the reason in the device log.

#### Alert Sounds

*Shown only on boards with sound player support (defaults to boards with audio hardware).*

Upload MP3 files to play as button actions or via MQTT. Files are stored on the device's LittleFS filesystem.

| Element | Description |
|---------|-------------|
| **Sound list** | Shows all uploaded sounds with Play and Delete buttons |
| **Name** | Identifier for the sound (letters, numbers, hyphens, underscores). Auto-populated from the selected filename |
| **MP3 File** | File picker for `.mp3` files (max 512 KB per file). The server validates the MP3 header on upload |
| **Upload** | Uploads the file to the device |

Once uploaded, alert sounds are available through **Sound Alert** with the **MP3 Alert** kind in the button editor, swipe actions, and boot actions.

#### BLE Signals

The firmware exposes two BLE health signals for bindings and diagnostics:

| Signal | Purpose | Values |
|--------|---------|--------|
| **`ble_status`** | Compact user-facing status | `disabled`, `ready`, `pairing`, `connected`, `error` |
| **`ble_state`** | Detailed diagnostic status | `disabled`, `idle`, `advertising`, `pairing`, `connecting`, `claimed`, `secured`, `error` |

**`ble_status` values:**

| Value | Meaning |
|-------|---------|
| `disabled` | BLE keyboard is turned off in runtime configuration |
| `ready` | BLE keyboard is enabled and waiting, but not currently pairing or in a secured active session |
| `pairing` | BLE keyboard is in the new-device pairing window |
| `connected` | A host is connected and the BLE HID session is encrypted and usable |
| `error` | BLE initialization failed or the BLE stack entered a fault state |

**`ble_state` values:**

| Value | Meaning |
|-------|---------|
| `disabled` | BLE keyboard is turned off in runtime configuration |
| `idle` | BLE stack is initialized but not advertising a stronger user-visible condition |
| `advertising` | BLE is advertising without an owner claim yet |
| `pairing` | BLE is in pairing mode and waiting for a new host |
| `connecting` | A host connection exists, but the secure HID session is not fully established yet |
| `claimed` | BLE has an owner on record and is advertising for that owner to reconnect |
| `secured` | BLE has an encrypted active connection |
| `error` | BLE initialization failed or the BLE stack entered a fault state |

### Brightness

*Shown only on boards with a display.*

| Setting | Description |
|---------|-------------|
| **Backlight Brightness** | Slider (0–100%). Changes take effect immediately; save to persist across reboots |

### Screen Preview

Select the active screen and capture the current device framebuffer in the
portal. Screen changes apply immediately and are not saved. Preview capture is
manual, so opening the fragment does not allocate a framebuffer or encode an
image. Use **Refresh Preview** after changing screens or when the displayed
content changes.

On boards with a touch display, a captured preview is also interactive. Click or
tap a point on the screenshot to queue one normal device tap at that pixel. The
portal refreshes the preview once after the request is accepted. Clicks in the
empty bars around a portrait or landscape image are ignored.

The preview is a best-effort snapshot. A queued tap can wait for a physical
touch to release or for the screen saver to wake, and the screen can change
before the device consumes it. After changing **Current Screen**, the old image
is hidden and cannot be tapped; capture a fresh preview first. Screen Preview
does not support dragging, swiping, long presses, multi-touch, or live video.

### Screen Saver (Burn-in Prevention)

The screen saver has two optional features, both measured from the most recent user activity. **Idle Screen** temporarily shows a configured pad, which can host an animated clock or extension. **Display Sleep** turns off the panel to protect it from burn-in. Either can be used alone, or Idle Screen can lead into Display Sleep. A built-in pixel-shift mechanism moves content slightly each Display Sleep cycle to prevent ghosting. The portal shows a live timeline of the current settings above the controls.

| Setting | Description |
|---------|-------------|
| **Turn off display after inactivity** | Turn automatic panel sleep on or off |
| **Turn off display after** | Seconds of inactivity before panel sleep (0 = disabled) |
| **Show a standby pad** | Enable a transient pad when the device is idle, with or without Display Sleep |
| **Show standby pad after** | Seconds of inactivity before the configured pad appears (0 = disabled) |
| **Standby pad** | Pad to show while idle; it remains available as a normal pad elsewhere |
| **Fade Out** | Fade-to-black duration when entering Display Sleep (ms, 0 = instant) |
| **Fade In** | Fade-from-black duration when leaving Display Sleep (ms, 0 = instant) |
| **Wake on touch press** | One touch fully exits Idle Screen or wakes Display Sleep |
| **MQTT Wake and Keep Awake Binding** | An ON binding fully exits Idle Screen or wakes Display Sleep, and keeps both stages off while it remains ON (e.g. `[mqtt:devices/node/presence/state]`) |

For example, an Idle Screen at 300 seconds and Display Sleep at 1800 seconds shows the selected pad after five minutes, then turns off the panel after 30 minutes total. The first wake interaction is consumed and returns to the screen that was active before the Idle Screen appeared, so the temporary pad is not added to navigation history.

### Swipe Actions

*Shown only on boards with a display.*

Configure what happens when you swipe in each direction. Swipe gestures work on all screens and use the same action system as buttons (screen navigation, MQTT publish, BLE key sequence, beep, sound, etc.).

Each of the four directions (left, right, up, down) can have one action. By default, swipe right navigates back.

### Boot Actions

*Shown only on boards with a display.*

Configure up to 3 sequential actions to run automatically when the device boots — for example, play a startup sound, publish an MQTT message, or navigate to a specific pad. Boot actions use the same action editor as buttons and swipe gestures.

Actions are dispatched once after the first screen is shown during boot. Changes take effect on next reboot.

### MQTT Triggers

*Shown on boards with MQTT and either a display or a physical button.*

Dispatch actions automatically when a matching MQTT message arrives — no button press or screen interaction needed. Each trigger has a **Topic**, an optional **Value filter**, and up to 3 sequential actions (the same action editor as buttons, swipe, and boot actions).

When a message is received on the topic, its payload is compared to the value filter: leave the filter empty to match any message on the topic, or set it to an exact string (e.g. `ON`) to fire only on that payload. Wildcard topics (`#`, `+`) are not supported — enter exact topic names.

> **Tip:** Avoid an empty-value trigger on a topic that the trigger's own actions publish to, to prevent message loops.

The number of available trigger slots depends on the board (8 by default, fewer on memory-constrained boards). Changes apply immediately and subscriptions are re-established whenever the device reconnects to the MQTT broker.

### Timers

*Shown only on boards with a display.*

Configure up to three expiry actions for each on-device timer slot. The Timer action editor sets the mode on every Start and Toggle action and shows Duration when Countdown is selected.

Expire actions use the same action editor as buttons, so you can play a sound, send an MQTT message, navigate to a screen, play a beep pattern, or any combination. This replaces the previous beep-only expiry with full action parity.

When a countdown starts, it copies the slot's saved expiry list. Changes apply to the next run and do not alter a countdown already in progress. After saving an expiry-list change, stop and start a running timer to use it. Runtime state, mode, and duration are not persisted across reboot. Use `[timer:N]` bindings on pad button labels to display timer values. Use `[timer:N_target]` for the active countdown preset in whole seconds, such as a gauge maximum. The target changes after Start, Set, and Adjust, but normal ticking, Stop, and Reset leave it unchanged. Count-up and unconfigured timers return `0`.

## E-Paper Page

*Available only on e-paper boards such as the Inkplate 5V2.*

The E-Paper page configures the battery-oriented image workflow:

| Setting | Description |
|---------|-------------|
| **Image sources** | Up to 5 image slots, each with its own URL, Duration, and Stay flag. Empty slots are skipped top-to-bottom. |
| **Hourly refresh window** | 24-hour local-time mask that can disable refreshes during selected hours to save battery |
| **Timezone offset** | Fixed UTC offset used to evaluate the hourly window |
| **WiFi Failure Backoff** | Maximum WiFi retry sleep after repeated failures |
| **Status** | Read-only refresh counters, timing, battery, and manual refresh action |
| **Overlay** | Status overlay position/color/fields drawn on the image |
| **VCOM** | Inkplate TPS65186 calibration controls |

Duration is per slot and applies while that slot is active. The page does not expose a separate "wake every" control.

---

## Pads Page

*Available in Full mode only.*

The Pads page is the heart of ESP32 Macropad — this is where you design your touch screen layouts. It supports up to 16 independent pads, each with a configurable grid of buttons that can display live data, trigger MQTT actions, and change color dynamically.

The Pads page has its own floating footer with **Save Pad**, **Show on Device**, and a **More** menu for bulk operations (Fill, Copy/Paste Pad, Export/Import) and **Building Blocks** — pre-configured button groups you can place into a pad with a single click. While the current pad has unsaved changes, a fixed **Save Pad** button also appears at the lower-right of the page. This is completely separate from the device config Save & Reboot footer on other pages.

The **Pad and Button Defaults** section at the bottom of the Pads page sets device-wide pad background and layout defaults alongside button colors, borders, and label styles. Pads and buttons inherit the applicable settings automatically, while explicit overrides still take precedence.

Label fields in the button editor support explicit line breaks with `\n` (for example, `Line 1\nLine 2`). This applies to button labels (Top/Center/Bottom) and gauge start labels.

For sensitive normal-button actions, open **Action Safety** in the Actions group and enable **Confirm before tap or long-press actions**. The device shows an explicit Cancel/Confirm prompt before running either action list, uses the optional custom message when present, and cancels automatically after 10 seconds.

Choose **Timer / Delay** when a later action must wait. Set a whole-number
duration from 1 to 55,000 ms. Delay pauses only the current ordered action list;
when it completes, the next action runs. By default, up to three pausable actions
can be pending device-wide at a time; starting another pausable action when all
slots are occupied stops its action list.

The Table widget's **Data Binding** field accepts structured table payload bindings such as `[health:table]` and `[health:extended_table]`. Use an exact single-token expression (no static prefix/suffix text and no format parameter) so the widget receives the full schema payload.

On camera-enabled boards, select **Camera Preview** as a button's widget to display the shared camera feed. Choose Cover to fill the button, Letterbox to show the complete image without stretching, or Center crop to clip an unscaled centered image.

On ESP32-P4 builds, the **Extensions** page has two small slots
and one large slot for trusted native Extensions. Upload the signed package
`extension-id@version.ext`, then reboot to install it into executable flash.
The package contains the Extension ELF and its first-party signature; unsigned
or modified packages are rejected. Select **Extension** as a button's widget,
choose an enabled installed extension, and optionally provide per-button
configuration text.

Switching between pads or navigating away with unsaved changes shows a confirmation dialog to prevent accidental data loss.

For the complete guide — including binding template syntax, widget configuration (bar charts, gauges, sparklines, tables, rockers), label styling, dynamic colors, pad bindings (named data sources), building blocks, and real-world examples (including a dual-binding gauge power-balance setup) — see the **[Pad Editor Guide](pad-editor-guide.md)**.

All binding fields validate syntax in real time as you type — bracket balance, scheme names, parameter counts, format strings, expression syntax, and known health, timer, and Music keys (including `table`, `extended_table`, and `status`) are checked with inline error messages. See [Binding Validation](pad-editor-guide.md#binding-validation) for details.

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

> **Warning**: This cannot be undone. Everything stored on the device is wiped: all settings (WiFi, MQTT, BLE, display), all pad layouts, pad and button defaults, timers, swipe and boot actions, stored icons, stored sounds, indexed-store data (e.g. shutter / scale sessions), and any BLE pairings.

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

A **🔄 reboot button** also lives in the portal header (next to the light/dark theme toggle) and is available on every page. It prompts for confirmation, then reboots without saving — handy when you've made a change elsewhere (e.g., the API) and just need to restart.

After a reboot, the portal shows an automatic reconnection dialog. If it can't reconnect (e.g., the device name changed), it provides a manual link with the new address.

---

## Tips & Tricks

- **Bookmark your device**: After setup, bookmark `http://<device-name>.local` for quick access
- **Back up your config**: Use **Export Device Config** regularly — it saves everything into a single JSON file
- **Clone devices**: Export from one device, import on another to duplicate your setup
- **Copy/paste buttons**: The button editor has Copy and Paste buttons to quickly duplicate button settings across cells. Both keep the editor open so you can keep working, and column/row spans are preserved when space allows
- **Drag to resize**: Hover over a button to see edge handles, then drag to grow or shrink it across cells — faster than editing span values manually
- **Binding templates**: Mix static text with live data — e.g., `Solar: [mqtt:home/solar;power;%.0f W]` shows "Solar: 3500 W". For Table widget data, use exact single-token structured bindings such as `[health:table]`.
- **Security**: Enable HTTP Basic Auth on devices accessible from outside your home network
