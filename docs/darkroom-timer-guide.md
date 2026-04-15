# Darkroom Timer Guide

The darkroom timer turns your ESP32 Macropad into a dedicated enlarger timer with smart relay control. It manages a single exposure countdown, controls an enlarger lamp through a Shelly Plug, and provides focus mode for framing your print — all operated through configurable touch buttons on the device.

This guide covers hardware setup, configuration, and building your first enlarger pad.

---

## Hardware Setup

The darkroom timer controls an enlarger lamp through a **Shelly Plug** (Gen1 or Gen2) on your local network. The Shelly Plug acts as a smart relay; the device sends HTTP commands to turn the lamp on and off.

### Requirements

- A Shelly Plug (any model) connected to your WiFi network
- The enlarger lamp plugged into the Shelly Plug
- The macropad connected to the same WiFi network
- (Optional) A **TSL2591 light sensor** connected via I2C for metering features

### Shelly Plug Configuration

1. Set up the Shelly Plug using the Shelly app and connect it to your WiFi network.
2. Assign a **fixed/static IP address** to the Shelly Plug (recommended — via your router's DHCP reservation or the Shelly app).
3. Note the IP address (e.g., `192.168.1.100`).

> **Tip**: Test the Shelly Plug by opening `http://<shelly-ip>/relay/0?turn=on` in a browser. The relay should click on. Use `turn=off` to turn it back off.

### Light Sensor (Optional)

The **TSL2591** digital light sensor enables the [Light Meter](#light-meter) (bare-bulb reference reading and spot metering through negatives). Connect the sensor to the I2C bus:

| Pin  | Connection        |
|------|-------------------|
| SDA  | I2C SDA (GPIO 7)  |
| SCL  | I2C SCL (GPIO 8)  |
| VIN  | 3.3V              |
| GND  | GND               |

The sensor uses I2C address `0x29` and shares the bus with the touch controller and audio codec. It initializes automatically on boot and logs its detection status to the serial monitor.

> [!NOTE]
> The light sensor is optional. All timer features (exposure timer, test strip sequencer, chemistry timers) work without it. Metering features require the sensor.

---

## Device Configuration

### Shelly IP Address

Open the web portal and go to the **Home** page. Under the **Darkroom Timer** section, enter the Shelly Plug IP address (or hostname) and save.

The device sends `GET http://<ip>/relay/0?turn=on` and `turn=off` commands to this address. Requests are fire-and-forget with a 2-second timeout — a momentarily unreachable Shelly Plug won't block the timer.

---

## Exposure Timer

The exposure timer is a countdown timer with relay control. Set an exposure time, press start, and the enlarger lamp turns on for exactly that duration. When the countdown reaches zero, the lamp turns off and a beep sounds.

### Timer States

The exposure timer has four states:

| State | Lamp | Description |
|-------|------|-------------|
| **Stopped** | OFF | Idle — ready for a new exposure |
| **Running** | ON | Countdown active — lamp is on |
| **Paused** | OFF | Countdown frozen — lamp is off |
| **Focus** | ON | Lamp on with no countdown — for framing and focusing |

### State Transitions

```mermaid
stateDiagram-v2
    Stopped --> Running : start
    Stopped --> Focus : focus
    Focus --> Running : start (lamp stays on)
    Focus --> Stopped : focus_off
    Running --> Paused : pause
    Running --> Stopped : stop
    Running --> Stopped : countdown expires
    Paused --> Running : resume
    Paused --> Stopped : stop
```

The **focus → start** transition is seamless — the lamp stays on without any interruption, which avoids the print shifting between framing and exposure.

---

## Binding Tokens

Binding tokens let you display live timer data on button labels. Use these in the **Label** fields of any button on a pad.

### Available Tokens

| Token | Description | Example output |
|-------|-------------|----------------|
| `[expose:time]` | Exposure time setting (seconds) | `8.0` |
| `[expose:remaining]` | Countdown remaining | `8.0` |
| `[expose:elapsed]` | Countdown elapsed | `2.3` |
| `[expose:state]` | Current state | `running` |
| `[expose:relay]` | Relay state | `ON` |

### Format Override

All three time tokens (`time`, `remaining`, `elapsed`) default to seconds with one decimal place (e.g., `8.0`, `5.7`). Override the format with a semicolon when you need a different display:

| Format | Output example | Description |
|--------|----------------|-------------|
| `mm:ss` | `0:08` | Minutes and seconds (default) |
| `mm:ss.d` | `0:08.3` | With tenths of a second |
| `ss` | `8` | Seconds only |
| `ss.d` | `8.3` | Seconds with tenths |
| `hh:mm:ss` | `0:00:08` | Hours, minutes, seconds |

**Examples**:

- `[expose:remaining;mm:ss.d]` — countdown as minutes: `0:08.3`
- `[expose:time;ss]` — exposure setting as whole seconds: `8`
- `Remaining: [expose:remaining]` — with static prefix: `Remaining: 8.0`

### Using with Expressions

Combine with the expression binding for conditional labels:

- `[expr:[expose:state]=="running"?"●":"○"]` — filled dot when running
- `[expr:[expose:state]=="focus"?"FOCUS":""]` — shows "FOCUS" only in focus mode

---

## Button Actions

Use the **Exposure Timer** action type in the button editor to wire buttons to timer commands.

### Available Commands

| Command | Description |
|---------|-------------|
| **Toggle** | Start from stopped, pause from running, resume from paused |
| **Start** | Begin countdown (turns lamp on) |
| **Stop** | Stop and reset timer (turns lamp off) |
| **Pause** | Freeze countdown (turns lamp off) |
| **Resume** | Continue countdown (turns lamp on) |
| **Reset** | Stop timer, keep exposure time setting |
| **Focus Light ON** | Turn lamp on without timer (for framing) |
| **Focus Light OFF** | Turn lamp off (exit focus mode) |
| **Focus Light Toggle** | Toggle focus on/off (no-op while running) |
| **Set Time** | Set exposure time to a specific value (seconds) |
| **Add Seconds** | Adjust exposure ± seconds |
| **Add F-Stops** | Adjust exposure ± f-stops |

### F-Stop Adjustments

**Add F-Stops** multiplies the current exposure time by 2^N, where N is the stop value. This is the standard photographic relationship — each full stop doubles or halves the exposure.

| Value | Effect | 8s becomes |
|-------|--------|------------|
| `1` | +1 stop (double) | 16s |
| `-1` | -1 stop (halve) | 4s |
| `0.5` | +½ stop | 11.3s |
| `-0.5` | -½ stop | 5.7s |
| `0.333` | +⅓ stop | 10.1s |
| `-0.333` | -⅓ stop | 6.3s |

> **Tip**: Use ⅓-stop increments (`0.333` and `-0.333`) for fine-tuning. These match the standard ⅓-stop aperture clicks on most enlarger lenses.

### Multi-Action Buttons

Buttons support multiple actions that execute in sequence. Useful combinations:

- **Start + Beep**: Play a confirmation beep when starting an exposure
- **Reset + Set Time**: One button that resets and loads a preset exposure time

---

## Building an Enlarger Pad

Here's a practical walkthrough for building a darkroom enlarger control pad.

### Recommended Layout

A 3×4 grid works well for an enlarger pad on the JC4880P433 (480×800 portrait display):

| | Column 1 | Column 2 | Column 3 |
|---|----------|----------|----------|
| **Row 1** | Time display | Remaining display | State indicator |
| **Row 2** | -⅓ stop | Start / Pause | +⅓ stop |
| **Row 3** | -1s | Stop / Reset | +1s |
| **Row 4** | Focus | Set 8s | Set 15s |

### Step-by-Step Setup

1. **Create a new pad** — Open the Pads page, select an empty pad, set it to 3 columns × 4 rows, and name it "Enlarger".

2. **Time display button** (row 1, col 1) — Set the center label to `[expose:time;ss.d]s` and give it a dark background. No action needed — this is a display-only button showing the current exposure setting.

3. **Remaining display button** (row 1, col 2) — Set the center label to `[expose:remaining;mm:ss.d]`. Use a label style with a large font (`font_size:48`) so the countdown is easy to read under safelight.

4. **State indicator** (row 1, col 3) — Set the center label to `[expose:state]`. Optionally use a dynamic background color with an expression binding.

5. **F-stop buttons** (row 2, col 1 and col 3):
   - **-⅓ stop**: Label `-⅓`, action = Exposure Timer → Add F-Stops, value = `-0.333`
   - **+⅓ stop**: Label `+⅓`, action = Exposure Timer → Add F-Stops, value = `0.333`

6. **Start/Pause button** (row 2, col 2) — Label `[expr:[expose:state]=="running"?"PAUSE":"START"]`, action = Exposure Timer → Toggle. Use a green background for visibility.

7. **Second adjustment buttons** (row 3, col 1 and col 3):
   - **-1s**: Label `-1s`, action = Exposure Timer → Add Seconds, value = `-1`
   - **+1s**: Label `+1s`, action = Exposure Timer → Add Seconds, value = `1`

8. **Stop/Reset button** (row 3, col 2) — Label `STOP`, action = Exposure Timer → Stop. Use a red background.

9. **Focus button** (row 4, col 1) — Label `[expr:[expose:state]=="focus"?"FOCUS ●":"FOCUS"]`, action = Exposure Timer → Focus Light Toggle. Safe during an active exposure (no-op while running).

10. **Preset buttons** (row 4, col 2 and col 3):
    - **8s preset**: Label `8s`, action = Exposure Timer → Set Time, value = `8`
    - **15s preset**: Label `15s`, action = Exposure Timer → Set Time, value = `15`

### Label Legibility

In a darkroom under safelight, the display is your primary light source. Keep these tips in mind:

- Use **large font sizes** for countdown displays (`font_size:36` or larger)
- Use **high-contrast colors** — white or bright text on dark backgrounds
- Keep labels **short** — abbreviate where possible
- The `mm:ss.d` format with tenths is useful during focusing but `mm:ss` is sufficient during normal printing

---

## Test Strip Sequencer

The test strip sequencer automates f-stop test strip exposures. It calculates a series of segments spaced at equal f-stop intervals around a base time, then exposes each segment in sequence with audio cues and inter-segment pauses for mask movement.

### How It Works

The sequencer uses the **progressive uncover** technique. Start with the paper fully masked, then reveal one more strip before each exposure step. Segment 1 is uncovered first and accumulates the most light; the last segment receives only a single exposure increment.

Segment times are calculated using f-stop math: each segment's cumulative time is `base_time × 2^(offset)`, where the offset is determined by the step interval and position relative to the center segment.

With default settings (8s base, ⅓-stop steps, 7 segments), a typical strip produces cumulative exposures from roughly 4s to 16s — a 2-stop range centered on your estimate.

### Sequence Flow

When you start the sequencer, it runs automatically through all segments:

1. **Initial countdown** — a configurable heads-up period (default 5s) with per-second ticks and a pre-expose beep in the last 3 seconds
2. **Expose** — the relay turns on for the incremental exposure time (with optional per-second ticks); after this step, uncover the next strip
3. **Pause** — the relay turns off and a configurable pause (default 3s) gives you time to uncover the next strip; the pre-expose beep plays in the last 3 seconds
4. Steps 2-3 repeat for each remaining segment
5. **Done** — the relay turns off, a completion tone sounds, and the sequencer returns to idle

The only user interaction during a sequence is **Cancel** to abort.

### Binding Tokens

Use the `strip` binding scheme to display sequencer data on button labels.

#### State and Progress

| Token | Description | Example output |
|-------|-------------|----------------|
| `[strip:state]` | Current state | `exposing` |
| `[strip:segment]` | Current segment number (1-based) | `3` |
| `[strip:segments]` | Total segment count | `7` |
| `[strip:progress]` | Current progress | `3/7` |
| `[strip:remaining]` | Current phase remaining (seconds) | `4.2` |
| `[strip:elapsed]` | Current phase elapsed (seconds) | `1.8` |
| `[strip:relay]` | Relay state | `ON` |

#### Segment Data

| Token | Description | Example output |
|-------|-------------|----------------|
| `[strip:seg_time:N]` | Cumulative time for segment N | `8.0` |
| `[strip:seg_inc:N]` | Incremental time for segment N | `1.9` |
| `[strip:seg_offset:N]` | F-stop offset for segment N | `+0.3` |
| `[strip:seg_inc]` | Incremental time for current segment | `1.9` |

#### Configuration

| Token | Description | Example output |
|-------|-------------|----------------|
| `[strip:base_time]` | Base time setting | `8.0` |
| `[strip:step]` | Step interval (fraction) | `1/3` |
| `[strip:range]` | Exposure range | `4.0-16.0` |
| `[strip:total_time]` | Estimated total sequence time | `98` |
| `[strip:countdown]` | Initial countdown setting | `5` |
| `[strip:pause]` | Inter-segment pause setting | `3` |
| `[strip:tick]` | Exposure tick on/off | `on` |

#### Table Widget

The `[strip:table]` token returns a JSON payload for the table widget, showing all segments with their incremental exposure duration and cumulative total. The table uses darkroom-safe red tones for text and highlights the active segment during a sequence. Use it as the `widget_data_binding` on a button configured with the table widget type.

#### Format Override

Time tokens (`remaining`, `elapsed`, `total_time`) support format overrides:

- `[strip:remaining;mm:ss.d]` — remaining as `0:04.2`
- `[strip:total_time;mm:ss]` — total time as `1:38`

### Button Actions

Use the **Test Strip** action type in the button editor.

#### Control Commands

| Command | Description |
|---------|-------------|
| **Start** | Begin the sequence from idle |
| **Cancel** | Abort the sequence, turn relay off |

#### Configuration Commands

Configuration commands only take effect when the sequencer is idle.

| Command | Value | Description |
|---------|-------|-------------|
| **Set Base Time** | seconds | Center exposure time (1.0-999.9, default 8.0) |
| **Add Base Time** | seconds | Add or subtract from base time (e.g. 1, -0.5) |
| **Step Interval Up** | — | Increase step: 1/5 → 1/4 → 1/3 → 1/2 → 1/1 |
| **Step Interval Down** | — | Decrease step: 1/1 → 1/2 → 1/3 → 1/4 → 1/5 |
| **Add Segments** | count | Add or remove segments (+2/-2, stays odd, clamped 3-11) |
| **Set Segments** | count | Set number of segments (rounded to odd, clamped 3-11) |
| **Set Countdown** | seconds | Initial countdown duration (2-10, default 5) |
| **Set Pause** | seconds | Inter-segment pause for mask movement (3-15, default 3) |
| **Set Tick** | on/off | Per-second tick during exposure (default on) |

### Building a Test Strip Pad

A 3×4 grid provides a practical layout for test strip controls:

| | Column 1 | Column 2 | Column 3 |
|---|----------|----------|----------|
| **Row 1** | Base time display | Progress / state | Step display |
| **Row 2** | Step down | Start | Step up |
| **Row 3** | -1 segment | Cancel | +1 segment |
| **Row 4** | Segment table (span 3 cols) | | |

#### Key Buttons

- **Base time display** — Label: `[strip:base_time]s`, no action (display-only)
- **Progress / state** — Label: `[expr:[strip:state]=="idle"?"READY":[strip:progress]]`
- **Step display** — Label: `[strip:step] stop`
- **Start** — Action: Test Strip → Start
- **Cancel** — Action: Test Strip → Cancel
- **Step down / Step up** — Action: Test Strip → Step Interval Down / Step Interval Up
- **-1 / +1 segment** — Action: Test Strip → Add Segments, value: `-2` / `2` (always odd count)
- **Segment table** — Widget: Table, data binding: `[strip:table]` (shows all segments with duration and total)

> **Tip**: The default ⅓-stop step matches standard aperture increments. Tap Step Up for wider coverage (½ or full stop) or Step Down for finer precision (¼ or ⅕ stop).

---

## Light Meter

The light meter takes sensor readings to calibrate your enlarger and meter negatives. It combines bare-bulb reference reading (Lref), bright/dark spot metering, and exposure time calculation into a single module. All commands are independent — call them in any order, re-read at any time, and results recalculate automatically.

**Grade recommendation works without any calibration** — SBR is purely a ratio of the two spot readings. Exposure time requires Lref and a Zone V time.

> [!NOTE]
> The light meter requires the TSL2591 light sensor. See [Light Sensor](#light-sensor-optional) in Hardware Setup.

### How It Works

The meter has three independent readings and two configuration values:

| Input | How to set | Persists across negatives |
|-------|-----------|--------------------------|
| **Lref** | Read Lref button (bare-bulb sensor reading) or manual set | Yes — stored in [shared memory](#shared-memory) |
| **Zone V time** | Manual via ±0.1s and ±1s buttons | Yes |
| **Bright spot** | Read Bright button (sensor reading through negative) | No — cleared on next read |
| **Dark spot** | Read Dark button (sensor reading through negative) | No — cleared on next read |

Whenever any input changes, the meter recalculates all derived values (SBR, grade, exposure time). If you read the spots in the wrong order, the meter automatically swaps them.

### Calibration Steps (One-Time Per Session)

Before metering negatives, take two calibration readings:

1. **Read Lref** — Remove the negative. Set the lens to working aperture with a Grade 2 filter. Place the sensor puck on the easel. Press **Read Lref** — the sensor takes a bare-bulb reading and stores it as the reference illuminance.

2. **Bare-bulb test strip** — Run a [test strip](#test-strip-sequencer) with a 5.0s base time (no negative, same enlarger setup). The segment that produces a medium grey is your **Zone V time**. Enter it on the meter pad.

These two values anchor the meter to your specific enlarger, lens, and paper combination. Redo them whenever you change the enlarger setup.

### Metering a Negative

With Lref and Zone V time set:

1. Insert a negative and focus the image
2. Turn on the enlarger (use the exposure timer's Focus mode or a separate Focus button)
3. Place the sensor puck on the **brightest** image area and tap **Read Bright**
4. Move the puck to the **darkest** area and tap **Read Dark**
5. Read results: SBR, recommended grade, and suggested exposure time
6. Navigate to your exposure pad, set the suggested time, and print

To meter another negative: re-read bright and dark. Lref and Zone V time persist.

### The Math

After both spot readings, the meter computes:

- **SBR** = log₁₀(L_bright / L_dark) — the negative’s contrast range
- **Grade** = looked up from an SBR-to-grade table (Zone System standard values)
- **Exposure time** = Zone V time × 10^(D_mid), where D_mid is the average density offset from Lref

### SBR-to-Grade Table

| SBR ≤ | Grade | Description |
|-------|-------|-------------|
| 0.60 | 5 | Very flat |
| 0.70 | 4.5 | Flat |
| 0.80 | 4 | Flat |
| 0.90 | 3.5 | Slightly flat |
| 1.00 | 3 | Normal- |
| 1.10 | 2.5 | Normal |
| 1.20 | 2 | Normal |
| 1.35 | 1.5 | Contrasty |
| 1.50 | 1 | Contrasty |
| 1.65 | 0.5 | Very contrasty |
| > 1.65 | 0 | Extremely contrasty |

### Sensor Configuration

The TSL2591 uses **auto-gain** starting at high sensitivity (428× gain, 300ms integration) and falls back to lower gain on overflow. Each reading averages 3 samples for noise reduction. Total read time is approximately 1 second.

Lux values are displayed with 4 decimal places for full precision at the sensor’s resolution (~0.003 lux at high gain).

### Binding Tokens

| Token | Description | Example output |
|-------|-------------|----------------|
| `[meter:lref]` | Lref (lux) | `3.1270` or `---` |
| `[meter:zone5_time]` | Zone V time (seconds) | `6.3` or `---` |
| `[meter:l_bright]` | Bright spot reading (lux) | `1.0430` or `---` |
| `[meter:l_dark]` | Dark spot reading (lux) | `0.0870` or `---` |
| `[meter:sbr]` | SBR (computed) | `1.08` or `---` |
| `[meter:grade]` | Recommended grade | `2` or `2.5` or `---` |
| `[meter:grade_label]` | Grade description | `Normal`, `Contrasty` |
| `[meter:time]` | Suggested exposure time (seconds) | `14.2` or `---` |

### Button Actions

Use the **Light Meter** action type in the button editor.

#### Reading Commands

| Command | Description |
|---------|-------------|
| **Read Lref** | Bare-bulb sensor reading — stores result as Lref and in shared memory |
| **Read Bright** | Take bright spot reading (callable any time) |
| **Read Dark** | Take dark spot reading (callable any time) |

#### Configuration Commands

| Command | Value | Description |
|---------|-------|-------------|
| **Set Lref** | lux | Override Lref value manually |
| **Add Lref** | lux | Adjust Lref ± (e.g., `100` or `-50`) |
| **Set Zone V** | seconds | Set Zone V time (e.g., `6.3`) |
| **Add Zone V** | seconds | Adjust Zone V time ± (e.g., `0.1` or `-0.1`) |

### Building a Meter Pad

A 4×8 grid with display rows at the top and action buttons at the bottom:

| | Column 1 | Column 2 | Column 3 | Column 4 |
|---|----------|----------|----------|----------|
| **Row 1** | Lref display | | Zone V time | |
| **Row 2** | -0.1 | -1 | +1 | +0.1 |
| **Row 3** | Bright reading | | Dark reading | |
| **Row 4** | SBR | | Grade | |
| **Row 5** | Grade label (full width) | | | |
| **Row 6** | Suggested time (full width, large font) | | | |
| **Row 7** | Read Lref | | (empty or info) | |
| **Row 8** | Read BRIGHT | | Read DARK | |

**Key buttons**:

- **Lref display** — Label: `[meter:lref]`, top label: `Lref`. No action (display-only).
- **Zone V time** — Label: `[meter:zone5_time] s`, top label: `Zone V time`. No action.
- **Zone V adjustment** — Row of four buttons: -0.1, -1, +1, +0.1. Action: Light Meter → Add Zone V with values `-0.1`, `-1`, `1`, `0.1`.
- **Bright / Dark readings** — Labels: `[meter:l_bright]` / `[meter:l_dark]`. Display-only.
- **SBR / Grade** — Labels: `[meter:sbr]` / `[meter:grade]`. Display-only.
- **Grade label** — Full width, label: `[meter:grade_label]`.
- **Suggested time** — Full width, label: `[meter:time] s` with `font_size:48`. Display-only.
- **Read Lref** — Action: Light Meter → Read Lref. Label: `Read Lref`.
- **Read BRIGHT** — Action: Light Meter → Read Bright. Label: `BRIGHT`.
- **Read DARK** — Action: Light Meter → Read Dark. Label: `DARK`.

### Typical Workflow

1. **First session setup**: Read Lref (bare-bulb), run a test strip, enter Zone V time
2. Insert a negative, turn on the enlarger (Focus mode)
3. Read BRIGHT → Read DARK
4. Read results: SBR, grade, suggested time
5. Go to exposure pad → set time → print

For the next negative, repeat steps 2–5. Lref and Zone V time persist.


## Shared Memory

Shared memory is a RAM-only key-value store for passing numeric values between darkroom modules. Values set by one feature (such as a calibration reading) can be displayed on any button label or consumed by other features.

Values are stored in RAM only and reset on reboot. The store holds up to 8 entries.

### Binding Tokens

Use the `mem` binding scheme to display stored values on button labels.

| Token | Description | Example output |
|-------|-------------|----------------|
| `[mem:lref]` | Value for key `lref` | `1847.3` |
| `[mem:lref;%.0f]` | With printf format override | `1847` |
| `[mem:undefined_key]` | Key not set | `---` |

The default format shows one decimal place and strips trailing `.0` for clean integers. Override the format with a semicolon and a printf-style format string (e.g., `%.0f` for no decimals, `%.2f` for two decimals).

### Button Actions

Use the **Memory** action type in the button editor to set values from button presses.

| Command | Description |
|---------|-------------|
| `set_<key>:<value>` | Set a named value (e.g., `set_lref:1847.3`) |

The command format is `set_` followed by the key name, a colon, and the numeric value. Keys are up to 15 characters.

**Examples**:

- `set_lref:1847.3` — store a reference light reading
- `set_grade:2` — store a paper grade selection
- `set_base:8.0` — store a base exposure time

---

## Chemistry Timers

For timing chemical processing (developer, stop bath, fixer), the darkroom timer device includes the standard **Timer Control** action type with three independent count-up or countdown timers. These use the existing timer engine and don't require the Shelly Plug.

Configure chemistry timers on the **Home** page under the **Timers** section. Set each timer's mode (count-up or countdown) and countdown preset. Use Timer Control button actions to start, stop, and reset each timer.

A typical setup uses Timer 1 for the developer, Timer 2 for stop bath, and Timer 3 for fixer — each with its own countdown preset and expire beep.

See the [Pad Editor Guide](pad-editor-guide.md) for general button and binding configuration details.
