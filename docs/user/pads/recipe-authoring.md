---
title: Author a recipe catalog
description: Write the user-facing catalog language, define fixed or adaptive layouts, substitute parameters, and provision safely.
---

A recipe catalog is a collection of reusable pad setups that users can install
and adapt. Each recipe describes a button layout, any inputs the user should
supply, and optional setup of related features.

**Pads > Recipe Catalog** accepts one complete JSON document, up to 64 KiB.
Saving replaces the device catalog; preserve your source copy.
This is the recipe authoring language, not a full-device configuration format.
Recipe `layout.buttons` entries install into Main. Existing Overlay buttons and
shared pad settings remain unchanged, and the device's combined Main and
Overlay button limit still applies.

## Start with a complete recipe

```json
{
  "schema": 1,
  "catalog_version": "my-catalog-1",
  "recipes": [{
    "id": "hello",
    "name": "Hello button",
    "description": "Show a configurable notification.",
    "requires": ["display"],
    "parameters": [{
      "id": "message",
      "label": "Message",
      "type": "string",
      "default": "Hello"
    }],
    "layout": {
      "buttons": [{
        "col_offset": 0,
        "row_offset": 0,
        "label_center": "Hello",
        "actions": [{
          "type": "notify",
          "message": "${message}",
          "duration_ms": 3000
        }]
      }]
    },
    "post_install": {"message": "Tap the saved button to test it."}
  }]
}
```

Each recipe needs a unique lowercase letter/digit/hyphen ID beginning with a
letter or digit, nonempty name/description, and a nonempty layout.buttons array.
Buttons use the running device's supported button/action fields. Recipe
positions and overlap checks apply to Main; an Overlay button may share their
grid coordinates.
Use its live authoring metadata or adapt the
[complete samples](../../samples/recipe-catalog.json), not an unrelated
firmware's field catalog.

## Choose layout syntax

A fixed layout uses nonnegative whole-number col_offset/row_offset per button,
with optional positive col_span/row_span. Buttons cannot overlap and the
footprint must fit within 8 x 8 and the destination device's actual limits.

An adaptive layout instead sets layout.flow to `row` or `column`.
Every button specifies shape `square`, `wide`, `tall`, `very_wide`, or `very_tall`,
and size `S`, `M`, or `L`; omit fixed positions/spans.
Packing follows declaration order from the selected anchor and considers
rendered button aspect ratio. A square is not necessarily equal row/column spans.
Adaptive recipes need an existing pad to know its grid.

## Substitute parameters

Each parameter needs a unique lowercase ID starting with a letter, then letters,
digits, or underscores; a label; type `string` or `number`; and a default.
An optional description supplies guidance.
`${id}` alone preserves the parameter's type. Embedded `${id}` becomes text.
Unknown references fail installation.
`${target_pad}` is reserved and expands to the chosen screen ID.

## Declare prerequisites and fonts

Optional requires entries are `display`, `mqtt`, `audio`, `alarm`,
`sound_player`, and `ttf_fonts`. Unsupported recipes cannot install.
Optional fonts is an array of objects with name and url: IDs use 1-31 lowercase
letters/digits/underscores/hyphens; URLs must be HTTPS without embedded credentials.
IDs must be unique. Trust and licensing still matter; downloading is not a
font sandbox. Existing matching font IDs can be reused.

## Provision settings deliberately

Optional provision.pad.bindings maps names to binding strings.
Names begin with a lowercase letter and contain lowercase letters, digits, or
underscores. An existing different value for the same name rejects installation.
Pad provisioning supports only named bindings.

Optional provision.components maps component IDs to config patches.
Objects merge recursively; arrays replace, not append.
For example, replacing an alarm hook list removes the old list.
Use the running feature's current contract and state, not a speculative JSON
shape. Explain changes in a nonempty confirmation string before destructive
provisioning. The samples show timer/alarm component use.

Optional post_install requires a nonempty message and can include links with
nonempty label and an internal fragment ID, such as `alarm-schedule`.
It appears only on successful completion; it is not a rollback hook.

## Verify installation

Save the catalog, install into a disposable empty area, test parameter expansion,
and inspect every affected setting. Pad save precedes component writes, so
failure can leave partial changes. Re-run checks for footprint, prerequisites,
font loading, and bindings on each destination device.
See [installation and failure handling](recipes.md).

[Pad tasks](README.md) | [User guides](../README.md)
