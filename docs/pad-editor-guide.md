# Pad Editor Guide

The pad editor is the heart of ESP32 Macropad — it turns your touch screen into a fully custom dashboard, remote control, or status panel. Each device supports up to **16 independent pads**, each with its own grid of buttons that can display live data, control smart home devices, and react to real-time conditions.

You'll find the pad editor on the **Pads** page of the web portal (Full mode only). If you haven't connected your device to WiFi yet, complete the [first-time setup](first-time-setup.md) first.

---

## Designing a Pad

A pad is a grid of buttons displayed on the device — swipe or navigate between pads using button actions.

### Pad Settings

At the top of the pad editor, you configure the pad itself:

- **Pad selection** — switch between Pad 1 through 16. Each pad is saved independently.
- **Pad Name** — an optional label shown in Home Assistant and on-device. For example, "Solar", "Lights", or "Cameras".
- **Columns / Rows** — the grid size. The maximum depends on the board (e.g. 5×5 on round displays, 8×8 on larger panels). A 3×2 grid gives you 6 large buttons; a 4×4 grid gives you 16 smaller ones.
- **Wake Screen** — when the screensaver wakes up, which screen should appear? Leave empty to return to the last active screen, or pick a specific pad.
- **Background** — the color behind the grid. Accepts a `#hex` color or a binding expression for dynamic backgrounds.

> **Example**: A home energy dashboard might use a 4×2 grid named "Energy" with a dark background (`#111111`) — four columns for solar, grid, battery, and net power, with two rows for the bar chart and its label.

### Button Defaults

The **Button Defaults** section (collapsible, at the bottom of the Pads page) lets you set device-wide default values for button appearance. Any button on any pad that doesn't have an explicit override inherits from these defaults.

**Available defaults:**

| Setting | Description |
|---------|-------------|
| **Background color** | Default button fill color |
| **Text color** | Default label/icon color |
| **Border color** | Default button outline color |
| **Border width** | Default border thickness (px) |
| **Corner radius** | Default button corner rounding (px) |
| **Label top/center/bottom style** | Default label style DSL (e.g., `font_size:24;align:left`) |

The cascade order is: **Button field → Device button defaults → Firmware hardcoded default**. If a button has no explicit color set, the device default is used. If no device default is set either, the firmware default applies (dark gray background, white text, black border, no border, 8px radius).

> **Tip**: Set your button defaults first, then add buttons. Changing a default immediately updates all buttons that don't have a custom override — both in the editor preview and on the device.

When editing a button, fields that match the device default show their inherited value normally. If you change a field to a custom value, a small **↩** reset link appears next to the field label — click it to revert to the device default.

### Template Pad

The **Template Pad** dropdown (below Background Color) lets you inherit buttons from another pad. When you select a template pad:

- Buttons from the template pad fill **empty** grid positions on the current pad. Your own buttons always take priority — the template only fills gaps.
- Template buttons appear as **ghost overlays** in the editor (semi-transparent with a dashed outline) so you can see what will be inherited.
- The template pad's **bindings** are also merged in (your pad's own settings win on any conflict).
- **No chaining** — if the template pad itself references another template, that second-level reference is ignored. This prevents circular dependencies and keeps behavior predictable.
- The merge is **read-only** — template buttons are never written into your pad's JSON file. They're merged in-memory each time the pad loads.

> **Use case**: Put navigation buttons (Home, Settings, Back) on a "nav" pad. Then set that as the template for your other pads — the nav buttons automatically appear in empty positions without copying them one by one.

### Building Blocks

Building blocks are pre-configured groups of buttons that you can insert into a pad in one step. Instead of manually creating and configuring 3–6 related buttons, select a block from the **More ▾** menu and place it with a single click.

**How to use:**

1. Open the **More ▾** menu on the Pads page. Available blocks appear under the **━━ Blocks ━━** heading.
2. Click a block name to enter **placement mode**. A banner appears above the grid with a Cancel button.
3. Hover over empty cells to see a live ghost overlay — **green dashed outline** means the block fits, **red** means it overlaps existing buttons or exceeds the grid boundary.
4. Click a valid cell to place the block. The block's buttons (with their spans, labels, actions, bindings, and styles) are inserted at that position.
5. Press **Escape** or click **Cancel** to exit placement mode without placing anything.

**Availability checks** — a block only appears in the menu when the current pad meets its requirements:

- Grid dimensions are at least as large as the block's minimum (e.g., 3 columns × 2 rows for the Countdown Timer).
- The pad has enough free (empty) cells for the block's buttons.
- The total button count after insertion stays within the 64-button limit.

After placing a block, all its buttons become regular buttons — you can edit, move, or delete them individually just like any other button.

**Built-in blocks:**

| Block | Size | Description |
|-------|------|-------------|
| **Countdown Timer** | 3×2 min | Three rocker buttons (1 min, 10 sec, 1 sec) in the top row, plus a 2-column-span timer display with `font_family:segment` and a combined start/pause/reset button in the bottom row. Uses Timer 1. |

> **Tip**: Building blocks and template pads serve different purposes. Use **template pads** to share common buttons (like navigation) across many pads. Use **building blocks** to quickly add a self-contained functional group (like a timer control panel) to a specific pad.

---

## Moving Buttons

To rearrange your pad layout, **drag any configured button to an empty cell**. Click a button and move the pointer to start dragging — the button keeps its full configuration (labels, actions, colors, icon, widget). While dragging, the source cell dims and valid drop targets show a green dashed outline.

If the button has a multi-cell span (e.g. 2×2) and the target position has room, the span is preserved. Otherwise the button is placed as 1×1.

Drop targets must be empty — you cannot drop onto an occupied cell. To cancel a drag, release over an occupied cell or press **Escape**. The [copy and paste workflow](#copy-and-paste-buttons) is always available as an alternative, and is also the only option on mobile browsers where drag-and-drop is not supported.

---

## Resizing Buttons

Hover over any button to reveal thin drag handles on all four edges (right, left, bottom, top). Drag an edge outward to grow the button into adjacent empty cells, or drag inward to shrink it back down.

- **Snap behavior** — the resize snaps to cell boundaries early (~20% into the next cell) so there is no dead zone between sizes.
- **Occupancy validation** — you cannot resize into cells occupied by other buttons. The drag simply stops at the last valid size.
- **Grid bounds** — the button cannot grow beyond the grid edges. Shrinking stops at 1×1 (the minimum size).
- **Touch support** — drag handles work with both mouse and touch input. On touch devices, press and drag an edge to resize.
- **Cursor hints** — horizontal edges show a `col-resize` cursor, vertical edges show `row-resize`.

The resize handles appear only when the editor is in normal mode — they are hidden during [block placement](#building-blocks) and [drag-and-drop moves](#moving-buttons).

> **Tip**: Combine resizing with [column/row spans](#spans-multi-columnrow-buttons) for precise layouts. Drag handles are the fastest way to visually adjust span sizes without opening the button editor.

---

## The Button Editor

Click any button in the grid preview to open the button editor. This is where each button gets its personality — from static labels and icons to live MQTT data and conditional colors.

The editor is organized into collapsible sections. Let's walk through each one.

### Labels

Every button has three label slots: **Top**, **Center**, and **Bottom**. Each can hold static text, live data bindings, or a mix of both.

**Static text** is straightforward:

```
Kitchen
```

**Binding templates** pull in live data using `[scheme:params]` tokens:

```
[mqtt:home/solar;power;%.0f W]
```

This subscribes to the `home/solar` MQTT topic, extracts the `power` field from the JSON payload, and formats it as a rounded number with " W" appended. The result might show "3450 W" on the button.

**Mix them freely** — static text around tokens is preserved:

```
Solar: [mqtt:home/solar;power;%.0f] W
```

**Explicit line breaks** are supported in label inputs using `\n`:

```
Line 1\nLine 2
```

This is saved as a real newline and rendered as two lines on the device. When you reopen the button editor, it is shown again as `\n` so you can edit it predictably.

You can even put multiple bindings in a single label:

```
[mqtt:indoor;temp;%.1f]° / [mqtt:indoor;humidity;%.0f]%
```

This might display "22.3° / 45%". See the [Binding Templates](#binding-templates) chapter for full syntax.

> **Tip**: The button editor has a **?** button next to each label field that opens a built-in binding reference with examples.

### Label Style Overrides

Click the **Aa** button next to any label to reveal an advanced style input. This uses a compact `key:value;...` syntax to fine-tune how that specific label renders.

**Available properties:**

| Property | Values | What it does |
|----------|--------|-------------|
| `font_size` | `12`, `14`, `18`, `24`, `32`, `36`, `48` | Override the automatic font size |
| `font_family` | `dseg7` / `segment`, `bebas`, `doto` / `pixel` | Use an alternate display font (see below) |
| `font_upscale` | `1.0` to `2.0` (e.g. `1.2`, `1.4`, `2`) | Scale the current font size at runtime for hero text |
| `align` | `left`, `center`, `right` | Horizontal text alignment |
| `x` | `-999` to `999` | Shift the label left (negative) or right (positive) in pixels |
| `y` | `-999` to `999` | Shift the label up (negative) or down (positive) in pixels |
| `mode` | `clip`, `scroll`, `dot`, `wrap` | How to handle text that doesn't fit |
| `color` | `#RGB` or `#RRGGBB` | Override the label's text color |

Combine them with semicolons:

``` 
font_size:48;align:left;mode:dot
```

This renders a hero-sized left-aligned label that shows "..." when the text is too long.

A few more examples:

- `font_size:14;color:#FF0` — small yellow text
- `font_size:36;font_upscale:1.4` — extra-large hero text (combined scale)
- `font_family:dseg7;font_size:48` — 7-segment LCD style for sensor readings
- `font_family:bebas;font_size:48` — bold condensed headline
- `font_family:doto` — dot-matrix / pixel style
- `x:10;y:-4;align:right` — right-aligned, shifted right 10 px and up 4 px
- `font_size:24;mode:wrap` — medium text that wraps to multiple lines
- `color:#4CAF50` — green text (useful for status indicators)

> Without style overrides, font size is chosen automatically based on the grid dimensions and display resolution. The default alignment is center, and overflow is clipped.

#### Font Families

Three additional font families are available alongside the default Montserrat:

| Family | DSL value | Aliases | Style | Available sizes |
|--------|-----------|---------|-------|----------------|
| DSEG7 Classic Bold | `dseg7` | `segment` | 7-segment LCD display | 12, 14, 18, 24, 32, 36, 48 |
| Bebas Neue | `bebas` | — | Bold condensed display | 12, 14, 18, 24, 32, 36, 48 |
| Doto | `doto` | `pixel` | Dot-matrix / pixel | 12, 14, 18, 24, 32, 36, 48 |

All font families share the same set of sizes as the built-in Montserrat font. You can combine `font_family` with an explicit `font_size`:

```
font_family:dseg7;font_size:48
```

This is ideal for hero values like temperature readings, power measurements, or countdown timers where a specialized display font adds visual impact.

> **Note**: Custom fonts include digits 0–9, uppercase A–Z, lowercase a–z, and common symbols (`. , : ; - + / % °`). Characters outside this set fall back to Montserrat.

### Icons

Each button can display an icon above or alongside its labels.

**Icon types:**

- **None** — no icon, labels use the full button area
- **Emoji** — paste any emoji (☀️, 🔋, 🏠, 💡). Rendered in full color at the display's native resolution.
- **Material Symbol** — enter a Google Material Symbols icon name like `power_settings_new`, `thermostat`, or `lightbulb`. These are vector icons that get tinted with the button's text color.

Browse the full Material Symbols catalog at [fonts.google.com/icons](https://fonts.google.com/icons) — pick an icon and use its name (lowercase with underscores).

**Icon Size %** controls scaling. Leave at 0 for automatic sizing (recommended), or set 1–250 to force a specific percentage. When a bar chart widget is active, the icon automatically shares vertical space with the bar.

**Icon Position** controls where the icon appears relative to the center label:

- **Above label** (default) — icon stacked above the center label in a vertical layout
- **Left of label** — icon and center label side by side in a horizontal row
- **Centered** — icon centered on the button; the center label remains at its default position underneath

> **Note:** When a gauge, bar chart, or sparkline widget is active, the widget controls icon and label placement. The Icon Position dropdown is hidden in this case.

### Colors and Borders

The **Colors** section (collapsible) controls the button's appearance:

- **Background color** — the button fill
- **Text color** — applies to labels and Material Symbol icons
- **Border color** — the button outline

Each color field accepts either a static `#hex` value or a binding expression for dynamic colors — more on that in [Dynamic Colors](#dynamic-colors-with-bindings). Click the color swatch to open the color picker popover. Fields that support bindings show an **fx** badge above the swatch — type a binding expression (e.g. `[expr:...]`) directly into the picker input.

**Default color** is the fallback used while a binding hasn't resolved yet or if it returns an error. Set this to a sensible neutral color so buttons don't flash unexpectedly on startup.

**Border width** (0–10 px) and **corner radius** (0–50 px) let you fine-tune the look. A radius of 0 gives sharp corners; higher values create rounded buttons. When a button doesn't have an explicit value, it inherits from the device-level [Button Defaults](#button-defaults). If you set a custom value, a **↩** reset link appears next to the label — click it to revert to the inherited default.

**UI offset** nudges all button visuals using `x;y` pixels (for example `20;-10`). `+x` moves right, `-x` moves left, `+y` moves down, and `-y` moves up. This is optional and defaults to `0;0` when omitted.

### Button State (Conditional Visibility)

The **Button State** field controls whether a button is visible and interactive. It supports three values:

- **enabled** (default) — visible and tappable
- **disabled** — visible but grayed out and not tappable
- **hidden** — completely invisible, other buttons don't shift to fill the gap

This field supports binding templates, which makes it powerful for conditional UIs:

```
[expr:[mqtt:devices/printer;state]=="online"?"enabled":"hidden"]
```

This hides the printer control button when the printer is offline. Or:

```
[expr:[mqtt:home/alarm;armed]=="true"?"disabled":"enabled"]
```

This disables a door-lock button when the alarm is armed.

> If a binding hasn't resolved yet (shows `---`) or returns an error, the button defaults to **enabled** to avoid hiding buttons during startup.

### Spans (Multi-Column/Row Buttons)

**Column span** and **Row span** let a button occupy multiple grid cells. A button with col_span=2 takes up two columns; row_span=2 takes two rows. Use this for important display elements — a large clock button, a camera feed, or a prominent status indicator.

Spanned buttons automatically claim the grid cells they cover. Other buttons in those cells will be hidden behind the spanning button.

> **Tip**: The fastest way to adjust spans is to [drag the resize handles](#resizing-buttons) on the button edges in the grid preview. You can also set exact span values here in the button editor.

### Background Images and Camera Feeds

Any button can display an image or live camera stream fetched from a URL, rendered as the button background behind labels and icons.

| Setting | Description |
|---------|-------------|
| **Image or Stream URL** | HTTP or HTTPS URL to a JPEG, PNG, or MJPEG stream (`multipart/x-mixed-replace`) |
| **Auth User / Password** | HTTP Basic Auth credentials for protected sources |
| **Refresh Interval** | How often to re-fetch in milliseconds. `0` = fetch once (or stream continuously for MJPEG) |
| **Letterbox** | When enabled, the image fits inside the button with black bars. When off, the image covers the full button area (cropping if needed) |

**MJPEG streaming (recommended for cameras):**

When the URL serves a `multipart/x-mixed-replace` MJPEG stream, the device opens a single persistent TCP connection and reads frames as they arrive — no repeated TCP setup or per-frame server-side capture delay. Set **Refresh Interval to `0`** for continuous streaming.

Typical sources:
- **go2rtc** (built into Home Assistant): `http://homeassistant.local:1984/api/stream.mjpeg?src=camera_name`
- **Frigate**: `http://frigate.local:5000/api/front_door/latest.jpg` (snapshot) or check the Frigate MJPEG endpoint docs
- **VLC / ffmpeg relay**: any `multipart/x-mixed-replace` HTTP server wrapping an RTSP stream

**Snapshot mode (JPEG/PNG):**

For static images or cameras that only expose a snapshot endpoint, set the URL to the JPEG URL and configure a **Refresh Interval** (e.g. `5000` for 5 s polling).

**Common uses:**

- **Security cameras (MJPEG)**: Point at a go2rtc or Frigate MJPEG stream, set interval to `0`, get 8–15 fps live view.
- **Security cameras (snapshot)**: Set the URL to the camera's snapshot endpoint, add credentials, set interval to `5000`–`10000` ms.
- **Weather maps**: Fetch a radar image once per minute (interval: `60000`).
- **Album art**: Use a Home Assistant media player's entity picture URL.

> Images and streams are decoded in a background task and scaled to the button's pixel dimensions. On ESP32-P4, hardware JPEG decode and PPA scaling are used automatically for best performance. Keep source resolution reasonable — very large images increase PSRAM usage and decode time.

### Actions (Tap and Long-Press)

Each button supports up to **3 sequential actions** per gesture — one for **tap** and one for **long-press** (triggered after holding ~500ms). Actions execute in order: for example, action 1 publishes an MQTT message, action 2 plays a beep, and action 3 navigates to another screen.

By default, only the first action slot is shown. Click **"+ Add tap action"** or **"+ Add long-press action"** to reveal additional slots. Use the **"× Remove"** link to hide a slot and clear its action.

**Action types:**

| Type | What it does |
|------|-------------|
| **None** | No action (display-only button) |
| **Navigate to screen** | Jump to another pad or screen (e.g., `pad_1`, `info_screen`) |
| **Go back** | Return to the previous screen |
| **Publish MQTT** | Send a message to an MQTT topic. Topic and payload fields support binding templates (e.g. `[health:cpu]`). |
| **Send BLE Keys** | Send a BLE HID keystroke or key sequence to the paired host (see [BLE Key Sequences](#ble-key-sequences) below). The sequence field supports binding templates. ESP32-P4 boards only. |
| **Start BLE Pairing** | Clear the existing bond and open a 60-second pairing window. ESP32-P4 boards only. Remove the device from the old host's Bluetooth settings before re-pairing. |
| **Play Beep** | Play a beep pattern through the speaker. Specify a pattern (e.g. `1000:200 100 1000:200` for a double beep) and an optional volume override. The pattern field supports binding templates. ESP32-P4 boards only. |
| **Play Sound** | Play an uploaded MP3 sound file through the speaker. Select a file from the dropdown and optionally set a volume override. Upload sounds on the Home page under Audio &gt; Sound Files. ESP32-P4 boards only. |
| **Set Volume** | Set the device audio volume to an absolute value (0–100). The value field supports binding templates. Sub-option of System Command. ESP32-P4 boards only. |
| **Adjust Volume** | Step the device audio volume up or down by a signed delta (e.g. `10`, `-10`, or `{step}` for numeric rocker). The value field supports binding templates. Sub-option of System Command. ESP32-P4 boards only. |
| **Set Brightness** | Set the display backlight brightness to an absolute value (5–100). The value field supports binding templates. Sub-option of System Command. Session-only, resets on reboot. |
| **Adjust Brightness** | Step the display brightness up or down by a signed delta (e.g. `10`, `-10`, or `{step}`). The value field supports binding templates. Sub-option of System Command. Session-only, resets on reboot. |
| **Timer** | Control one of 3 independent timers — toggle, start, stop, pause, resume, reset, lap, set countdown, adjust countdown time, or set mode. Set and adjust countdown values support binding templates. See [Timer Actions](#timer-actions) below. |
| **Show Notification** | Display a floating message bubble on the screen. Configure text, duration, colors, opacity, font size, and location. All text and color fields support bindings. See [Notification Action](#notification-action) below. |
| **Home Assistant Service** | Call a Home Assistant service over the REST API (e.g. toggle a light, run a scene). Configure an entity ID, service, and optional service-data JSON. Requires the HA URL and token to be set on the **Home Assistant** portal page. See [Home Assistant Service Action](#home-assistant-service-action) below. |
| **System Command** | Trigger a device-level operation: **Reboot Device**, **Reconnect WiFi**, **Enable Screensaver**, **Set/Adjust Volume**, or **Set/Adjust Brightness**. |

**Example setup for a smart light:**
- **Tap action 1**: Publish MQTT → topic: `home/lights/kitchen/set`, payload: `toggle`
- **Tap action 2**: Play Beep → `1000:100` (confirmation chirp)
- **Long-press action**: Navigate to screen → `pad_3` (a dedicated lighting pad with brightness controls)

**Example setup for navigation:**
- **Tap action**: Navigate to screen → `pad_2` (cameras pad)
- Button label: "Cameras" with a `videocam` Material Symbol icon

### BLE Key Sequences

The **BLE Key** action sends keystrokes over Bluetooth to a paired host device. The sequence field accepts a compact DSL:

**Single keys:**
- `a`, `enter`, `tab`, `esc`, `space`, `backspace`, `delete`
- Arrow keys: `up`, `down`, `left`, `right`
- Function keys: `f1` through `f12`

**Modifier combos** — use `+` to combine modifiers:
- `ctrl+c` — copy
- `ctrl+shift+t` — reopen closed tab
- `gui+l` — lock workstation (Windows)
- `alt+f4` — close window

Available modifiers: `ctrl`, `shift`, `alt`, `gui` (Windows/Command key)

**Media/consumer keys:**
- `vol_up`, `vol_down`, `mute`
- `play_pause`, `next_track`, `prev_track`

**Text literals** — wrap in double quotes:
- `"Hello World"` — types the text character by character

**Multi-step sequences** — chain steps with spaces:
- `ctrl+a ctrl+c` — select all, then copy
- `"user@email.com" tab "password123" enter` — fill a login form

**Delays** — insert a pause (in ms):
- `ctrl+a 200ms ctrl+c` — select all, wait 200ms, then copy

> **Tip**: Assign `ble_pair` to a dedicated button so you can pair a new host device directly from the macropad's touch screen.

### Timer Actions

The **Timer** action type controls one of 3 independent on-device timers. Timers support count-up (stopwatch) and countdown modes. Use `[timer:N]` bindings on labels to display the timer value (see [Timer Binding](#timer-binding)).

Timer configuration (mode, countdown duration, expire actions) is set at the device level on the **Home** page under the **Timers** section. Button actions only control the timer at runtime.

When you select a Timer action, a dropdown groups all actions by timer:

| Action | Description |
|--------|-------------|
| **Toggle** | Stopped → start, running → pause, paused → resume |
| **Start** | Start the timer |
| **Stop** | Stop and reset to 0 (count-up) or the countdown preset (countdown) |
| **Pause** | Freeze the timer at its current value |
| **Resume** | Continue from the paused value |
| **Reset** | Reset to 0 or preset without changing the running state |
| **Lap** | Reset the timer and start fresh (useful for step timing) |
| **Set Countdown** | Set the countdown preset to an absolute number of seconds. Only affects countdown-mode timers |
| **Adjust** | Add or subtract seconds from the countdown preset (e.g., `15`, `-10`, or `{step}` for numeric rocker). Only affects countdown-mode timers |

#### Device-Level Timer Configuration

On the **Home** page, the **Timers** section lets you configure each timer:

- **Mode** — Count Up (stopwatch) or Countdown
- **Countdown Duration** — the starting value in seconds (countdown mode only)
- **Expire Actions** — up to 3 actions to execute when a countdown timer reaches zero. These use the same action editor as button actions, so you can play a sound, send an MQTT message, navigate to a screen, play a beep, or any combination:

| Example expire action | What happens |
|----------------------|-------------|
| Play Sound: `alarm` | Plays the "alarm" MP3 file |
| MQTT Publish: `home/timer/expired` → `ON` | Sends an MQTT notification |
| Navigate to screen: `pad_alarm` | Shows an alarm pad |
| Play Beep: `1000:300 200 1000:300` | Plays a beep pattern |

**Countdown overtime** — when a countdown timer reaches zero, it keeps running and displays negative values (e.g., "-0:05", "-1:23"). This lets you see how far past the target time you are. The `[timer:N_expired]` binding returns `ON` when the timer has crossed zero.

> **Tip**: Create a V60 coffee timer pad with a "Start" button, "+15s" and "-10s" adjust buttons, and a large display button showing `[timer:1;mm:ss]`. Configure Timer 1 as a 240-second countdown on the Home page, with an expire action that plays an alarm sound.

### Notification Action

The **Show Notification** action displays a floating message bubble on the device screen — useful for confirmations, alerts, or status messages triggered by button presses or automations.

**Fields:**

| Field | Description |
|-------|-------------|
| **Message** | The notification text. Supports binding templates (e.g., `Power: [mqtt:home/solar/power;$.power;%.0f]W`). Empty message dismisses the active notification. |
| **Duration (ms)** | How long the bubble stays visible. Default `3000` (3 seconds). Set to `0` for a persistent notification that stays until tapped. Supports bindings. |
| **Text Color** | Hex color for the message text (default `#ffffff`). Supports bindings. |
| **Background Color** | Hex color for the bubble background (default `#333333`). Supports bindings. |
| **Border Color** | Hex color for the bubble border. Leave empty for no border. Supports bindings. |
| **Opacity (%)** | Background opacity, 0–100 (default 85). |
| **Font Size** | Explicit font size (12/14/18/24/32/36/48). Leave at 0 for auto — uses the same scale-tier font as button center labels. |
| **Location** | Where the bubble appears: **Bottom** (default), **Center**, or **Top**. |

The bubble fades in over 200 ms, displays for the configured duration, then fades out. Tap anywhere on the bubble to dismiss it immediately. A new notification replaces the active one.

**Example: confirmation bubble on MQTT publish**
- **Tap action 1**: Publish MQTT → topic: `home/lights/toggle`, payload: `ON`
- **Tap action 2**: Show Notification → message: `Lights toggled!`, duration: `2000`

**Example: persistent alert from binding**
- **Tap action**: Show Notification → message: `Power: [mqtt:home/solar/power;$.power;%.0f]W`, duration: `0`, bg_color: `#1a3a1a`, location: `center`

> **Home Assistant integration**: Notifications can also be triggered remotely via the **Notify** text entity. See the [Home Assistant Integration Guide](ha-integration-guide.md#notifications) for details and automation examples.

### Home Assistant Service Action

The **Home Assistant Service** action calls a Home Assistant service directly over the REST API — for example, toggling a light, running a scene, or opening a cover — without routing through MQTT.

**Prerequisites**: On the portal's **Home Assistant** page, set the **Home Assistant URL** (base URL including scheme and port, e.g. `http://192.168.1.50:8123`) and a **Long-Lived Access Token** (created under your HA profile). HTTPS URLs are supported (the certificate is not verified). Leave the URL empty to disable service actions.

**Fields:**

| Field | Description |
|-------|-------------|
| **Entity ID** | The target entity, e.g. `light.living_room`. The service **domain** is derived automatically from the text before the first `.` (here, `light`). |
| **Service** | The service to call within that domain, e.g. `toggle`, `turn_on`, `turn_off`. |
| **Service Data (JSON)** | Optional. A JSON object merged into the request body alongside `entity_id`, e.g. `{"brightness_pct": 60}`. Leave empty for services that need no extra data. |

The action sends `POST <ha_url>/api/services/<domain>/<service>` with the access token as a bearer credential. The HTTP request runs on the main loop (not the render task), so the UI stays responsive. These fields are stored literally and do **not** support binding templates.

**Example: toggle a light**
- **Tap action**: Home Assistant Service → entity ID: `light.living_room`, service: `toggle`

**Example: set brightness on turn-on**
- **Tap action**: Home Assistant Service → entity ID: `light.kitchen`, service: `turn_on`, service data: `{"brightness_pct": 75}`

**Example: run a scene**
- **Tap action**: Home Assistant Service → entity ID: `scene.movie_night`, service: `turn_on`

> See the [Home Assistant Integration Guide](ha-integration-guide.md#service-actions-rest-api) for setup details and more examples.

### Audio Behavior

*Applies only to boards with audio hardware (ESP32-P4 boards with ES8311 codec).*

Buttons use the device-level beep patterns configured on the Home page. To play a custom sound on a specific button, add a **Play Beep** or **Play Sound** action — this automatically suppresses the device-level feedback beep to avoid overlapping audio.

**Behavior notes:**
- Buttons with no actions configured are completely inert — no visual tap flash and no audio cue. A button with no tap actions won't flash or beep on tap; a button with no long-press actions won't flash or beep on long-press.
- If any action in the sequence is a **Play Beep** or **Play Sound** action, the device-level feedback beep is automatically suppressed.
- When multiple actions are configured and one of them navigates to a different screen, any subsequent actions in the sequence still execute safely. The last navigation wins (the user sees the final target screen).
- Swipe gestures use the device-level tap beep with the same suppression logic.

---

## Widgets

Widgets replace the standard button rendering with specialized visualizations or interaction modes. Select the widget type in the button editor.

### Rocker

The rocker widget splits a button into two tap zones — tap the top half to trigger one set of actions, tap the bottom half to trigger another. This turns a single button into a directional control, ideal for brightness up/down, volume +/−, thermostat setpoints, or any value you want to nudge from one place.

Unlike the other widgets, the rocker doesn't visualize data. Instead, it changes how the button responds to taps.

**How it works:**

- The button area is divided into two equal zones along the selected axis.
- **Zone A** (top or left) dispatches the **Tap Action** set.
- **Zone B** (bottom or right) dispatches the **Long-Press Action** set.
- Small chevron indicators (▲▼ or ◄►) appear at the edges so the user knows the button is directional.
- The tap flash overlay covers only the tapped half for clear visual feedback.
- Both zones use the device's **Tap Beep** pattern (suppressed when the action itself produces audio).
- Long-press is disabled on rocker buttons since both action slots are used for the two zones.

> **Note:** The action labels in the button editor change contextually when a rocker widget is selected — "Tap Action" becomes "Up Action" (or "Left Action") and "Long-Press Action" becomes "Down Action" (or "Right Action").

**Configuration:**

| Setting | Description |
|---------|-------------|
| **Direction** | Vertical (up/down, default) or horizontal (left/right) |
| **Indicator Color** | Color of the chevron symbols (default white) |
| **Opacity** | Chevron visibility from 0 (invisible) to 255 (fully opaque). Default 80 (~31%) |

**Example — Brightness rocker:**

| Setting | Value |
|---------|-------|
| Widget | Rocker |
| Direction | Vertical |
| Center label | `☀️` or `[health:brightness]` |
| Up Action | System Command → Adjust Brightness → `10` |
| Down Action | System Command → Adjust Brightness → `-10` |

Labels, icons, and colors work alongside the rocker widget. A typical rocker button uses the center label for an icon or the current value, with top/bottom labels for context.

### Numeric Rocker

The numeric rocker widget splits a button into 4 tap zones for fine and coarse numeric adjustment — think `«  ‹  5:00  ›  »` where inner arrows adjust by ±1 and outer arrows adjust by ±10. This gives you a single button that handles both small nudges and large jumps, ideal for timers, counters, and sliders.

Unlike the regular rocker (which maps two zones to two separate action sets), the numeric rocker uses **one action template** with a `{step}` placeholder. The widget substitutes the correct signed step value at tap time, producing 4 distinct actions from a single configuration.

**How it works:**

- The button area is divided into 5 zones along the selected axis, with pixel-clamped widths that adapt to button size.
- **Horizontal mode**: left = decrement, right = increment.
- **Vertical mode**: bottom = decrement, top = increment (up = more).
- **Outer decrement** (far left / bottom) → `{step}` = `-large_step`
- **Inner decrement** → `{step}` = `-small_step`
- **Center zone** → works as a normal button (tap and long-press actions)
- **Inner increment** → `{step}` = `+small_step`
- **Outer increment** (far right / top) → `{step}` = `+large_step`
- Zone widths target 12% (outer) and 15% (inner) of the button span, clamped to 40–80 px. The center zone gets whatever remains.
- Double chevron indicators (`<<`/`>>` or `▲▲`/`▼▼`) mark the outer zones; single chevrons (`<`/`>` or `▲`/`▼`) mark the inner zones.
- The tap flash covers only the tapped zone.
- Inner zones (small step) use the device's **Tap Beep** pattern; outer zones (large step) use the **Long-Press Beep** pattern for a distinct audio cue. Suppressed when the adjustment action itself produces audio.
- The center zone supports full tap and long-press actions (all 3+3 action slots). Outer and inner zones use the dedicated **Adjustment Action**.
- The `{step}` placeholder is replaced in `mqtt_payload`, `key_sequence`, `volume_value`, `brightness_value`, and `timer_value` fields.

**Disabling zones:** Set a step value to **0** to disable that zone pair. The remaining zone expands to fill the freed space (from 15% to the full 27% per side). Setting both steps to 0 makes the entire button a center zone.

> **Tip:** For best usability, use `col_span >= 2` in horizontal mode or `row_span >= 2` in vertical mode so the tap zones are easy to hit.

**Configuration:**

| Setting | Description |
|---------|-------------|
| **Direction** | Horizontal (left/right, default) or vertical (up/down) |
| **Small Step** | Inner zone adjustment magnitude (default 1). Supports decimals (e.g. 0.1, 0.5). Set to 0 to disable inner zones |
| **Large Step** | Outer zone adjustment magnitude (default 10). Supports decimals. Set to 0 to disable outer zones |
| **Indicator Color** | Chevron color (default white) |
| **Opacity** | Chevron visibility from 0 (invisible) to 255 (fully opaque). Default 80 (~31%) |
| **Adjustment Action** | The action template dispatched for outer/inner zones. The `{step}` placeholder is replaced with the signed step value |

**Example — Countdown timer adjustment:**

| Setting | Value |
|---------|-------|
| Widget | Numeric Rocker |
| Direction | Horizontal |
| Col Span | 2 |
| Center label | `[timer:1;mm:ss]` |
| Adjustment Action | Type: `timer`, Timer: `1`, Command: `Adjust Countdown`, Value: `{step}` |
| Small Step | 1 |
| Large Step | 10 |

Tapping the inner-right zone sends an "Adjust Countdown" action with value `1` (add 1 second). Tapping the outer-left zone sends value `-10` (subtract 10 seconds). The center label shows the live timer value via the `[timer:1;mm:ss]` binding.

### List

The list widget renders a scrollable, tappable list of items inside a button. Unlike other widgets that visualize MQTT data, the list gets its items from a registered **data provider** — a pluggable module that supplies a set of labeled items at screen load time.

Tapping a list item dispatches the button's configured **Tap Actions** with `[list:provider_id.selected]` binding resolution — the binding engine resolves the selected item's ID at dispatch time. Long-pressing dispatches **Long-Press Actions** the same way. This makes the widget fully generic: the provider supplies data, the button's action configuration defines behavior.

**How it works:**

- The **Data Binding** field specifies the provider ID (not an MQTT topic). For example, `shutter_tests` or `brew_definitions`.
- On screen enter, the widget calls the provider to get the current list of items.
- Each item has an **ID** (used in binding resolution) and a **label** (displayed in the list).
- Tapping an item sets the selected ID for that provider, then dispatches the button's tap actions through the binding engine which resolves `[list:provider_id.selected]` to the item's ID.
- Long-pressing an item dispatches the button's long-press actions (with the same binding resolution).
- If no tap action is configured, tapping is a no-op.
- If the provider ID is not found (e.g., feature module not compiled), the widget shows "Source not found".
- If the provider returns 0 items, the widget shows "No items".

> **Note:** Tap and long-press events are handled by the list items themselves and do not propagate up to the button — only one of "tap an item" or "long-press an item" fires per interaction. The button's own tap/long-press handlers are not invoked separately when the list widget is active; the widget reuses the same action slots.

**Configuration:**

| Setting | Description |
|---------|-------------|
| **Data Binding** | The provider ID string (e.g., `shutter_tests`). Not an MQTT topic |
| **Filter** | Optional comma-separated rules that include or exclude items returned by the provider. Plain text — binding tokens like `[mqtt:...]` are **not** resolved here. Empty = all items. See syntax below |
| **Select Action** (tap) | The button's normal tap action(s), dispatched when a list item is tapped. Use `[list:provider_id.selected]` as the screen target to navigate to the selected item |
| **Long-Press Action** | Optional long-press action(s), dispatched on item long-press |

**Filter syntax:**

| Rule | Meaning |
|------|---------|
| *(empty)* or `*` | Match everything (default) |
| `pad_3` | Exact match against item ID or label (case-insensitive) |
| `pad_*` | Glob match against ID or label (`*` = any sequence, `?` = single char) |
| `*Home*` | Substring-style glob |
| `#5` | Item at index 5 (0-based position in provider's original list) |
| `#0-3` | Index range 0, 1, 2, 3 |
| `!pad_5` | Exclusion — negate any rule type with leading `!` |

An item passes if **any** positive rule matches **and no** exclusion rule matches. If only exclusion rules are given, items default to included. To list specific indices, use separate rules like `#0,#2,#4` (commas separate rules; `#` does not accept comma-lists).

**Examples:**
- `pad_*,!pad_5` — every `pad_` item except `pad_5`
- `#0-3` — first four items
- `*Home*,*Office*` — labels containing "Home" or "Office"
- `!pad_0` — all items except `pad_0`

The `[list:provider_id.selected]` binding token is scoped per provider — each list widget tracks its own selected item independently. The token is resolved in all action fields including `screen_id`, `mqtt_payload`, `key_sequence`, `volume_value`, `brightness_value`, and `timer_value`.

**Example — Select a test script:**

| Setting | Value |
|---------|-------|
| Widget | List |
| Data Binding | `shutter_tests` |
| Tap Action | Type: `mqtt`, Topic: `macropad/test/start`, Payload: `[list:shutter_tests.selected]` |

When the user taps "Full Range Test" (id: `full-range`), the widget dispatches an MQTT publish to `macropad/test/start` with payload `full-range`.

**Built-in providers:**

| Provider ID | Title | Description |
|-------------|-------|-------------|
| `pads` | Select Pad | Lists all configured pads with their custom names. Item IDs are `pad_0`, `pad_1`, etc. Useful for building a pad navigation menu |

**Example — Pad navigation list:**

| Setting | Value |
|---------|-------|
| Widget | List |
| Data Binding | `pads` |
| Tap Action | Type: `screen`, Screen: `[list:pads.selected]` |

Tapping "Pad 2: Home Assistant" (id: `pad_2`) navigates to that pad screen.

> **Tip:** The list refreshes its content each time the screen is entered. No reboot needed to pick up new items added by the provider.

### Bar Chart

The bar chart widget draws one or more vertical or horizontal bars that fill based on numeric values — perfect for power meters, CPU gauges, progress bars, tank levels, or side-by-side comparisons of related values.

**Configuration:**

| Setting | Description |
|---------|-------------|
| **Bar 1 data binding** | A binding template that resolves to a number (e.g., `[mqtt:solar/power;watts]`) |
| **Bar 2 / 3 / 4 data binding** | Optional. Each non-empty binding adds another bar to the widget (up to 4 total). Each bar gets its **own track** (background, gridlines, and target marker stay scoped to that bar — they no longer span the whole widget). The bars are spread evenly across the full width as columns (vertical) or stacked as rows (horizontal) with a fixed 6 px gap. All bars share the same min/max scale and zero-centered behavior |
| **Bar 1 / 2 / 3 / 4 caption** | Optional per-bar label, colored to match its bar and hidden when empty. Fully bindable, so it can mix static text with live data — e.g. `Solar [mqtt:home/solar;power;%.0f]W`. Use `\n` for a line break (multi-line captions). Shown **beneath** each bar (vertical, centered) or in a strip to the **left** of each row (horizontal, right-aligned). The strip is only reserved when at least one caption is set, so caption-less charts keep their full bar size. Captions use the small font and clip if too long — keep them short |
| **Caption size (px)** | Fixes the caption strip to an exact pixel size (1–200): **width** in horizontal mode, **height** in vertical mode. Leave at `0` for automatic sizing (horizontal ≈28% of the button width clamped 30–120 px; vertical one line tall). Increase the vertical size to fit multi-line (`\n`) captions |
| **Min / Max** | The value range. The bar is empty at min and full at max. Accepts a number or a binding expression (e.g. `[health:psram_total]`) for dynamic scaling |
| **Bar width %** | Each bar's thickness within its own column (1–100%), centered in the column. With a single bar this is the bar's width relative to the button, exactly as before; with multiple bars, lowering it thins each bar while keeping them spread across the full width. In horizontal mode it controls bar height instead |
| **Bar 1 color** | The fill color of the first bar. Supports binding expressions — use `[expr:threshold(...)]` for multi-zone coloring (see [Dynamic Colors](#dynamic-colors-with-bindings)). Default: green (`#4CAF50`) |
| **Bar 2 / 3 / 4 color** | Fill colors for the additional bars. Defaults: blue (`#2196F3`), purple (`#9C27B0`), orange (`#FF9800`). Each is bindable |
| **Bar background** | The color of the empty bar track. Supports binding expressions for dynamic color |
| **Orientation** | **Vertical** (default): bar fills bottom-to-top. **Horizontal**: bar fills left-to-right — ideal for progress bars or wide buttons |
| **Zero-Centered** | The fill grows from the zero point instead of the minimum — negative values grow down (or left in horizontal mode), positive values grow up (or right). Use with a negative minimum (e.g. min `-5000`, max `5000`) for signed values like net power flow |
| **Animation (ms)** | Duration of the ease-out transition when the bar value changes (0–5000 ms). Default: 300. Set to 0 for instant updates (no animation). The first value after screen load always snaps immediately |
| **Gridlines** | Number of evenly spaced scale lines drawn across the bar (0–20, 0 = none), with configurable width and bindable color |
| **Target Value** | A bindable value on the scale drawn as a marker line across the bar (e.g. a setpoint). Empty = no target |
| **Target Zone %** | A shaded band centered on the target, sized as a percentage of the min–max range (0 = no band). The marker line width and the marker/zone colors are configurable and bindable |

**Color by value** — to color the bar based on its current value, use a `threshold()` expression in the Bar color field. The color picker's built-in **Generate Color by Threshold** helper builds these expressions for you: pick your zone colors, set breakpoints, and the expression auto-generates as you type. For a solar panel with a 5 kW max:

```
[expr:threshold([mqtt:solar/power;watts], "#4CAF50", 1000, "#8BC34A", 3000, "#FF9800", 4500, "#F44336")]
```

Green below 1 kW, light green 1–3 kW, orange 3–4.5 kW, red above 4.5 kW.

Labels, icons, and colors still work alongside the widget. A typical bar chart button uses the top label for a title ("Solar") and the bottom label for the current value (`[mqtt:solar/power;watts;%.0f W]`).

### Gauge

The gauge widget draws an arc that fills based on a numeric value — ideal for clocks, speedometers, temperature dials, or any circular meter.

**Configuration:**

| Setting | Description |
|---------|-------------|
| **Data Binding (slot 1 / ring 1)** | Primary binding template for the outer ring (for example `[mqtt:sensor/temperature]` or `[time:%S]`) |
| **Data Binding (slot 2 / ring 2 or pair-1 negative)** | Optional. In normal mode this creates a second ring. In Dual Binding Pair 1 mode it becomes the negative direction for slot 1's ring |
| **Data Binding (slot 3 / ring 3 or pair-2 positive)** | Optional. In normal mode this creates a third ring. In Dual Binding Pair 2 mode it becomes the positive direction for the next combined ring |
| **Data Binding (slot 4 / ring 4 or pair-2 negative)** | Optional. In normal mode this creates a fourth ring. In Dual Binding Pair 2 mode it becomes the negative direction for slot 3's ring |
| **Start Label (slot 1)** | Optional text or binding shown at the start of slot 1's ring. Uses that ring's arc color and the same font size as the top/bottom labels |
| **Start Label (slot 2)** | Optional text or binding for slot 2. In dual mode, slot 2 still has its own color but shares the same physical ring as slot 1 |
| **Start Label (slot 3)** | Optional text or binding for slot 3 |
| **Start Label (slot 4)** | Optional text or binding for slot 4 when it renders as its own ring |
| **Min / Max** | The value range. The arc is empty at min and full at max. Accepts a number or a binding expression for dynamic scaling |
| **Arc Degrees** | Total sweep of the arc (10–360°). 180 = half circle, 270 = three-quarter, 359 = near-full circle |
| **Start Angle** | Where the arc begins in LVGL degrees (0° = 3 o'clock, 90° = 6 o'clock, 180° = 9 o'clock, 270° = 12 o'clock) |
| **Zero-Centered** | Arc fills from the zero point — negative values grow left, positive grow right. Only applicable to single mode rings. |
| **Dual Binding Pair 1** | Combines slots 1 and 2 into one ring: slot 1 fills the positive direction, slot 2 fills the negative direction |
| **Dual Binding Pair 2** | Combines slots 3 and 4 into one ring using the same positive/negative split |
| **Show needle** | Display a line from the center to the current value position on the arc |
| **Arc Width %** | Arc thickness as a percentage of the radius (5–50%) |
| **Tick Marks** | Number of interior tick marks (0 = none). N ticks divide the arc into N+1 equal segments |
| **Needle Width** | Line width in pixels (0 = hidden, max 10) |
| **Needle Cutoff** | Percentage of the needle length to remove from the center (0–99%). Use this to prevent the needle from overlapping a center label or icon. Default: 0 (full-length needle) |
| **Tick Width** | Tick line width in pixels (1–5) |
| **Arc Color** | Fill color for slot 1. Supports binding expressions — use `[expr:threshold(...)]` for multi-zone coloring (see [Dynamic Colors](#dynamic-colors-with-bindings)). Default: green (`#4CAF50`) |
| **Arc Color 2** | Fill color for slot 2, or the negative half of Dual Binding Pair 1. Default: blue (`#2196F3`) |
| **Arc Color 3** | Fill color for slot 3, or the positive half of Dual Binding Pair 2. Default: purple (`#9C27B0`) |
| **Arc Color 4** | Fill color for slot 4, or the negative half of Dual Binding Pair 2. Default: orange (`#FF9800`) |
| **Track Color** | Color of the unfilled arc background. Supports binding expressions for dynamic color |
| **Needle Color** | Color of the needle line. Supports binding expressions for dynamic color |
| **Tick Color** | Color of the tick marks. Supports binding expressions for dynamic color |
| **Target Value** | Bindable value on the scale that positions a target marker and optional zone across all active rings. Empty = no target. For example `[mqtt:hvac/setpoint;temperature]` |
| **Target Zone Angle** | Total zone width in degrees centered on the target value (0 = no zone, max 90). The zone is rendered as a semi-transparent overlay on all rings |
| **Target Tick Width** | Tick line width at the target value position (0 = no tick, 1–5 px) |
| **Target Marker Color** | Color of the target value tick. Supports binding expressions |
| **Target Zone Color** | Color of the zone overlay arc. Supports binding expressions |
| **Animation (ms)** | Duration of the ease-out transition when arc and needle values change (0–5000 ms). Default: 300. Set to 0 for instant updates (no animation). Applies to all rings and the needle. The first value after screen load always snaps immediately |

Each ring has its own arc color field, so rings can be independently colored or threshold-driven. Use `[expr:threshold(...)]` in any arc color field for value-based coloring — the color picker's built-in **Generate Color by Threshold** helper makes this easy.

The icon and center label are positioned inside the arc at the pivot point. A typical gauge button uses the center label for the numeric readout and the top label for a title.

**Multi-ring gauges** — fill in slots 2, 3, and 4 to add up to four concentric rings (Apple Health ring style). All active rings share the same min/max, arc degrees, and start angle, but each slot has its own arc color and optional start label. The needle is shown on the outermost active gauge ring only; tick marks and target markers are rendered on all active rings. The arc width percentage applies to each ring equally, with automatic gaps between them.

**Dual binding gauges** — enable Dual Binding Pair 1 and/or Pair 2 to collapse slot pairs into shared rings. In a dual pair, the first slot fills from zero toward the positive direction and the second slot fills from zero toward the negative direction. If the partner binding is empty or invalid, it is treated as `0`. Pair 1 also drives the needle using the signed difference `slot1 - slot2`.

**Power balance example** (house vs solar vs grid, in kW):
- Ring 1 data binding: house consumption (for example `[mqtt:home/house;power_kw]`)
- Ring 2 data binding: solar production (for example `[mqtt:home/solar;power_kw]`)
- Ring 3 data binding: grid power (for example `[mqtt:home/grid;power_kw]`) where positive = import, negative = injection
- Dual Binding Ring 1 and 2: enabled
- Min / Max: `-3` / `3`
- Zero-Centered: enabled
- Show needle: disabled

This visualization shows system balance at a glance: the outer combined ring contrasts house load against solar production, while the inner ring shows resulting grid exchange. Mental model: house load + solar (negative contribution on ring 2) should align with grid power on ring 3.

**Zero centered example** (grid power, -3 kW to +3 kW):
- Data binding: `[mqtt:grid/power;$.value]`, Min: -3, Max: 3
- Arc Degrees: 180, Start Angle: 180, Zero centered: on
- Negative values (import) fill leftward from center, positive values (export) fill rightward

**Clock example** (seconds hand on a full circle):
- Data binding: `[time:%S]`, Min: 0, Max: 60
- Arc Degrees: 359, Start Angle: 270 (12 o'clock)
- Tick Marks: 12, Center label: `[time:%H:%M]`

**Thermostat setpoint example** (temperature gauge with target zone):
- Data binding: `[mqtt:hvac/current_temp;temperature]`, Min: 15, Max: 30
- Arc Degrees: 270, Start Angle: 135
- Target Value: `[mqtt:hvac/setpoint;temperature]`
- Target Zone Angle: 4 (±2° around setpoint)
- Target Tick Width: 2, Target Marker Color: `#FFFFFF`, Target Zone Color: `#FF5722`
- Center label: `[mqtt:hvac/current_temp;temperature;%.1f]°`

### Sparkline

The sparkline widget draws a mini trend line showing how a value changes over time — perfect for temperature history, power consumption trends, network latency, or any metric you want to watch evolve.

**Configuration:**

| Setting | Description |
|---------|-------------|
| **Data binding (main line)** | A binding template that resolves to a number (e.g., `[mqtt:sensor/temp;temperature]`, `[health:cpu]`). Each line has a color swatch below the binding input |
| **Data binding (line 2/3)** | Optional extra bindings for overlaid lines. Each gets its own data stream and color. Leave empty for single line |
| **Y-Axis Min / Max** | The Y-axis range. Leave empty for auto-scaling based on observed data. Accepts a number or a binding expression (e.g. `[health:heap_total]`) for dynamic scaling |
| **Same scale for all lines** | When enabled (default), all lines in a multi-line sparkline share the same auto-scaled Y-axis range, so values are visually comparable. Disable to let each line auto-scale independently — useful when lines have very different magnitudes and you want to compare trends/shapes rather than absolute values. Has no effect when explicit min/max are configured or with single-line sparklines |
| **Time window** | How many seconds of history to display (default: 300 = 5 minutes) |
| **Data points** | Number of samples in the line (default: 60). More points = higher resolution but slightly more memory |
| **Line width** | Thickness of the trend line in pixels (1–10, default: 2) |
| **Line separation** | Per-line vertical offset in pixels (0 = off, 1–6). When non-zero and the widget has more than one line, each line is nudged by a small symmetric offset (e.g. two lines fan out by ±offset, three lines by −offset / 0 / +offset) so lines with near-identical values stay visually distinct instead of drawing on top of each other. The current-value dot and min/max markers move with their line. Note: this shifts lines a couple of pixels off their exact value, so leave it at 0 when precise pixel-accurate readout matters |
| **Max marker size** | Dot radius for the maximum-value marker (0 = off, 1–20 px). When non-zero, a dot and optional label are drawn at the highest point in the visible data |
| **Max format** | Printf format string for the max label (e.g., `hi %.1f`). Only one `%f`/`%e`/`%g` specifier allowed. Leave empty for dot only (no label) |
| **Max label color** | Override color for the max label text. White (#FFFFFF) means "auto" — the label inherits the line's current color |
| **Min marker size** | Same as max marker, but for the lowest point in the visible data |
| **Min format** | Printf format string for the min label (e.g., `lo %.1f`) |
| **Min label color** | Override color for the min label text. White = auto |
| **Current value dot** | Dot radius at the right edge of the chart showing the most recent value (0 = off, 1–20 px). Uses the line’s resolved color |
| **Label width (px)** | Width of the right-side label area in pixels (0 = 50px default, 1–200). Only takes effect when at least one current-value label is configured. The chart area shrinks to make room |
| **Line 1/2/3 Label** | Binding expression or static text for a per-line current-value label displayed in the right margin (e.g., `[mqtt:sensor/temp;temperature;%.1f] °C`). Labels track the Y position of the rightmost data point and inherit the line’s color. Collision avoidance pushes overlapping labels apart. Leave empty to disable |
| **Reference line 1/2/3** | Up to 3 horizontal reference lines at fixed Y values. Each has a Y value (numeric), a color, and a line pattern (Solid, Dotted, or Dashed). Drawn behind the data lines. Only lines with a valid Y value are rendered |
| **Keep reference lines in view** | When enabled, auto-scale expands the Y range to include all configured reference line values, so they are always visible. Data that exceeds the reference lines still expands the range normally. Only affects auto-scaled axes (explicit min/max take priority) |
| **Smoothing** | Gaussian kernel smoothing radius (0 = off, 1–8). Smooths the trend line by averaging neighboring data points — higher values produce a smoother curve. A value of 3–4 gives a pleasant smoothing effect; 8 gives heavy averaging. Min/max markers and current-value dot are positioned on the smoothed line. Set to 0 for raw data rendering |

**Background data collection** — unlike bar chart and gauge which only show the current value, sparklines need historical data. The data stream registry collects data continuously in the background, even when the sparkline's screen is not visible. When you navigate to a sparkline's screen, the graph is immediately populated with all collected history.

**Auto-scaling** — when min and max are left empty, the sparkline automatically scales the Y-axis to fit the observed data range. This is the recommended default for most use cases. With multiple lines and **Same scale for all lines** enabled (default), all lines share the same Y-axis range computed from the global min/max across all streams — so a value of 50 on line 1 and 50 on line 2 appear at the same height. Disable it if your lines have very different magnitudes (e.g., watts 0–5000 vs efficiency 0–100) and you want each to fill the chart independently.

**Data gaps** — if data stops arriving (e.g., MQTT sensor goes offline), the sparkline uses Last Observation Carried Forward (LOCF) to fill gaps, keeping the graph smooth instead of showing holes.

Labels, icons, and colors still work alongside the widget. A typical sparkline button uses the top label for a title ("Temperature") and the bottom label for the current value (`[mqtt:sensor/temp;temperature;%.1f°C]`).

**Temperature trend example:**
- Data binding: `[mqtt:home/sensor/living_room;temperature]`
- Min: 15, Max: 35, Time window: 600 (10 minutes), Slots: 60
- Top label: `Living Room`, Bottom label: `[mqtt:home/sensor/living_room;temperature;%.1f°C]`

**CPU usage sparkline:**
- Data binding: `[health:cpu]`
- Min: 0, Max: 100, Time window: 300 (5 minutes), Slots: 60
- Line color: `[expr:threshold([health:cpu], "#4CAF50", 50, "#FF9800", 80, "#F44336")]` (green→orange→red as CPU increases)

**Multi-line solar comparison:**
- Data binding: `[mqtt:home/solar/power;production]` (green line)
- Data binding (line 2): `[mqtt:home/solar/power;grid_import]` (blue line)
- Min: 0, Max: auto, Time window: 600 (10 minutes), Slots: 60
- Top label: `Solar`, Bottom label: `[mqtt:home/solar/power;production;%.0fW]`

### Table

The table widget renders multi-column row data from a structured binding payload.

**Configuration:**

| Setting | Description |
|---------|-------------|
| **Data binding** | Must resolve to a table schema payload. Use an exact single-token binding expression such as `[health:table]` or `[health:extended_table]` |
| **Font style override** | Optional label-style DSL for table text (for example `font_size:14` or `font_family:bebas`) |
| **Scroll** | Enable or disable table scrolling |

**Important:** Do not wrap the table binding in static text and do not add a format parameter. The table widget expects the resolved structured payload, not formatted text.

**Examples:**

- Standard table source: `[health:table]`
- Extended table source: `[health:extended_table]`
- Optional fallback during startup: `[health:table|{}]`

---

## Binding Templates

Binding templates are the engine behind live data on your buttons. They follow a simple pattern:

```
[scheme:parameters]
```

Static text before, after, or between tokens is preserved. If a binding can't resolve (topic not received yet, invalid path), it shows `---` as a placeholder. Errors show `ERR:reason`.

For bindings that return structured payloads (for example `health:table` and `health:extended_table`), use an exact single-token template (for example `[health:table]`) with no prefix/suffix text and no format parameter.

### Binding Validation

The pad editor and Home page validate binding syntax **in real time** as you type. Any field that accepts a binding expression (labels, colors, data bindings, widget parameters, MQTT topics, action values, wake binding, etc.) is checked automatically. In the action editor, fields that support bindings are marked with an **fx** badge.

**What gets validated:**

- **Bracket balance** — unclosed `[` or extra `]` characters
- **Scheme names** — unknown schemes are flagged, with a "did you mean?" suggestion for typos (e.g., `mqt` → `mqtt`)
- **Parameter counts** — too many or too few semicolon-delimited parameters for the scheme
- **Known keys** — health binding keys (e.g., `cpu`, `heap_free`, `rssi`, `table`, `extended_table`) and timer parameters (e.g., `1`, `2_state`, `3_expired`) are checked against the valid set
- **Format strings** — printf-style format specifiers like `%.0f`, `%d`, `%s` are validated for correct syntax
- **Expression syntax** — `[expr:]` bodies are checked for operator/operand sequencing errors (e.g., `[expr:1 +]` or `[expr:* 2]`)
- **Nested bindings** — bindings inside `[expr:]` are recursively validated (e.g., a typo in `[expr:[mqt:topic] * 2]` is caught)

**How it works:**

- Validation runs on a **400 ms debounce** while you type, so errors appear almost immediately without interrupting your typing flow
- Validation also runs **immediately on blur** (when you click or tab away from a field)
- Errors appear as a **red border** and a **red message below the field** describing the problem
- **Saving is blocked** while any field has a validation error — the save button shows which fields need attention

> **Tip:** Validation is purely syntactic — it checks that your binding is well-formed, not that the MQTT topic exists or that the data path returns a value. Runtime resolution issues still show `---` or `ERR:` on the device.

### Pipe Fallback

**Syntax:** `[scheme:params|fallback]`

Append `|value` to any binding to show a custom fallback instead of the default `---` placeholder.

#### Why this exists

Without a pipe fallback, unresolved bindings display `---`. That's fine for label text, but it causes problems in two common situations:

- **Boot / startup delay** — After a reboot, it takes a few seconds before WiFi connects, MQTT subscribes, and the first messages arrive. During this window every MQTT-bound label flashes `---`, which looks broken.
- **MQTT reconnect** — If the broker restarts or the network hiccups, retained values are cleared from the subscription store. Labels revert to `---` until new messages come in.
- **Color bindings** — A `---` fallback in a color field (background, text color) isn't a valid color and falls through to the firmware default. By providing a pipe fallback like `|#333333`, you control exactly what color appears during these gaps.
- **Expression bindings** — An `[expr:]` that depends on an MQTT value will fail to evaluate while the inner binding is unresolved. The pipe fallback provides a clean result instead of `ERR:` or `---`.

The pipe character (`|`) is parsed at the outermost bracket level only — pipes inside nested bindings (e.g., inside an `[expr:]`) are left alone.

**Examples:**

```
[mqtt:solar/power;watts|0]               → 0 until first MQTT message arrives
[mqtt:light/color;hex|#ffffff]           → #ffffff as default color
[health:cpu|--]                          → -- instead of --- placeholder
[pad:power|0]                            → 0 when named binding hasn't resolved
```

Pipe fallbacks work with **color bindings** too — set a sensible default color that shows until the binding resolves:

```
[expr:[mqtt:sensor;temp] > 30 ? "#FF4444" : "#4CAF50"|#333333]
```

This shows dark gray (`#333333`) during startup, then switches to red/green once the MQTT value arrives.

### MQTT Binding

**Syntax:** `[mqtt:topic;path;format]`

| Parameter | Required | Description |
|-----------|----------|-------------|
| **topic** | Yes | The MQTT topic to subscribe to |
| **path** | No | JSON key to extract. Use dot-notation for nested objects (`data.temp`). Omit or use `.` for raw payload |
| **format** | No | Printf format string for the value |

The device automatically subscribes to every topic it discovers across all 16 pads. Each unique topic is subscribed once, even if used by dozens of buttons.

**Practical examples:**

A temperature sensor that publishes `{"temperature": 22.5, "humidity": 45}` to `home/sensor/living_room`:

```
[mqtt:home/sensor/living_room;temperature;%.1f°C]     → 22.5°C
[mqtt:home/sensor/living_room;humidity;%.0f%%]         → 45%
```

A simple on/off state from `zigbee2mqtt/kitchen_light`:

```
[mqtt:zigbee2mqtt/kitchen_light;state]                 → ON
```

A Home Assistant energy sensor publishing raw watts to `home/solar/power`:

```
[mqtt:home/solar/power;;%.0f W]                        → 3450 W
```

Note the empty path (double semicolon `;;`) — this takes the raw payload without JSON extraction.

Multiple bindings in one label, showing indoor conditions:

```
[mqtt:sensor/indoor;temp;%.1f]° / [mqtt:sensor/indoor;hum;%.0f]%   → 22.3° / 45%
```

**Common printf formats:**
- `%d` — integer (22)
- `%.0f` — float rounded to integer (22)
- `%.1f` — one decimal (22.5)
- `%.2f` — two decimals (22.50)
- `%s` — string as-is

### Health Binding

**Syntax:** `[health:key;format]`

Displays real-time device diagnostics — useful for system monitoring buttons or debug pads.

| Key | Returns | Example value |
|-----|---------|---------------|
| `cpu` | CPU usage percentage | `42` |
| `rssi` | WiFi signal strength (dBm) | `-54` |
| `uptime` | Seconds since boot | `86400` |
| `chip` | SoC model name | `ESP32-S3` |
| `chip_rev` | Silicon revision number | `1` |
| `chip_cores` | Number of CPU cores | `2` |
| `cpu_freq` | CPU clock speed (MHz) | `240` |
| `flash_size` | Flash chip size (bytes) | `16777216` |
| `firmware` | Firmware version string | `1.10.0` |
| `board` | Board name used at build time | `esp32-4848S040` |
| `mac` | WiFi MAC address | `AA:BB:CC:DD:EE:FF` |
| `reset_reason` | Last reset cause | `Power On` |
| `heap_total` | Total heap size (bytes) | `8390520` |
| `heap_free` | Free heap memory (bytes) | `145320` |
| `heap_min` | Heap low-water mark (bytes) | `98000` |
| `heap_largest` | Largest free block (bytes) | `65536` |
| `heap_internal_total` | Total internal RAM (bytes) | `327680` |
| `heap_internal` | Free internal RAM (bytes) | `82000` |
| `heap_internal_used` | Used internal RAM (total − free) | `245680` |
| `psram_total` | Total PSRAM (bytes; 0 if absent) | `8388608` |
| `psram_free` | Free PSRAM (bytes) | `6291456` |
| `psram_used` | Used PSRAM (total − free) | `2097152` |
| `psram_min` | PSRAM low-water mark (bytes) | `4194304` |
| `psram_largest` | Largest free PSRAM block (bytes) | `4194304` |
| `wifi_connected` | WiFi connection status | `ON` / `OFF` |
| `wifi_ssid` | Connected network name | `MyNetwork` |
| `ip` | Device IP address | `192.168.1.42` |
| `hostname` | Device hostname | `macropad` |
| `table` | Structured table payload (standard schema) | `{"title":"Status","columns":[...],"rows":[...]}` |
| `extended_table` | Structured table payload (extended schema) | `{"title":"Status","columns":[...],"rows":[...],"styles":...}` |
| `ble_status` | Compact BLE status | `disabled`, `ready`, `pairing`, `connected`, `error` |
| `ble_name` | Current BLE keyboard name | `Kitchen Pad EEFF` |
| `ble_state` | Detailed BLE state | `disabled`, `pairing`, `connecting`, `secured`, `claimed`, ... |
| `ble_pairing` | BLE pairing mode active | `ON` / `OFF` |
| `ble_bonded` | Current connection is bonded | `ON` / `OFF` |
| `ble_encrypted` | Current connection is encrypted | `ON` / `OFF` |
| `ble_peer_addr` | Connected peer's Bluetooth address | `AA:BB:CC:DD:EE:FF` |
| `ble_peer_id_addr` | Connected peer's identity address | `AA:BB:CC:DD:EE:FF` |
| `brightness` | Current backlight brightness (0–100) | `75` |
| `volume` | Current audio volume (0–100) | `50` |

Values are cached for up to 2 seconds to keep the CPU impact low.

`table` and `extended_table` are intended for the Table widget data binding field. Use them as exact single-token templates (for example `[health:table]`) so the structured payload is passed through unchanged.

**BLE signal values:**

| Signal | Value | Meaning |
|--------|-------|---------|
| `ble_status` | `disabled` | BLE keyboard is turned off in runtime configuration |
| `ble_status` | `ready` | BLE keyboard is enabled and available, but not currently in pairing mode or in an active secured session |
| `ble_status` | `pairing` | BLE keyboard is in the 60-second pairing window and accepting a new owner |
| `ble_status` | `connected` | A host is connected and the BLE link is encrypted and usable for HID input |
| `ble_status` | `error` | BLE initialization failed or the stack is in a fault state |
| `ble_state` | `disabled` | BLE keyboard is turned off in runtime configuration |
| `ble_state` | `idle` | BLE stack is initialized but not actively advertising a user-relevant state |
| `ble_state` | `advertising` | BLE is advertising without an owner claim yet |
| `ble_state` | `pairing` | BLE is in pairing mode and waiting for a new host |
| `ble_state` | `connecting` | A host connection exists, but the secure usable HID session is not established yet |
| `ble_state` | `claimed` | BLE has an owner on record and is advertising for that owner to reconnect |
| `ble_state` | `secured` | BLE has an encrypted active connection |
| `ble_state` | `error` | BLE initialization failed or the stack is in a fault state |

**Examples:**

```
CPU: [health:cpu]%                                     → CPU: 42%
[health:heap_free;%d] bytes free                       → 145320 bytes free
WiFi: [health:rssi] dBm                               → WiFi: -54 dBm
[health:wifi_ssid]                                     → MyNetwork
[health:wifi_connected]                                → ON
[health:ip]                                            → 192.168.1.42
```

### Time Binding

**Syntax:** `[time:format;timezone]`

Displays the current date and time, synced via NTP. If the device hasn't synced yet (right after boot), the binding shows `--:--` until the first NTP response arrives.

**Format** uses standard [strftime](https://man7.org/linux/man-pages/man3/strftime.3.html) codes:

| Code | Result | Example |
|------|--------|---------|
| `%H:%M` | 24-hour time | `14:30` |
| `%I:%M %p` | 12-hour time with AM/PM | `02:30 PM` |
| `%H:%M:%S` | Time with seconds | `14:30:05` |
| `%d/%m/%Y` | Date (day/month/year) | `08/03/2026` |
| `%Y-%m-%d` | ISO date | `2026-03-08` |
| `%a` | Short weekday | `Sun` |
| `%A` | Full weekday | `Sunday` |
| `%b` | Short month | `Mar` |
| `%B` | Full month | `March` |

**Custom sub-second codes** (ESP32 Macropad extensions):

| Code | Resolution | Result |
|------|-----------|--------|
| `%ms` | 1 ms | `000`–`999` |
| `%cs` | 10 ms | `00`–`99` |
| `%ds` | 100 ms | `0`–`9` |
| `%ums` | — | Device uptime in milliseconds (no NTP needed) |

**Timezone** — use an Olson timezone name. Omit for UTC. Supported timezones:

<details>
<summary>Full timezone list (click to expand)</summary>

| Timezone | Region |
|----------|--------|
| `UTC` | Universal |
| `Europe/London` | UK |
| `Europe/Amsterdam` | Netherlands |
| `Europe/Berlin` | Germany |
| `Europe/Brussels` | Belgium |
| `Europe/Paris` | France |
| `Europe/Rome` | Italy |
| `Europe/Madrid` | Spain |
| `Europe/Zurich` | Switzerland |
| `Europe/Vienna` | Austria |
| `Europe/Stockholm` | Sweden |
| `Europe/Oslo` | Norway |
| `Europe/Copenhagen` | Denmark |
| `Europe/Warsaw` | Poland |
| `Europe/Helsinki` | Finland |
| `Europe/Athens` | Greece |
| `Europe/Bucharest` | Romania |
| `Europe/Istanbul` | Turkey |
| `Europe/Moscow` | Russia (MSK) |
| `America/New_York` | US Eastern |
| `America/Chicago` | US Central |
| `America/Denver` | US Mountain |
| `America/Los_Angeles` | US Pacific |
| `America/Anchorage` | Alaska |
| `America/Phoenix` | Arizona (no DST) |
| `America/Toronto` | Canada Eastern |
| `America/Vancouver` | Canada Pacific |
| `America/Sao_Paulo` | Brazil |
| `America/Argentina/Buenos_Aires` | Argentina |
| `America/Mexico_City` | Mexico |
| `Asia/Tokyo` | Japan |
| `Asia/Shanghai` | China |
| `Asia/Hong_Kong` | Hong Kong |
| `Asia/Singapore` | Singapore |
| `Asia/Seoul` | South Korea |
| `Asia/Kolkata` | India |
| `Asia/Dubai` | UAE |
| `Asia/Riyadh` | Saudi Arabia |
| `Asia/Bangkok` | Thailand |
| `Asia/Jakarta` | Indonesia |
| `Australia/Sydney` | Australia Eastern |
| `Australia/Melbourne` | Australia Eastern |
| `Australia/Perth` | Australia Western |
| `Pacific/Auckland` | New Zealand |
| `Pacific/Honolulu` | Hawaii |
| `Africa/Cairo` | Egypt |
| `Africa/Johannesburg` | South Africa |
| `Africa/Lagos` | Nigeria |

If your timezone isn't listed, you can provide a raw POSIX TZ string instead (e.g., `CST-8` for UTC+8).

</details>

**Examples:**

A simple clock for your local timezone:

```
[time:%H:%M;Europe/Amsterdam]                          → 15:30
```

A world clock setup across four buttons:

```
[time:%H:%M;America/New_York]                          → 09:30
[time:%H:%M;Europe/London]                             → 14:30
[time:%H:%M;Asia/Tokyo]                                → 23:30
[time:%H:%M;Australia/Sydney]                          → 01:30
```

A date label:

```
[time:%A, %B %d;Europe/Amsterdam]                      → Sunday, March 08
```

A precision timer with milliseconds:

```
[time:%H:%M:%S.%ms]                                    → 14:30:05.123
```

### Timer Binding

**Syntax:** `[timer:N]`, `[timer:N;format]`, `[timer:N_state]`, `[timer:N_expired]`, `[timer:N_mode]`

Displays the value or state of one of the 3 on-device timers. Timer N is 1, 2, or 3.

**Formats:**

| Format | Result | Example |
|--------|--------|---------|
| *(none)* (default) | Raw seconds with decisecond | `245.0` or `-5.3` |
| `mm:ss` | Minutes and seconds | `4:05` or `-0:12` |
| `hh:mm:ss` | Hours, minutes, seconds | `1:02:30` |
| `ss` | Total seconds | `245` |
| `mm:ss.d` | With deciseconds | `4:05.3` |

The numeric default makes `[timer:N]` usable as a data source for gauge, bar chart, and sparkline widgets. For human-readable display on labels, use `[timer:N;mm:ss]` or another named format.

**State keys:**

| Key | Returns | Values |
|-----|---------|--------|
| `N_state` | Timer state | `running`, `paused`, `stopped` |
| `N_expired` | Countdown expired? | `ON`, `OFF` |
| `N_mode` | Timer direction | `up`, `down` |

Countdown timers that run past zero show negative values (e.g., `-0:05`).

**Examples:**

```
[timer:1]                    → 245.0      (default: raw seconds)
[timer:1;mm:ss]              → 4:05       (minutes:seconds)
[timer:1;hh:mm:ss]           → 0:04:05
[timer:2;mm:ss.d]            → 3:22.7     (with deciseconds)
[timer:1;ss]                 → 245         (integer seconds)
[timer:1_state]              → running
[timer:1_expired]            → OFF
[timer:1_mode]               → down
```

Use `[timer:N_expired]` in expression bindings for conditional colors or text:

```
[expr:[timer:1_expired]=="ON" ? "#FF0000" : "#333333"]
```

### Expression Binding

**Syntax:** `[expr:expression;format]`

Expressions let you do math, comparisons, and conditional logic on binding results. Inner bindings are resolved first, then the expression is evaluated.

**Operators** (in order of precedence):

| Operators | Description | Example |
|-----------|-------------|---------|
| `( )` | Grouping | `(a + b) * c` |
| `-` `+` (unary) | Negation, positive | `-value` |
| `*` `/` `%` | Multiply, divide, modulo | `value * 1000` |
| `+` `-` | Add, subtract | `a + b` |
| `>` `<` `>=` `<=` `==` `!=` | Comparisons (return 1 or 0) | `temp > 30` |
| `? :` | Ternary (if/then/else) | `temp > 30 ? "Hot" : "OK"` |

**Built-in functions:**

| Function | Description | Example |
|----------|-------------|---------|
| `threshold(value, color0, t1, color1, ..., tN, colorN)` | Maps a numeric value to a color via ascending thresholds. Returns `color_i` where `value < t_(i+1)`, or the last color if value ≥ all thresholds. | `threshold(temp, "#4CAF50", 25, "#FF9800", 35, "#FF0000")` |

Ternary branches can return numbers or `"quoted strings"`.

**Practical examples:**

*Unit conversion* — show kilobytes instead of bytes:

```
[expr:[health:heap_free] / 1024;%.1f KB]               → 141.9 KB
```

*Calculated value* — net power from solar minus grid:

```
[expr:[mqtt:solar;power] - [mqtt:grid;power];%.0f W]   → 1200 W
```

*Conditional text* — threshold-based status:

```
[expr:[mqtt:sensor;temp] > 30 ? "HOT" : "OK"]          → OK
```

*Combining MQTT topics*:

```
[expr:[mqtt:solar;power] + [mqtt:battery;power];%.0f W] → 4200 W
```

*Percentage calculation*:

```
[expr:[mqtt:tank;level] / [mqtt:tank;capacity] * 100;%.0f%%]  → 73%
```

If any inner binding hasn't resolved yet (shows `---`), the entire expression returns `---` — no partial evaluation happens.

### Dynamic Colors with Bindings

Color fields throughout the button editor accept binding expressions, making buttons and widgets change color based on live data. This includes:

- **Button colors** — background, text, and border
- **Widget colors** — bar chart bar color, gauge arc/track/needle/tick colors, sparkline line colors, reference line colors, min/max marker colors

All color bindings update live every display cycle — you don't need new data to arrive for a color change to take effect.

**Basic pattern** — change color based on a threshold:

```
[expr:[mqtt:sensor;temp] > 30 ? "#FF4444" : "#4CAF50"]
```

The button turns red when the temperature exceeds 30°C, green otherwise.

**Multi-tier colors** with nested ternaries:

```
[expr:[mqtt:sensor;temp] > 35 ? "#FF0000" : [mqtt:sensor;temp] > 28 ? "#FF9800" : "#4CAF50"]
```

Red above 35°C, orange above 28°C, green otherwise.

**Multi-tier colors** with `threshold()` — cleaner alternative to nested ternaries:

```
[expr:threshold([mqtt:sensor;temp], "#4CAF50", 28, "#FF9800", 35, "#FF0000")]
```

Same result: green below 28°C, orange 28–35°C, red above 35°C. Add as many thresholds as needed:

```
[expr:threshold([mqtt:aqi;value], "#4CAF50", 50, "#FFEB3B", 100, "#FF9800", 150, "#FF5722", 200, "#9C27B0", 300, "#7E0023")]
```

Six color zones for an AQI indicator — much cleaner than five levels of nested ternaries.

**State-based colors** — match string values:

```
[expr:[mqtt:device;state] == "ON" ? "#4CAF50" : "#666666"]
```

Green when on, gray when off.

**Blinking effect** using time bindings:

```
[expr:[time:%ds] % 2 == 0 ? "#FF0000" : "#330000"]
```

Toggles between bright red and dark red every second — useful for alert buttons.

> Always set a sensible **default color** when using color bindings. The default is shown while the binding resolves (during startup or reconnection). Otherwise you'll see black or white flashes.

### Pad Bindings (Named Data Sources)

**Syntax:** `[pad:name]` or `[pad:name;format]`

Pad bindings let you define a data source once at the pad level and reference it across all buttons and widgets on that pad. This avoids repeating the same MQTT topic everywhere and makes it easy to switch data sources — change one binding instead of editing every button.

**Defining bindings** — in the pad JSON config, add a `"bindings"` object at the pad level:

```json
{
  "bindings": {
    "power": "[mqtt:home/solar/power;$.value]",
    "current": "[mqtt:home/solar/current;$.amps]",
    "power_kw": "[expr:[mqtt:home/solar/power;$.value]/1000]"
  },
  "buttons": [...]
}
```

**Using bindings** — reference them anywhere you'd normally write a binding:

```
[pad:power]                                   → 3842.5 (raw value)
[pad:power;%.0f]                              → 3843 (formatted)
Power: [pad:power;%.0f] W                     → Power: 3843 W
[pad:power_kw;%.2f] kW                        → 3.84 kW
```

The optional `;format` parameter applies a printf format to the resolved value, just like other bindings.

**Inside expressions** — `[pad:]` tokens work naturally inside `[expr:]`:

```
[expr:[pad:power] > 3000 ? "High" : "Low"]   → High
[expr:[pad:power] > 3000 ? "#00AA00" : "#333"] → #00AA00
```

**In widget data bindings** — use `[pad:power]` as a widget's data binding, sparkline source, or gauge ring binding.

**Naming rules**: Binding names must start with a letter and contain only letters, digits, and underscores (e.g., `power`, `solar_current`, `temp1`). Maximum 31 characters.

**Limit**: Up to 16 named bindings per pad.

**Why use pad bindings?**

Consider a solar monitoring button with these fields all referencing the same topic:

| Field | Without pad bindings | With pad bindings |
|-------|---------------------|-------------------|
| Bottom label | `[mqtt:solar/power;$.value;%.0f W]` | `[pad:power;%.0f W]` |
| Background color | `[expr:[mqtt:solar/power;$.value]>3000?"#0A0":"#333"]` | `[expr:[pad:power]>3000?"#0A0":"#333"]` |
| Widget data | `[mqtt:solar/power;$.value]` | `[pad:power]` |

Switching from solar to grid power? Without pad bindings: 3+ edits per button. With pad bindings: change one line in `"bindings"`.

---

## Pad Actions (Bulk Operations)

The **More ▾** dropdown above the grid preview provides shortcuts for working with entire pads.

### Copy and Paste Buttons

In the button editor dialog, **Copy** saves the current button's settings to a clipboard — the editor stays open so you can keep editing. **Paste** applies the clipboard to the button you're editing and keeps the editor open so you can review or tweak the result. Column and row span values are preserved in the clipboard and applied on a best-effort basis: if the span fits at the target position (within grid bounds and no overlap with existing buttons) it is applied, otherwise it falls back to 1×1.

This is the fastest way to create multiple similar buttons — configure one, copy it, then paste into other positions and adjust the differences.

### Fill Pad

After copying a button, **Fill Pad** applies it to every position in the grid (column/row spans are stripped — every cell gets a 1×1 button). Useful for quickly populating a pad with a template button that you then customize individually.

### Copy / Paste Pad

**Copy Pad** saves the entire pad layout (grid size, name, all buttons, background color). **Paste Pad** overwrites the current pad with the clipboard. Use this to duplicate a pad layout to a different pad number and then make adjustments.

### Export / Import Pad

**Export Pad** downloads the current pad as a JSON file. **Import Pad** loads a pad from a JSON file. Use these to share pad designs between devices or archive your work.

### Export / Import Device Config

**Export Device Config** downloads everything — all 16 pads plus all device settings (network, MQTT, display, operating mode) — as a single JSON file. **Import Device Config** restores from that file, triggering a reboot.

This is your backup and migration tool. Export regularly, and use import to clone a setup to a new device.

### Clear Pad

Removes all buttons from the current pad, leaving an empty grid.

---

## Real-World Examples

These are complete, copy-paste-ready configurations for common use cases. Each example describes the pad layout and the key button settings.

### Home Energy Dashboard

*Monitor solar production, grid import/export, and battery in real time.*

**Pad settings**: 4 columns × 2 rows, name "Energy", background `#111111`.

| Button | Position | Top label | Bottom label | Style | Widget |
|--------|----------|-----------|--------------|-------|--------|
| Solar | col 0 | `Solar` | `[mqtt:home/solar;power;%.0f W]` | `font:14` on top | Bar chart, max: 5000, thresholds: 1000/3000/4500 |
| Grid | col 1 | `Grid` | `[mqtt:home/grid;power;%.0f W]` | `font:14` on top | Bar chart, max: 5000, use absolute value on |
| Battery | col 2 | `Battery` | `[mqtt:home/battery;soc;%.0f%%]` | `font:14` on top | Bar chart, max: 100, thresholds: 20/50/80 |
| Net | col 3 | `Net` | `[expr:[mqtt:home/solar;power]-[mqtt:home/grid;power];%.0f W]` | `font:14` on top | Bar chart, max: 5000 |

**Bar chart data bindings:**
- Solar: `[mqtt:home/solar;power]`
- Grid: `[mqtt:home/grid;power]`
- Battery: `[mqtt:home/battery;soc]`
- Net: `[expr:[mqtt:home/solar;power]-[mqtt:home/grid;power]]`

**Dynamic background colors on the Grid button:**
```
[expr:[mqtt:home/grid;power]<0?"#1B5E20":"#B71C1C"]
```
Green when exporting (negative = feeding back), dark red when importing.

### Smart Home Light Controls

*A 3×2 pad to control room lights with tap to toggle and long-press to navigate to per-room detail pads.*

**Pad settings**: 3 columns × 2 rows, name "Lights".

| Button | Labels | Icon | Tap action | Long-press |
|--------|--------|------|------------|------------|
| Kitchen | Center: `Kitchen` | 💡 | MQTT → `zigbee2mqtt/kitchen_light/set` / `{"state":"TOGGLE"}` | Navigate → `pad_3` |
| Living Room | Center: `Living Room` | 💡 | MQTT → `zigbee2mqtt/living_room/set` / `{"state":"TOGGLE"}` | Navigate → `pad_3` |
| Bedroom | Center: `Bedroom` | 💡 | MQTT → `zigbee2mqtt/bedroom_light/set` / `{"state":"TOGGLE"}` | Navigate → `pad_4` |
| Hallway | Center: `Hallway` | `lightbulb` (Material) | MQTT → `zigbee2mqtt/hallway/set` / `{"state":"TOGGLE"}` | — |
| Porch | Center: `Porch` | `outdoor_garden` (Material) | MQTT → `zigbee2mqtt/porch/set` / `{"state":"TOGGLE"}` | — |
| All Off | Center: `All Off` | `power_settings_new` (Material) | MQTT → `home/lights/all/set` / `OFF` | — |

**Dynamic colors** — each light button changes color based on its state:

Background color:
```
[expr:[mqtt:zigbee2mqtt/kitchen_light;state]=="ON"?"#FF8F00":"#333333"]
```
Amber when on, dark gray when off.

### Security Camera Grid

*A 2×2 pad showing live camera snapshots, tap to cycle between cameras.*

**Pad settings**: 2 columns × 2 rows, name "Cameras", background `#000000`.

| Button | Image or Stream URL | Auth | Refresh | Letterbox |
|--------|---------------------|------|---------|-----------|
| Front Door | `http://ha.local:1984/api/stream.mjpeg?src=front_door` | — | 0 (stream) | Off (cover) |
| Backyard | `http://ha.local:1984/api/stream.mjpeg?src=backyard` | — | 0 (stream) | Off (cover) |
| Garage | `http://192.168.1.52/snap.cgi` | admin / password | 10000 ms | Off (cover) |
| Driveway | `http://192.168.1.53/snap.cgi` | admin / password | 5000 ms | Off (cover) |

Each button uses the **top label** with a small font for the camera name:
```
Front Door
```
Style: `font:14;color:#FFF;align:left`

### World Clock

*A 1×4 pad showing time across four cities.*

**Pad settings**: 1 column × 4 rows, name "Clock", background `#1A1A1A`.

| Button | Top label (style: `font:14;color:#888`) | Center label (style: `font:36`) | Bottom label (style: `font:14;color:#888`) |
|--------|---------|--------------|--------------|
| New York | `New York` | `[time:%I:%M %p;America/New_York]` | `[time:%a %d %b;America/New_York]` |
| London | `London` | `[time:%H:%M;Europe/London]` | `[time:%a %d %b;Europe/London]` |
| Tokyo | `Tokyo` | `[time:%H:%M;Asia/Tokyo]` | `[time:%a %d %b;Asia/Tokyo]` |
| Sydney | `Sydney` | `[time:%H:%M;Australia/Sydney]` | `[time:%a %d %b;Australia/Sydney]` |

### Server Monitor

*A 2×3 pad tracking device health and a remote server.*

**Pad settings**: 2 columns × 3 rows, name "System".

| Button | Center label | Bottom label | Notes |
|--------|-------------|--------------|-------|
| CPU | `CPU` | `[health:cpu]%` (style: `font:36`) | Background: `[expr:[health:cpu]>80?"#B71C1C":"#1B5E20"]` |
| Memory | `Memory` | `[expr:[health:heap_free]/1024;%.0f KB]` (style: `font:24`) | Background: `[expr:[health:heap_free]<50000?"#FF6F00":"#1B5E20"]` |
| WiFi | `WiFi` | `[health:rssi] dBm` (style: `font:24`) | Background: `[expr:[health:rssi]<-75?"#FF6F00":"#1B5E20"]` |
| IP | `[health:ip]` | `[health:hostname]` | Static green background |
| Uptime | `Uptime` | `[health:uptime;%d]s` (style: `font:24`) | — |
| Server | `Server` | `[mqtt:server/status;state]` | Button state: `[expr:[mqtt:server/status;state]=="offline"?"disabled":"enabled"]` |

### Climate Control

*A 3×2 pad for HVAC monitoring and control via Home Assistant MQTT.*

**Pad settings**: 3 columns × 2 rows, name "Climate".

| Button | Center label | Color binding | Action |
|--------|-------------|---------------|--------|
| Living Room | `[mqtt:climate/living;current_temperature;%.1f°C]` (style: `font:32`) | Bg: `[expr:[mqtt:climate/living;current_temperature]>25?"#FF5722":[mqtt:climate/living;current_temperature]<18?"#2196F3":"#4CAF50"]` | — |
| Thermostat ▲ | `▲` (style: `font:36`) | Static `#1B5E20` | MQTT → `climate/living/set` / `{"temperature_up":1}` |
| Thermostat ▼ | `▼` (style: `font:36`) | Static `#B71C1C` | MQTT → `climate/living/set` / `{"temperature_down":1}` |
| Target | `Target` | — | — |
| — | `[mqtt:climate/living;temperature;%.1f°C]` (style: `font:32`) | — | — |
| Mode | `[mqtt:climate/living;mode]` (style: `font:18`) | Bg: `[expr:[mqtt:climate/living;mode]=="heat"?"#FF5722":[mqtt:climate/living;mode]=="cool"?"#2196F3":"#666"]` | MQTT → `climate/living/mode/set` / `auto` |

---

## Tips and Troubleshooting

**Binding shows `---`**
The MQTT topic hasn't received a message yet. Check that the topic is correct and that your MQTT broker is receiving data. You can verify topics using a tool like MQTT Explorer.

**Binding shows `ERR:too big`**
The MQTT payload exceeds 2 KB. Extract a smaller field using a JSON path instead of using the full payload.

**Expression shows `ERR:div/0`**
A division or modulo by zero. Add a ternary guard: `[expr:[mqtt:a;val]!=0 ? [mqtt:b;val]/[mqtt:a;val] : 0]`

**Text overflows the button**
Use a label style override: `mode:dot` adds "..." at the end, `mode:scroll` scrolls the text horizontally, or `mode:wrap` wraps to multiple lines.

**Dynamic color not changing**
Make sure the expression returns a quoted hex string like `"#FF0000"`, not just a number. Also check that you've set a **default color** — without one, the button may appear black or white before the binding resolves.

**Icon too large or too small**
Set `Icon Size %` to a specific value (e.g., 50 for half size, 150 for 150%). Leave at 0 for automatic sizing.

**Camera images not loading**
Verify the URL works in a browser. If it requires auth, make sure username and password are filled in. Only HTTP Basic Auth is supported. HTTPS works but the device doesn't validate certificates.

**Time shows `--:--`**
NTP hasn't synced yet. This usually resolves within a few seconds of connecting to WiFi. If it persists, check that UDP port 123 isn't blocked on your network.

**Buttons feel laggy or display FPS drops**
Too many binding updates or high-resolution background images can increase CPU load. Reduce camera refresh intervals, simplify expressions, or use fewer background images.

**Backing up your work**
Use **More ▾ → Export Device Config** regularly. It captures *everything* — all 16 pads, network settings, display config, and button layouts — in a single JSON file. If you ever factory reset or set up a new device, **Import Device Config** restores it all.
