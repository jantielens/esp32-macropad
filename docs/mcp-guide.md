# MCP Server Guide

The firmware includes a built-in **Model Context Protocol (MCP)** server. It lets a
local AI assistant — Claude Desktop, Cursor, VS Code Copilot Chat, Continue, and
other MCP clients — both **inspect** and **control** the device through chat:

- "What's my device status?"
- "Press the kitchen-lights button."
- "Switch to the dashboard screen."
- "Reboot the device."

The server is **off by default** and opt-in from the web portal. It is served only
when the device is connected to your WiFi (station mode), never in the setup access
point.

> **Build flag:** the feature is gated by the `HAS_MCP` compile-time flag (default
> `true`). Boards built with `#define HAS_MCP false` compile the MCP server out
> entirely (no `/mcp` endpoint, no portal card) to save flash. `HAS_MCP` is
> independent of the display — headless boards still expose the read tools and
> `system_command`, while display tools (`set_screen`, `set_backlight`, `wake`,
> `list_screens`, pads) only appear on boards with a screen.

## Security model at a glance

| Layer | Behavior |
|-------|----------|
| Enabled | Off by default. Turn it on in the portal. |
| Token | A dedicated bearer token. Required on every request. Shown once at generation. |
| Read tools | Available when enabled and a token is set. |
| Control tools | Hidden and refused unless you also enable the **control** toggle. |
| Authoring tools | Hidden and refused unless you also enable the **pad authoring** toggle (create/modify/remove buttons). |
| Network | Station mode only. Inert in setup/AP mode. |
| Transport | Plain HTTP on your LAN — **no TLS**. |

> ⚠️ **Plain-text on the LAN.** The token and all tool traffic travel cleartext over
> your local network — the same posture as the device's existing WiFi, MQTT, and
> Home Assistant secrets. Enable MCP (and especially control) **only on trusted
> networks**. Treat the token as a network-local secret, not an internet credential.
> The token is stored in NVS in plain text like other device secrets, so physical or
> flash access can reveal it.

## Enable the server

1. Open the web portal (see the [Web Portal Guide](web-portal-guide.md)).
2. Go to **Connectivity → MCP Server**.
3. Tick **Enable MCP server**.
4. Click **Generate new token** and **copy the token immediately** — it is shown
   only once and cannot be retrieved later. Generating a new token replaces any
   previous one.
5. (Optional) Tick **Allow control tools** if you want the assistant to press
   buttons, change screens, set backlight, or reboot. Leave it off for read-only
   access.
6. (Optional) Tick **Allow pad authoring** to let the assistant create, modify,
   and remove buttons/widgets. This is a separate, more sensitive permission than
   control — leave it off unless you want the assistant to edit pads.
7. Click **Save**.

Settings apply immediately — no reboot is required.

## Find the endpoint

The MCP endpoint is a single HTTP `POST` route:

```
http://<hostname>.local/mcp
```

The portal's MCP card shows the exact URL for your device. `<hostname>` is the
device's mDNS name (for example `http://esp32-1a2b.local/mcp`). If mDNS (`.local`)
discovery does not work on your network, use the device's IP address instead:

```
http://192.168.1.50/mcp
```

You can find the IP on the portal home page or in your router's client list.

## Connect a client

Every request must include the bearer token:

```
Authorization: Bearer <your-token>
```

### VS Code (Copilot Chat / MCP)

Add an HTTP MCP server to your MCP configuration:

```jsonc
{
  "servers": {
    "esp32-macropad": {
      "type": "http",
      "url": "http://esp32-1a2b.local/mcp",
      "headers": {
        "Authorization": "Bearer YOUR_TOKEN_HERE"
      }
    }
  }
}
```

### Cursor

In Cursor's MCP settings, add an HTTP server with the URL and an `Authorization`
header set to `Bearer YOUR_TOKEN_HERE`.

### Claude Desktop (and other stdio-only clients)

Claude Desktop speaks MCP over stdio, so bridge it to the device's HTTP endpoint
with [`mcp-remote`](https://www.npmjs.com/package/mcp-remote). Pass the token with
`--header`:

```jsonc
{
  "mcpServers": {
    "esp32-macropad": {
      "command": "npx",
      "args": [
        "mcp-remote",
        "http://esp32-1a2b.local/mcp",
        "--header",
        "Authorization: Bearer YOUR_TOKEN_HERE"
      ]
    }
  }
}
```

The same `mcp-remote` bridge works for any stdio-only MCP client (Continue and
others) — point it at the device URL and pass the `Authorization` header.

## What the assistant can do

```mermaid
graph LR
    CLIENT["AI assistant"] -->|"POST /mcp (Bearer token)"| GATE["Enabled · station mode<br/>Origin · token"]
    GATE --> READ["Read tools<br/>(always available)"]
    GATE --> CONTROL["Control tools<br/>(control toggle on)"]
    READ --> INFO["status · health · screens<br/>pads · sensors · config"]
    CONTROL --> ACT["press_button · set_screen<br/>backlight · wake · system<br/>notify · volume · timers · config"]
```

**Read tools** (always available once enabled):

- `get_device_status` — firmware version, board, uptime, current screen, WiFi state.
- `get_health` — heap (internal/PSRAM), CPU, WiFi signal.
- `list_screens` / `get_current_screen` — available screens and the active one.
- `list_pads` / `get_pad` — configured pads and their buttons (so the assistant
  knows what it can press).
- `get_sensors` — current sensor readings (empty on boards without sensors).
- `get_config` — current device settings (device name, network, MQTT/HA,
  power, display/screen saver, audio). Secrets are redacted to `<field>_set`
  booleans — passwords and tokens are never returned.
- `get_component_config` — the saved JSON for one auxiliary feature: `timers`,
  `swipe`, `boot`, `button-defaults`, `hw-buttons`, or `mqtt-triggers`.

**Control tools** (require the control toggle):

- `press_button` — press a pad button by position or label, exactly like a tap.
- `set_screen` — navigate to a screen.
- `set_backlight` / `wake` — adjust display brightness or cancel the screen saver.
- `notify` — show a message bubble on the screen (empty text dismisses it).
- `set_volume` — set (0-100) or adjust (signed delta) the speaker volume.
- `timer_control` — start/stop/toggle/pause/resume/reset/lap/set/adjust one of
  the three on-screen timers.
- `set_config` — write a safe subset of device settings that apply live without a
  reboot: device name, backlight brightness, the screen-saver group, MQTT publish
  interval/scope, and audio volume. WiFi/MQTT/HA credentials, operating mode, and
  security toggles stay read-only (change those in the portal).
- `set_component_config` — overwrite one auxiliary feature's config (`timers`,
  `swipe`, `boot`, `button-defaults`, `hw-buttons`, `mqtt-triggers`) with a
  validated full-replacement object (read it first with `get_component_config`,
  edit, send back).
- `system_command` — `reboot`, `wifi_reconnect`, or `screensaver`.

Display-related tools are present only on boards that have a display; `set_volume`
requires audio hardware; `get_component_config` lists only the components compiled
into the board.

**Authoring tools** (require the pad authoring toggle; display boards only):

- `get_capabilities` — manifest of widget types + fields, button schema, label-style
  DSL, binding schemes (incl. `[pad:name]` and `template_pad`), and grid limits.
  It also carries a `device_config` section advertising `set_config`'s writable
  fields and the read/write component list. Read-only, so it works with token alone.
- `get_pad_blocks` — list pre-built button groups (building blocks) that can be
  dropped onto a pad. Read-only.
- `validate_pad` — dry-run validate a pad JSON (grid bounds, span overflow,
  widget types, colors, binding tokens) without saving. Read-only.
- `resolve_bindings` — resolve `[scheme:params]` tokens against the device's
  **live** data and return the resolved text, to debug/preview what a binding or
  a proposed button renders to **without saving**. Takes `bindings` (array of
  template strings) and/or a `button` object (its bindable `label_*` / `*_color`
  / `btn_state` / `widget_data_binding[_2..4]` fields are resolved), plus an
  optional `screen` (`pad_N` or friendly name) for that pad's `[pad:name]`
  context. Returns resolved **values** only — it does not render pixels (use
  `GET /api/screenshot` for a visual). Read-only; nothing is persisted.
- `set_button` / `set_buttons` — create or replace a button (or many in one save)
  by position, using the same schema as the portal pad editor.
- `set_pad` — set pad-level fields (layout, cols/rows, wake_screen, bg_color,
  `template_pad`, and named `[pad:name]` bindings) without touching buttons.
- `remove_button` / `clear_pad` — delete one button or empty a pad.

Writes are validated before saving and persisted on the main loop. Concurrent
edits from the LLM and the portal editor are **last-write-wins per pad** — the
last save replaces the pad, so avoid editing the same pad in both at once.

> **Verify bindings, don't assume.** A write rejects malformed binding *syntax*,
> but a syntactically valid `[scheme:params]` binding can still resolve to `---`
> (no data) or an unintended value. Make `resolve_bindings` part of the authoring
> loop: after adding or editing any binding, call it to confirm the binding
> resolves to the intended live value. The server's `initialize` instructions
> tell connected assistants to do this as a matter of course.

### Device-class tools

Some device classes register their own tools, which appear only on firmware
built for that class. Each class keeps its MCP tools inside its own folder
(e.g. `src/app/device_classes/shutter_tester/mcp/`) and is pulled into the build
by a single `#if IS_*` include in `mcp_components.cpp`, so non-matching boards
compile none of it.

**Shutter Tester** (`IS_SHUTTER_TESTER` boards, e.g. `jc4880p433-shutter`):

- `get_shutter_status` — live state: active preset/sensors/sample rate,
  comparison target speed + lock, session state (incl. guided progress),
  alignment readout, and the latest measurement with per-sensor health. *(read)*
- `get_shutter_history` — rolling history of recent measurements
  (speed, deviation, verdict, spread). *(read)*
- `get_shutter_waveform` — the latest capture's per-sensor ADC waveform,
  decimated (min-per-bucket) so the exposure pulse stays visible while the
  payload stays bounded. *(read)*
- `list_shutter_sessions` / `get_shutter_session` — saved test sessions: the
  manifest, and one session's metadata plus a `detail_url`. The full per-shot
  record (with per-sensor waveforms) is large, so `get_shutter_session` does not
  inline it — stream `GET /api/sessions/{id}` for the raw record. *(read)*
- `list_shutter_tests` / `get_shutter_test` — guided-test script definitions and
  their speed lists. *(read)*
- `shutter_control` — one control command (`command` + optional `value`) covering
  target speed (`set` / `adjust` / `toggle_lock`), sessions (`sess_start` /
  `sess_stop` / `sess_toggle` / `sess_discard`), guided runs (`guide_start` /
  `guide_stop` / `guide_skip` / `guide_redo`), and capture
  (`align_start` / `align_stop` / `recalibrate`). The tool description explains
  the hands-on rig workflow: the user aligns the camera over the sensors and
  **physically fires the shutter** for each measurement (there is no per-shot
  command), so the assistant coordinates capture by polling `get_shutter_status`
  / `get_shutter_history` and relaying guided-run steps. *(control)*
- `delete_shutter_session` — delete a saved session by id. *(control, destructive)*
- `set_shutter_tests` — overwrite the guided-test script file. *(authoring)*

  The `content` is a plain-text DSL (not JSON), one test per block — the same
  format as the portal's **Guided Test Definitions** editor. The tool's own
  description carries the full grammar; in short:

  ```text
  name: leicam6|Leica M6 (x3)
  shots_per_speed: 3
  1
  1/2
  1/60
  1/1000
  ```

  Speeds are bare standard shutter speeds (whole seconds like `1`/`2` or
  fractions like `1/2`/`1/1000`); `#` starts a comment; repeat the `name:` block
  for more tests.

> **Note:** The Shutter Tester read tools assume a PSRAM board — their scratch
> buffers allocate with `MALLOC_CAP_SPIRAM` and have no internal-RAM fallback.

**Coffee Scale** (`IS_COFFEE_SCALE` boards, e.g. `jc4880p433-nau7802`):

- `get_scale_status` — live scale state: weight (g), flow rate (g/s),
  availability, calibration factor + raw offset, calibration reference weight,
  status string, and the active smoothing preset. *(read)*
- `get_brew_status` — brew state machine snapshot: phase (idle/active/done),
  active template, current stage (name, index, count, instruction, advance-button
  label), brew timer, live weight/flow, water + dose weight, current-stage
  targets (weight/flow/time with remaining), and captured data points. *(read)*
- `get_brew_series` — the in-progress brew's recorded weight/flow time-series
  (1 Hz), decimated (last-weight + peak-flow per bucket) so the pour shape stays
  visible while the payload stays bounded, plus stage-transition markers. Live
  only while a brew records; for finished brews use `get_brew`'s `detail_url`. *(read)*
- `list_brews` / `get_brew` — saved brews: a newest-first (capped) manifest, and
  one brew's summary fields, template snapshot, and markers plus a `detail_url`.
  The full per-second series is large, so `get_brew` does not inline it — stream
  `GET /api/brews?id=N` for the raw record. *(read)*
- `list_brew_templates` / `get_brew_template` — brew template definitions
  (built-in + user) and one template serialized to its JSON DSL. *(read)*
- `scale_control` — one scale command (`command` + optional `value`):
  `tare`, `calibrate`, `cal_weight` (gram delta), `cal_weight_set` (absolute g).
  The description walks the assistant through the hands-on **calibration**
  procedure (clear + tare, set the reference mass, place that exact weight,
  confirm it settled, then `calibrate`) rather than calling `calibrate` blind. *(control)*
- `brew_control` — one brew command (`command` + optional `value`) covering
  `set_template`, `advance`, `start`, `next`, `stop`, `reset`, `tare`. The
  description drives a brew **one stage at a time**: relay the current stage's
  instruction from `get_brew_status`, wait for the user to complete the physical
  step, then `advance` (manual stages only — auto stages self-advance). *(control)*
- `delete_brew` — delete a saved brew by id. *(control, destructive)*
- `delete_brew_template` — delete a user (dynamic) template by name; built-ins
  cannot be deleted. *(control, destructive)*
- `set_brew_template` — create or replace one brew template. *(authoring)*

  The `content` is the template's JSON DSL — the same format `get_brew_template`
  returns and the portal's template editor uses. The tool's own description
  carries the full schema; in short:

  ```json
  {
    "v": 1,
    "name": "my_v60",
    "display_name": "My V60",
    "stages": [
      { "name": "Dose", "instruction": "Add coffee", "type": "manual",
        "on_enter": ["tare"], "on_exit": ["capture_dose"] },
      { "name": "Bloom", "type": "auto_time", "auto_time_s": 45,
        "target_weight": 40.0 }
    ]
  }
  ```

  Stage `type` is `manual`, `auto_weight` (advances at `target_weight` /
  `auto_threshold` g) or `auto_time` (advances after `auto_time_s`); effects in
  `on_enter`/`on_exit` include `tare`, `beep`, `capture_dose`, `marker`,
  `capture_weight`. Max 16 stages; the template is validated before saving.

> **Note:** The Coffee Scale read tools assume a PSRAM board — their scratch
> buffers (live series, brew-log parsing) allocate with `MALLOC_CAP_SPIRAM` and
> have no internal-RAM fallback.

**Darkroom Timer** (`IS_DARKROOM_TIMER` boards, e.g. `jc4880p433-darkroom`):

- `get_expose_status` — single-exposure timer state: state
  (stopped/running/paused/focus), exposure time setting, dry-down-compensated
  effective time, dry-down percent, countdown remaining/elapsed, and the relay
  on/off state. *(read)*
- `get_strip_status` — f-stop test strip sequencer state: state
  (idle/countdown/exposing/pausing), current segment + total count, base time,
  step interval (stops + label), countdown/pause settings, current-phase
  remaining/elapsed, estimated total sequence time, relay state, and the
  per-segment table (cumulative/incremental seconds + f-stop offset). *(read)*
- `get_meter_status` — print-prep light meter state: Lref + Zone V inputs,
  bright/dark spot lux, computed Subject Brightness Range, recommended grade +
  label, recommended exposure time, and magnification-compensation readings
  (lux A/B + factor). *(read)*
- `get_relay_config` — the enlarger/safelight relay action configuration (an
  object keyed `enlarger_on`/`enlarger_off`/`safelight_on`/`safelight_off`). *(read)*
- `list_prints` / `get_print` — saved print sessions: a newest-first (capped)
  manifest with summary fields, notes, star, and a `detail_url`, plus one print's
  full record (exposure fields, metering context, test-strip segments, notes,
  star). Stream `GET /api/prints?id=ID` for the raw file. *(read)*
- `expose_control` — one single-exposure command (`command` + optional `value`)
  covering `start`/`stop`/`toggle`/`pause`/`resume`/`reset`, focus
  (`focus`/`focus_off`/`focus_toggle`), and settings (`set_time`,
  `adjust_seconds`, `adjust_stops`, `set_dry_down`, `adjust_dry_down`). The tool
  description reminds the assistant that `start` exposes real paper on the easel
  (and `focus` floods the enlarger lamp), so it confirms the user is ready before
  starting. *(control)*
- `strip_control` — one test-strip command covering `start`/`cancel` and the
  configuration setters (`set_base`, `adjust_base`, `step_up`, `step_down`,
  `set_segments`, `adjust_segments`, `set_countdown`, `adjust_countdown`,
  `set_pause`, `adjust_pause`, `set_tick`). Config commands are rejected while a
  sequence runs. The tool description explains that `start` runs a hands-on
  automated sequence (the user slides a mask to uncover the next strip during
  each beeped pause) issued **once**, not per segment, so the assistant confirms
  the setup before starting. *(control)*
- `meter_control` — one meter command covering sensor reads
  (`read_lref`/`read_bright`/`read_dark`), manual inputs
  (`set_lref`/`adjust_lref`/`set_zone5`/`adjust_zone5`), and magnification
  (`mag_measure_a`/`mag_measure_b`/`mag_clear`). The tool description tells the
  assistant to take the sensor reads **one at a time** — because the single
  physical probe must be repositioned between readings, it prompts the user for
  placement (bare bulb, brightest highlight, deepest shadow) and waits for
  confirmation before each read. *(control)*
- `relay_control` — switch the enlarger/safelight relay (`on` boolean or `state`
  `on`/`off`). The description cautions that switching the enlarger on floods the
  easel with light (use `expose_control`/`strip_control` for timed exposures). *(control)*
- `print_control` — star/unstar the most recently saved print
  (`toggle_star` / `set_star`). *(control)*
- `delete_print` — delete one saved print by id. *(control, destructive)*
- `delete_all_prints` — clear the whole print log (requires `confirm=true`).
  *(control, destructive)*
- `set_print` — edit any saved print's `notes` and/or `starred` flag by id
  (unlike `print_control`, which only stars the most recent print). *(authoring)*
- `set_relay_config` — create or replace the relay action configuration. *(authoring)*

> **Note:** Feature-rich device classes (Shutter Tester, Coffee Scale, Darkroom
> Timer) **require PSRAM** — a deliberate policy, enforced at compile time by a
> `#error` guard in `board_config.h`. Their scratch buffers (here: the print-log
> id list and record parsing) allocate with `MALLOC_CAP_SPIRAM` and intentionally
> have no internal-RAM fallback, since these classes only ship on PSRAM boards.

## Visually verifying the display

The assistant cannot see the panel directly, but it can capture exactly what is
on-screen through a browser. The device serves the live framebuffer at
`GET /api/screenshot` as a hardware-encoded JPEG by default on ESP32-P4 boards
and as a 24-bit BMP elsewhere. The image is image-only, so an assistant must
**not** fetch it as text — it renders the URL in a browser and captures the
image element instead. `?format=bmp|jpg` selects a format explicitly on P4;
`?quality=1..100` sets JPEG quality (default `85`).

With a Playwright-style browser tool (such as the one in VS Code), the recipe is:

1. If verifying a specific pad, call `set_screen("pad_N")` first so it is on-screen
   (this needs control tools enabled).
2. `page.goto("http://<device-ip>/api/screenshot")` — use `get_device_status` →
   `wifi.ip` for the address.
3. `page.waitForTimeout(1000)` — let the image load.
4. Capture the page with selector `img` — this crops out the browser chrome and
   returns just the device framebuffer, edge to edge.

If portal Basic Auth is enabled, embed credentials in the URL
(`http://user:pass@host/api/screenshot`); the MCP bearer token does not apply to
`/api/screenshot`. The MCP server also advertises this workflow to the model in
its `initialize` instructions and in `get_capabilities` (`visual_inspection`), so
a capable assistant can offer to verify UI work on its own.

The `initialize` response's `instructions` field additionally gives the model a
board-agnostic orientation to the firmware and the core **discover → act →
verify** tool-chaining (e.g. `list_pads`/`get_pad` before `press_button`,
`list_screens` before `set_screen`), so it understands what the device is and how
the read and control tools compose — even on headless boards with no display.
Specialized device classes prepend one sentence describing their **core use
case** (e.g. "a darkroom enlarger timer for black-and-white printing: meter
prints, run f-stop test strips, time exposures, log prints"), so the model knows
what the device is *for* before it reads the individual tool list.

## Example prompts

- "What's my device status?"
- "How much free memory does the device have?"
- "List the buttons on pad 0."
- "Press the **Lights** button."
- "Switch to the **info** screen."
- "Set the backlight to 40%."
- "Reboot the device."
- "Add a button to pad 0 that shows the time."
- "Show MQTT topic `home/temp` as a bar chart on pad 2."

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| `404` from `/mcp` | MCP is disabled, or the device is in setup/AP mode. Enable it in the portal while connected to WiFi. |
| `401` | Missing or wrong token, or no token has been generated yet. Generate a token and set the `Authorization: Bearer` header. |
| `403` | The request carried an `Origin` header (a browser tab). Native MCP clients send none and are accepted. Browser-based clients are not supported. |
| Control tools missing | The control toggle is off. Enable **Allow control tools** and save. |
| "control busy, retry" | Another control command is still running. Issue one control action at a time. |
| Can't resolve `.local` | Use the device's IP address in the URL instead. |
