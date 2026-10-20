---
title: Connect an AI assistant through MCP
description: Enable authenticated local MCP access, configure clients, confirm device identity, and verify safe control and authoring.
---

MCP (Model Context Protocol) lets a compatible AI assistant inspect the device,
operate supported controls, and author pads. The device exposes its available
tools through an authenticated local endpoint, with separate permissions for
reading, control, and editing.

## Enable only the permissions you need

Open **Connectivity > MCP Server** on the normal Wi-Fi network.
Enable the server, Generate new token, and copy it immediately.
Generation replaces the previous token; it cannot be retrieved later.
Save. Settings apply without reboot.

Read access needs the enabled server/token. Allow control tools separately
permits device operations; Allow pad authoring separately permits edits.
Availability still depends on device capabilities.
MCP is off by default and unavailable in setup/AP mode.

> [!WARNING]
> MCP uses plain LAN HTTP, not TLS. Its dedicated bearer token is separate from
> portal login and can authorize physical effects or persistent changes.
> Keep it private, restrict access to a trusted network, and disable permissions
> when not needed. Never port-forward the endpoint to the internet.

## Connect an HTTP client

Copy the endpoint from the portal, normally `http://HOSTNAME.local/mcp`.
Use the device IP if mDNS fails.
Every request needs `Authorization: Bearer YOUR_MCP_TOKEN`.
For VS Code, add an HTTP server to the MCP configuration:

```json
{
  "servers": {
    "my-device": {
      "type": "http",
      "url": "http://DEVICE_IP/mcp",
      "headers": {"Authorization": "Bearer YOUR_MCP_TOKEN"}
    }
  }
}
```

Use your client's secure input/secret storage rather than committing a real
token into a project. Other HTTP-capable clients use the same URL/header.
Browser-origin requests are refused; a webpage is not an authorized MCP client.

For a stdio-only client, a trusted HTTP bridge such as mcp-remote can be
configured as:

```json
{
  "mcpServers": {
    "my-device": {
      "command": "npx",
      "args": ["mcp-remote", "http://DEVICE_IP/mcp",
               "--header", "Authorization: Bearer YOUR_MCP_TOKEN"]
    }
  }
}
```

Review and install a bridge according to your client/security policy.
This example is configuration, not a requirement to install it on the device.

## Confirm the physical target

Start with get_identity. Compare device name and immutable device_id to the
device you intend to change; an assistant's namespace/alias is not identity.
Protected writes require expected_device_id copied from that identity.
Do not retry an identity mismatch by blindly accepting whichever ID was returned.

Ask the assistant to discover capabilities/screens/pads before acting.
For example: "Inspect this device, confirm its identity, then add one clock
button without replacing other buttons."
Use live metadata, not a handwritten schema copied from another firmware.
Read component settings before editing: component writes replace the full
configuration. Portal and assistant pad saves are last-write-wins;
avoid simultaneous editing and [back up](../device/backup-and-restore.md) first.

## Verify effects

After a write, read back the affected pad/configuration and resolve its bindings.
Accepted/queued means admitted, not necessarily executed or durably saved.
For eligible Home Assistant actions, press_button returns an execution ID;
use get_ha_execution_result until terminal, then inspect every action result.
Records are short-lived. Lists using confirmations follow normal continuations
instead and do not provide that tracked completion path.

Inspect a fresh framebuffer through the authenticated portal
`/api/screenshot` in an image-capable browser, not as text.
Portal Basic Auth, when enabled, protects it separately from the MCP token.
A screenshot does not prove the physical panel updated.
For tap_screen, choose native pixel coordinates from a fresh image.
Success queues one tap; screen changes, wake, or physical contact can delay it.
It is not drag/swipe/hold input and does not bypass confirmation.

For fonts, sensors, alarms, or instruments, use their owning
[user topics](../README.md) and the server's current tool descriptions.
If tools are missing, check permissions/capabilities. HTTP 401 means token
failure; 404 can mean disabled/AP mode; 403 can mean an Origin-bearing client.
Serialize control requests if the device reports busy.

The `system_command` control also accepts `reboot_ap`. It restarts the device
into the setup AP and drops the current MCP session. Saved Wi-Fi settings and
the AP password are preserved. The AP credentials are shown on the device
during setup.

[Integration tasks](README.md) | [User guides](../README.md)
