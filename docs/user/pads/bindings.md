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
reported by the running device. The compact wand button opens the Binding
Builder; **fx** opens the separate Binding Reference. Color fields show these
controls only inside the color dialog; click the swatch to open it. Open fx, search its
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

## Build a token from a source

Choose the wand button beside the field you want to fill. In the Binding
Builder, choose **On-device bindings**, **Home Assistant**, or **MQTT**.
They share the same source picker and display options.
Select a source using the options above the search or topic
input. Unconfigured integrations are muted and marked **Setup needed**;
select one to see its configuration link. Integrations unsupported by the
firmware are hidden. Local device values do not need an integration configured.

* **Home Assistant** searches entities or loads an exact entity ID, then offers
  state, attributes, or a custom path. Follow the
  [HA lookup workflow](../integrations/home-assistant.md#build-an-ha-binding)
  for token permissions and unavailable states.
* **MQTT** inspects one exact topic after you configure a broker. Choose
  **Read topic**, wait for a received message, then click the raw payload
  or a suggested scalar field. A scalar payload is selected automatically.
  There is no wildcard or broker-wide discovery.
  No message is different from an empty payload or zero. Failure or timeout
  leaves you in source selection; retry or edit the topic. Cached received
  messages are identified. See [MQTT data](../integrations/mqtt.md#feed-labels-and-widgets).
* In **On-device bindings**, type at least three characters of a key or description
  and click a result. Complete any required parameters. Static local values,
  including device-class values and listed format presets, supply declared
  examples without reading the device. Complete catalog values go straight to
  customization; only missing required parameters need **Continue**.

After selection, the picker shows **Sample value**. Device examples are marked
**(example)**; HA and MQTT use the value acquired during selection. Examples
are illustrative, not current readings, and are never saved as runtime data.
**Display options** contains supported formatting, unit text, and
**When unavailable**. Numeric values default to **Number**, or **Whole number**
for integer-only bindings where supported. Values with fixed numeric output
or duration-specific syntax retain their supported native formatting.
Other values use **As received**. **Insert binding** fills the
originating field; **Copy binding** copies the exact generated text shown in
the preview. **Edit value** returns to source selection or the selected catalog
parameters. Raw-value widget fields omit display formatting and units.

For expressions, named pad bindings, or other advanced syntax, search the scheme
in **On-device bindings** or enter a complete token. Complete or edit its
registered parameters, then choose **Continue** to customize and preview the
binding. Use **Edit value** to adjust optional catalog parameters later.
Both builders use declared samples or the HA/MQTT value acquired during source
selection, without an extra device-preview request. When no sample is available,
you can still copy or insert a valid binding. Parameter inputs do not infer types
or units from a sample. Check named pad bindings in the button editor using saved
definitions, not unsaved edits.

MQTT units start empty and are display text, not conversion. Inspection can
expose up to 2,047 payload bytes, but a selected MQTT value must fit the
resolver's 127-byte output. Truncated payloads do not supply JSON suggestions
or numeric evidence; the builder explains output limits rather than promising
a complete sample. Topic input is limited to 127 UTF-8 bytes, scalar paths to
47, and generated tokens and labels to 191. Use shorter values when needed.
Closing or changing the source releases temporary inspection; abandoned handles
expire after 30 seconds. A subscribe request being accepted is not proof the
broker acknowledged it.

The reference and builder are independent dialogs: close one and use the other
field control to open the other. **Cancel**, the close button, and Escape leave
the field unchanged. **Insert binding** updates the field, but you still need
to save through the editor. The reference and manual input remain available.
To create labels and a widget
together instead, use the
[Button Builder](editing.md#create-a-button-from-a-source). It creates only an
unsaved draft. Catalog entries without explicit numeric authoring metadata
offer value labels only, even if a preview looks numeric. Numeric scenarios
also need suitable samples and runtime widgets.
Local labels and live history work on display firmware without HA or MQTT;
history starts collecting while the device runs, not from an invented backfill.

## Preview and save

In the button editor, a bindable field shows its resolved value below the input
or color control when it contains a token. Colors can show a resolved swatch.
Long results are shortened to fit; hover over a result or focus it with the keyboard to read the
full value. Click a result, or press Enter or Space while it is focused, to
request a fresh reading. Preview returns current values, not final rendered
pixels.

For text and numeric fields, fx and the wand button stay visible
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
