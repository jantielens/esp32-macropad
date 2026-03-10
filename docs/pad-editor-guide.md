# Pad Editor Guide

The pad editor is the heart of ESP32 Macropad — it turns your touch screen into a fully custom dashboard, remote control, or status panel. Each device supports up to **8 independent pads**, each with its own grid of buttons that can display live data, control smart home devices, and react to real-time conditions.

You'll find the pad editor on the **Pads** page of the web portal (Full mode only). If you haven't connected your device to WiFi yet, complete the [first-time setup](first-time-setup.md) first.

---

## Designing a Pad

A pad is a page of buttons arranged in a grid. You can think of each pad as a separate screen on your device — swipe or navigate between them using button actions.

### Pad Settings

At the top of the pad editor, you configure the page itself:

- **Pad selection** — switch between Pad 1 through 8. Each pad is saved independently.
- **Pad Name** — an optional label shown in Home Assistant and on-device. For example, "Solar", "Lights", or "Cameras".
- **Columns / Rows** — the grid size, from 1×1 up to 8×8. A 3×2 grid gives you 6 large buttons; a 4×4 grid gives you 16 smaller ones.
- **Wake Screen** — when the screensaver wakes up, which screen should appear? Leave empty to return to the last active screen, or pick a specific pad.
- **Background** — the color behind the grid. Accepts a `#hex` color or a binding expression for dynamic backgrounds.

> **Example**: A home energy dashboard might use a 4×2 grid named "Energy" with a dark background (`#111111`) — four columns for solar, grid, battery, and net power, with two rows for the bar chart and its label.

---

## The Button Editor

Click any cell in the grid preview to open the button editor. This is where each button gets its personality — from static labels and icons to live MQTT data and conditional colors.

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
| `font` | `12`, `14`, `18`, `24`, `32`, `36` | Override the automatic font size |
| `align` | `left`, `center`, `right` | Horizontal text alignment |
| `y` | `-128` to `127` | Shift the label up (negative) or down (positive) in pixels |
| `mode` | `clip`, `scroll`, `dot`, `wrap` | How to handle text that doesn't fit |
| `color` | `#RGB` or `#RRGGBB` | Override the label's text color |

Combine them with semicolons:

```
font:36;align:left;mode:dot
```

This renders a large left-aligned label that shows "..." when the text is too long.

A few more examples:

- `font:14;color:#FF0` — small yellow text
- `y:-4;align:right` — right-aligned and nudged up 4 pixels
- `font:24;mode:wrap` — medium text that wraps to multiple lines
- `color:#4CAF50` — green text (useful for status indicators)

> Without style overrides, font size is chosen automatically based on the grid dimensions and display resolution. The default alignment is center, and overflow is clipped.

### Icons

Each button can display an icon above or alongside its labels.

**Icon types:**

- **None** — no icon, labels use the full button area
- **Emoji** — paste any emoji (☀️, 🔋, 🏠, 💡). Rendered in full color at the display's native resolution.
- **Material Symbol** — enter a Google Material Symbols icon name like `power_settings_new`, `thermostat`, or `lightbulb`. These are vector icons that get tinted with the button's text color.

Browse the full Material Symbols catalog at [fonts.google.com/icons](https://fonts.google.com/icons) — pick an icon and use its name (lowercase with underscores).

**Icon Size %** controls scaling. Leave at 0 for automatic sizing (recommended), or set 1–250 to force a specific percentage. When a bar chart widget is active, the icon automatically shares vertical space with the bar.

### Colors and Borders

The **Colors** section (collapsible) controls the button's appearance:

- **Background color** — the button fill
- **Text color** — applies to labels and Material Symbol icons
- **Border color** — the button outline

Each color field accepts either a static `#hex` value or a binding expression for dynamic colors — more on that in [Dynamic Colors](#dynamic-colors-with-bindings).

**Default color** is the fallback used while a binding hasn't resolved yet or if it returns an error. Set this to a sensible neutral color so buttons don't flash unexpectedly on startup.

**Border width** (0–10 px) and **corner radius** (0–50 px) let you fine-tune the look. A radius of 0 gives sharp corners; higher values create rounded buttons.

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

### Spans (Multi-Cell Buttons)

**Column span** and **Row span** let a button occupy multiple grid cells. A button with col_span=2 takes up two columns; row_span=2 takes two rows. Use this for important display elements — a large clock button, a camera feed, or a prominent status indicator.

Spanned buttons automatically claim the grid cells they cover. Other buttons in those cells will be hidden behind the spanning button.

### Background Images

Any button can display an image fetched from a URL, rendered as the button background behind labels and icons.

| Setting | Description |
|---------|-------------|
| **Image URL** | HTTP or HTTPS URL to a JPEG or PNG image |
| **Auth User / Password** | HTTP Basic Auth credentials for protected images |
| **Refresh Interval** | How often to re-fetch in milliseconds. `0` = fetch once and keep |
| **Letterbox** | When enabled, the image fits inside the button with black bars. When off, the image covers the full button area (cropping if needed) |

**Common uses:**

- **Security cameras**: Set the URL to your camera's snapshot endpoint, add credentials, and set a refresh interval of 5000–10000 ms for a near-live view.
- **Weather maps**: Fetch a radar image once per minute (interval: 60000).
- **Album art**: Use a Home Assistant media player's entity picture URL.

> Images are decoded in a background task and scaled to the button's pixel dimensions using bilinear filtering. Keep image resolution reasonable — the device fetches, decodes, and scales in PSRAM. A few camera tiles at 480×320 work fine; avoid huge 4K images.

### Actions (Tap and Long-Press)

Each button has two action slots — one for **tap** and one for **long-press** (triggered after holding ~500ms).

**Action types:**

| Type | What it does |
|------|-------------|
| **None** | No action (display-only button) |
| **Navigate to screen** | Jump to another pad or screen (e.g., `pad_1`, `info_screen`) |
| **Go back** | Return to the previous screen |
| **Publish MQTT** | Send a message to an MQTT topic |

**Example setup for a smart light:**
- **Tap action**: Publish MQTT → topic: `home/lights/kitchen/set`, payload: `toggle`
- **Long-press action**: Navigate to screen → `pad_3` (a dedicated lighting pad with brightness controls)

**Example setup for navigation:**
- **Tap action**: Navigate to screen → `pad_2` (cameras pad)
- Button label: "Cameras" with a `videocam` Material Symbol icon

---

## Widgets

Widgets replace the standard button rendering with specialized visualizations. Select the widget type in the button editor.

### Bar Chart

The bar chart widget draws a vertical bar that fills up based on a numeric value — perfect for power meters, CPU gauges, or tank levels.

**Configuration:**

| Setting | Description |
|---------|-------------|
| **Data binding** | A binding template that resolves to a number (e.g., `[mqtt:solar/power;watts]`) |
| **Min / Max** | The value range. The bar is empty at min and full at max |
| **Use absolute value** | When on, negative values fill the bar upward too (useful for grid power that can be negative) |
| **Reversed, high values are better** | When toggled, the four color picker values swap in place so low values use warning colors and high values use good colors (e.g. battery level, signal strength). The zone labels stay fixed — only the colors move |
| **Bar width %** | How wide the bar is relative to the button (1–100%) |
| **Bar background** | The color of the empty bar track |

**Color thresholds** divide the bar into up to four colored zones. The zone labels are positional (Below T1, T1–T2, T2–T3, Above T3) and stay fixed. Toggling "Reversed" swaps the color picker values so the colors visually flip while labels remain in place:

| Zone label | Default color (normal) | Default color (reversed) |
|-----------|----------------------|-------------------------|
| Below T1 | Green (`#4CAF50`) | Red (`#F44336`) |
| T1 – T2 | Light green (`#8BC34A`) | Orange (`#FF9800`) |
| T2 – T3 | Orange (`#FF9800`) | Light green (`#8BC34A`) |
| Above T3 | Red (`#F44336`) | Green (`#4CAF50`) |

Thresholds default to 33%, 66%, and 90% of the range. Customize them to match your use case — for a solar panel with a 5 kW max, you might set thresholds at 1.0, 3.0, and 4.5 kW.

Labels, icons, and colors still work alongside the widget. A typical bar chart button uses the top label for a title ("Solar") and the bottom label for the current value (`[mqtt:solar/power;watts;%.0f W]`).

### Gauge

The gauge widget draws an arc that fills based on a numeric value — ideal for clocks, speedometers, temperature dials, or any circular meter.

**Configuration:**

| Setting | Description |
|---------|-------------|
| **Data binding** | A binding template that resolves to a number (e.g., `[mqtt:sensor/temperature]`, `[time:%S]`) |
| **Min / Max** | The value range. The arc is empty at min and full at max |
| **Arc Degrees** | Total sweep of the arc (10–360°). 180 = half circle, 270 = three-quarter, 359 = near-full circle |
| **Start Angle** | Where the arc begins in LVGL degrees (0° = 3 o'clock, 90° = 6 o'clock, 180° = 9 o'clock, 270° = 12 o'clock) |
| **Use absolute value** | When on, negative values fill the arc too |
| **Show needle** | Display a line from the center to the current value position on the arc |
| **Reversed, high values are better** | Swaps the four color picker values (same behavior as bar chart) |
| **Arc Width %** | Arc thickness as a percentage of the radius (5–50%) |
| **Tick Marks** | Number of interior tick marks (0 = none). N ticks divide the arc into N+1 equal segments |
| **Needle Width** | Line width in pixels (0 = hidden, max 10) |
| **Tick Width** | Tick line width in pixels (1–5) |
| **Track Color** | Color of the unfilled arc background |
| **Needle Color** | Color of the needle line |
| **Tick Color** | Color of the tick marks |

**Color thresholds** work identically to the bar chart — four zones (Below T1, T1–T2, T2–T3, Above T3) with the same swap behavior when "Reversed" is checked.

The icon and center label are positioned inside the arc at the pivot point. A typical gauge button uses the center label for the numeric readout and the top label for a title.

**Clock example** (seconds hand on a full circle):
- Data binding: `[time:%S]`, Min: 0, Max: 60
- Arc Degrees: 359, Start Angle: 270 (12 o'clock)
- Tick Marks: 12, Center label: `[time:%H:%M]`

---

## Binding Templates

Binding templates are the engine behind live data on your buttons. They follow a simple pattern:

```
[scheme:parameters]
```

Static text before, after, or between tokens is preserved. If a binding can't resolve (topic not received yet, invalid path), it shows `---` as a placeholder. Errors show `ERR:reason`.

### MQTT Binding

**Syntax:** `[mqtt:topic;path;format]`

| Parameter | Required | Description |
|-----------|----------|-------------|
| **topic** | Yes | The MQTT topic to subscribe to |
| **path** | No | JSON key to extract. Use dot-notation for nested objects (`data.temp`). Omit or use `.` for raw payload |
| **format** | No | Printf format string for the value |

The device automatically subscribes to every topic it discovers across all 8 pads. Each unique topic is subscribed once, even if used by dozens of buttons.

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
| `heap_free` | Free heap memory (bytes) | `145320` |
| `heap_min` | Heap low-water mark (bytes) | `98000` |
| `heap_largest` | Largest free block (bytes) | `65536` |
| `heap_internal` | Free internal RAM (bytes) | `82000` |
| `psram_free` | Free PSRAM (bytes) | `6291456` |
| `psram_min` | PSRAM low-water mark (bytes) | `4194304` |
| `psram_largest` | Largest free PSRAM block (bytes) | `4194304` |
| `ip` | Device IP address | `192.168.1.42` |
| `hostname` | Device hostname | `macropad` |

Values are cached for up to 2 seconds to keep the CPU impact low.

**Examples:**

```
CPU: [health:cpu]%                                     → CPU: 42%
[health:heap_free;%d] bytes free                       → 145320 bytes free
WiFi: [health:rssi] dBm                               → WiFi: -54 dBm
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

Color fields (background, text, border) accept binding expressions, making buttons change color based on live data.

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

---

## Pad Actions (Bulk Operations)

The **More ▾** dropdown above the grid preview provides shortcuts for working with entire pads.

### Copy and Paste Buttons

In the button editor dialog, **Copy** saves the current button's settings (everything except grid position) to a clipboard. **Paste** applies the clipboard to whichever cell you're editing. This is the fastest way to create multiple similar buttons — configure one, then copy-paste and adjust the differences.

### Fill Pad

After copying a button, **Fill Pad** applies it to every empty cell in the grid. Useful for quickly populating a pad with a template button that you then customize per cell.

### Copy / Paste Pad

**Copy Pad** saves the entire pad layout (grid size, name, all buttons, background color). **Paste Pad** overwrites the current pad with the clipboard. Use this to duplicate a pad layout to a different pad number and then make adjustments.

### Export / Import Pad

**Export Pad** downloads the current pad as a JSON file. **Import Pad** loads a pad from a JSON file. Use these to share pad designs between devices or archive your work.

### Export / Import Device Config

**Export Device Config** downloads everything — all 8 pads plus all device settings (network, MQTT, display, operating mode) — as a single JSON file. **Import Device Config** restores from that file, triggering a reboot.

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

| Button | Image URL | Auth | Refresh | Letterbox |
|--------|-----------|------|---------|-----------|
| Front Door | `http://192.168.1.50/snap.cgi` | admin / password | 5000 ms | Off (cover) |
| Backyard | `http://192.168.1.51/snap.cgi` | admin / password | 5000 ms | Off (cover) |
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
Use **More ▾ → Export Device Config** regularly. It captures *everything* — all 8 pads, network settings, display config, and button layouts — in a single JSON file. If you ever factory reset or set up a new device, **Import Device Config** restores it all.
