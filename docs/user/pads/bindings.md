---
title: Bind fields to live values
description: Write binding tokens, choose formatting and fallbacks, preview values, and reuse named pad bindings.
---

Bindings replace fixed text or values with data when the device uses them.
A binding can show a temperature in a label, supply a widget's reading, or
choose a color from current state. Tokens identify the source, while optional
formatting and fallbacks control its presentation.

## Insert and verify a token

A binding is `[scheme:parameters]`. Fields marked **fx** support the schemes
reported by the running device. For colors, click the swatch to find fx and
configured integration shortcuts inside the color dialog. Open fx, search its
reference, then Copy/Insert. Mix tokens and ordinary text in labels:

```text
Solar [mqtt:home/solar;power;%.0f] W
[time:%H:%M]
```

Semicolons separate scheme parameters at their own bracket depth.
Nested tokens keep their parameters together. Schemes have different
parameters; do not copy one scheme's formatting position to another.

Syntax checks catch unbalanced brackets, unknown schemes/keys, parameter counts,
formats, and malformed expressions. They do not prove a topic exists or a path
returns the expected value. Metadata comes from the device; if loading it fails,
generic structural checks are not a substitute for device save validation.

## Preview and save

In the button editor, a bindable field shows its resolved value below the input
or color control when it contains a token. Colors can show a resolved swatch.
Long results are shortened to fit; hover over a result or focus it with the keyboard to read the
full value. Click a result, or press Enter or Space while it is focused, to
request a fresh reading. Preview returns current values, not final rendered
pixels.

For text and numeric fields, fx and configured integration shortcuts stay visible
together. In narrow fields, these controls move below the field name rather than
splitting across lines. Color fields keep their shortcuts inside the color dialog.

Pad-local named bindings use device pad context for previews; save changes to
those definitions first. Then Save Pad and inspect the actual display.
Missing data normally shows `---`; resolver errors can show `ERR:...`.
The label's format and the widget's numeric input are separate.

## Set a fallback

Use `[scheme:params|fallback]` to replace an unavailable result.
The pipe belongs at the token's outermost bracket depth:

```text
[mqtt:home/solar;power|0]
[pad:power|N/A]
[expr:[mqtt:room;temp]>30?"#FF4444":"#4CAF50"|#333333]
```

Choose fallbacks intentionally. Zero is convenient for presentation but is not
proof a sensor measured zero. Do not mask missing data in controls that rely on
an actual measurement. Invalid scheme syntax still needs correction.

## Format values

For schemes that support printf-style formatting, typical formats are `%.0f`,
`%.1f`, `%.2f`, `%d`, and `%s`. Escape a literal percent as `%%`.
For raw MQTT payload with formatting, leave the path empty:

```text
[mqtt:home/solar/power;;%.0f W]
[mqtt:room;humidity;%.0f%%]
```

Time and timer formats have their own language; see
[data sources](data-sources.md) and [timers](../device/timers.md).
Structured Table input must be one exact token, without a text prefix/suffix
or format parameter.

## Name a source on the pad

Open Pad **Bindings**, select Add, and enter a Name and Binding.
For example, name `power` with `[mqtt:home/solar;power]`.
Reference it as `[pad:power]`, `[pad:power;%.0f W]`, or inside an expression.

Names start with a letter and contain letters, digits, or underscores, up to
31 characters. A pad supports up to 16 names.
Avoid circular definitions and references that rely on a different pad's context.
Template bindings merge with local bindings; the local name wins.
Use globals, not `[pad:name]`, in shared device defaults or context-free hosts.

For calculations and conditional colors, continue to [expressions](expressions.md).

[Pad tasks](README.md) | [User guides](../README.md)
