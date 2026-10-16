---
title: List items and selection actions
description: Choose a provider, resolve its selected item, and apply the complete include/exclude filter language.
---

A List displays selectable items supplied by a device feature, such as saved
pads or instrument test definitions. Selecting an item makes its ID available
to configured actions, so one control can operate on several choices.

## Choose a provider and action

Select List in Button Content. Data Binding is a provider ID, not an MQTT
token. For a pad chooser, use `pads`.
Set its tap action to Navigate to screen, Target Screen **Binding value**:

```text
[list:pads.selected]
```

Save Pad, enter it on the device, and tap an item.
The item supplies an ID, not just its visible label.
Tap/hold dispatches the corresponding button action list once; parent button
handlers do not run a second time. Without actions, selection has no external
effect.

Providers refresh on screen entry. Source not found means the provider is
unavailable in this firmware; No items means it returned an empty list.
Use the live Binding reference for installed providers.

## Filter items

Filter is plain comma-separated text, not a binding template:

| Rule         | Match                                           |
| ------------ | ----------------------------------------------- |
| Empty or `*` | Everything                                      |
| `pad_3`      | Exact ID or label, case-insensitive             |
| `pad_*`      | Glob; `*` means any sequence, `?` one character |
| `*Home*`     | ID/label containing Home                        |
| `#5`         | Original provider index 5, zero-based           |
| `#0-3`       | Inclusive original indices 0 through 3          |
| `!rule`      | Exclude any matching rule                       |

An item needs any positive match and no exclusion match.
With only exclusions, items start included.
Use `#0,#2,#4` for separate indices, not a comma-list after one `#`.
Examples: `pad_*,!pad_5`, `*Home*,*Office*`, or `!pad_0`.
Filtering does not renumber the original indices.

## Use the selected value carefully

`[list:provider.selected]` is the last selection for that provider.
Before selection it is unavailable. Selection state is shared per provider,
not private to every widget using that provider.
A selection-triggered action has the intended current value; do not assume a
boot/MQTT event selects a list item for you.

Specialized providers include brew templates and
[shutter test scripts](../specialized/shutter-tester/sessions.md).
Their IDs belong in the action's appropriate value field.

[Widget chooser](README.md) | [User guides](../README.md)
