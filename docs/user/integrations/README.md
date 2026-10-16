---
title: Connections and automation
description: Choose HID, MQTT, Home Assistant, or an AI assistant connection without confusing their credentials or permissions.
---

Integrations connect the device to a computer, messaging service, home-automation
system, or AI assistant. These connections serve different purposes and can be
enabled independently; choose the path for the data or controls you need.

* [Keyboard, mouse, and gamepad](keyboard-mouse-gamepad.md) sends input to a host.
* [MQTT](mqtt.md) publishes telemetry and feeds pad bindings/actions.
* [Home Assistant setup](home-assistant.md) separates broker/discovery from direct
  service calls and history access.
* [Home Assistant controls](home-assistant-control.md) covers notifications,
  audio, screens, and camera entities.
* [AI assistants through MCP](ai-assistants.md) inspect, control, and author with
  separate permissions.

Portal login, broker credentials, HA tokens, and MCP tokens are independent.
Use trusted networks and grant only the access a client needs.
Remote commands can operate real controls or replace unsaved/saved work.
Check [action behavior](../pads/actions.md) before automating it.

[Examples](../examples/README.md) | [User guides](../README.md)
