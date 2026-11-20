---
title: Structured Table
description: Display a structured table source with exact-token binding, typography, and optional scrolling.
---

A Table presents rows and columns supplied by a structured JSON binding source.
For local device health, use the [List widget](list.md#display-device-health)
instead.

## Display structured data

1. Select Table in Button Content.
2. Set Data binding to a source that supplies the Table payload contract,
   for example `[mqtt:devices/table;.]` for a compatible MQTT payload.
3. Give the button enough span for the rows/columns.
4. Optionally set Font style override and Scroll.
5. Save Pad and inspect the device.

The entire input must be one token with no text prefix/suffix and no format
parameter. For example, `Status: [mqtt:devices/table;.]` is not a table input even if
generic token validation accepts it.
The source must return the structured table contract, not arbitrary JSON.
There is no built-in health Table source.

## Style and scroll

Font style uses the [label language](../pads/appearance.md#use-label-style-syntax),
such as `font_size:14` or `font:my-font;font_size:24`.
Empty inherits the button's label size.
Uploaded fonts have the normal [trust/memory limits](../media/fonts.md).

Scroll defaults on; turn it off for a fixed visible subset.
An optional `[mqtt:devices/table;.|{}]` startup fallback is an empty payload, not
fabricated measurements.
Do not use ordinary numeric formatting on structured rows.

[Widget chooser](README.md) | [User guides](../README.md)
