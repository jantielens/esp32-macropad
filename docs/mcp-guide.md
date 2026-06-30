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
    READ --> INFO["status · health · screens<br/>pads · sensors"]
    CONTROL --> ACT["press_button · set_screen<br/>backlight · wake · system"]
```

**Read tools** (always available once enabled):

- `get_device_status` — firmware version, board, uptime, current screen, WiFi state.
- `get_health` — heap (internal/PSRAM), CPU, WiFi signal.
- `list_screens` / `get_current_screen` — available screens and the active one.
- `list_pads` / `get_pad` — configured pads and their buttons (so the assistant
  knows what it can press).
- `get_sensors` — current sensor readings (empty on boards without sensors).

**Control tools** (require the control toggle):

- `press_button` — press a pad button by position or label, exactly like a tap.
- `set_screen` — navigate to a screen.
- `set_backlight` / `wake` — adjust display brightness or cancel the screen saver.
- `system_command` — `reboot`, `wifi_reconnect`, or `screensaver`.

Display-related tools are present only on boards that have a display.

**Authoring tools** (require the pad authoring toggle; display boards only):

- `get_capabilities` — manifest of widget types + fields, button schema, label-style
  DSL, binding schemes (incl. `[pad:name]` and `template_pad`), and grid limits.
  Read-only, so it works with token alone.
- `get_pad_blocks` — list pre-built button groups (building blocks) that can be
  dropped onto a pad. Read-only.
- `validate_pad` — dry-run validate a pad JSON (grid bounds, span overflow,
  widget types, colors, binding tokens) without saving. Read-only.
- `set_button` / `set_buttons` — create or replace a button (or many in one save)
  by position, using the same schema as the portal pad editor.
- `set_pad` — set pad-level fields (layout, cols/rows, wake_screen, bg_color,
  `template_pad`, and named `[pad:name]` bindings) without touching buttons.
- `remove_button` / `clear_pad` — delete one button or empty a pad.

Writes are validated before saving and persisted on the main loop. Concurrent
edits from the LLM and the portal editor are **last-write-wins per pad** — the
last save replaces the pad, so avoid editing the same pad in both at once.

## Visually verifying the display

The assistant cannot see the panel directly, but it can capture exactly what is
on-screen through a browser. The device serves the live framebuffer at
`GET /api/screenshot` as a 24-bit BMP. The image is large and image-only, so an
assistant must **not** fetch it as text — it renders the URL in a browser and
captures the image element instead.

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
