# Brew Template Guide

Brew templates define the step-by-step flow of a brew session on the macropad. Each template is a sequence of **stages** — the device walks you through them one at a time, showing instructions, auto-advancing when conditions are met, and capturing measurements along the way.

The firmware ships with three built-in templates (Free Pour, V60, James Rao V60). You can also create and upload your own **custom templates** using a simple JSON format.

## Quick Start

1. Open your device's web portal and navigate to the **Brews** page
2. Scroll down to the **Brew Templates** section
3. Click a template card to see its stages visualized
4. To add your own: create a `.json` file following the format below, then click **Upload Template**

## Template JSON Format

Every template is a single JSON file with this structure:

```json
{
  "v": 1,
  "name": "my_template",
  "display_name": "My Template",
  "description": "A short description of this brew method",
  "start_label": "Start Brew",
  "done_label": "Brew Again",
  "stages": [
    { ... },
    { ... }
  ]
}
```

### Top-Level Fields

| Field | Required | Max Length | Description |
|-------|----------|-----------|-------------|
| `v` | Yes | — | Schema version. Must be `1`. |
| `name` | Yes | 23 chars | Machine name (lowercase, underscores). Used as filename and lookup key. |
| `display_name` | No | 47 chars | Human-friendly name shown in the UI. Falls back to `name` if omitted. |
| `description` | No | 127 chars | Short description shown on the template detail page. |
| `start_label` | No | 47 chars | Button label shown when idle (before starting a brew). Example: `"Start V60"`. |
| `done_label` | No | 47 chars | Button label shown after brew is done (restart prompt). Example: `"Brew Again"`. |
| `stages` | Yes | Max 16 | Array of stage objects (at least one required). |

## Stages

Each stage represents one step in the brew flow. The device shows the stage name, instruction text, and an advance button. Stages advance in order — either manually (user taps the button) or automatically.

```json
{
  "name": "Bloom",
  "instruction": "Pour to 60g, then swirl gently",
  "next_label": "Blooming...",
  "type": "auto_time",
  "auto_time_s": 45,
  "on_enter": ["beep"],
  "on_exit": ["capture_weight"],
  "target_weight": 60.0,
  "target_flow_rate": 6.0,
  "capture": {
    "key": "bloom_water",
    "label": "Bloom Water",
    "unit": "g"
  }
}
```

### Stage Fields

| Field | Required | Max Length | Description |
|-------|----------|-----------|-------------|
| `name` | Yes | 23 chars | Stage display name, shown via `[brew:stage]` binding. |
| `instruction` | No | 127 chars | Guidance text shown to the user. Supports `[brew:...]` binding tokens. |
| `next_label` | No | 47 chars | Label on the advance button for this stage. |
| `type` | Yes | — | Stage advance behavior (see [Stage Types](#stage-types) below). |
| `on_enter` | No | — | Array of [effect](#effects) names fired when entering this stage. |
| `on_exit` | No | — | Array of [effect](#effects) names fired when leaving this stage. |
| `auto_threshold` | No | — | Weight threshold in grams for `auto_weight` stages. |
| `target_weight` | No | — | Guidance target weight (grams) displayed to the user. |
| `target_flow_rate` | No | — | Guidance flow rate target (g/s) displayed to the user. |
| `auto_time_s` | No | — | Duration in **seconds** for `auto_time` stages. |
| `capture` | No | — | Capture object for recording a measurement (see [Capture](#capture) below). |

## Stage Types

The `type` field controls how a stage advances to the next one:

### `manual`

The user taps the advance button to move to the next stage. Use this for steps that require human judgment ("tap Done when finished").

```json
{
  "name": "Brewing",
  "type": "manual",
  "instruction": "Pour steadily, tap Done when finished",
  "next_label": "Done"
}
```

### `auto_weight`

Automatically advances when the scale reading exceeds `auto_threshold` grams above the tared value. Typically used for "arm and pour" stages — the stage sits armed, then auto-advances the moment water hits the scale, starting the timer and recording.

```json
{
  "name": "Arm pour",
  "type": "auto_weight",
  "on_enter": ["tare"],
  "auto_threshold": 2.0,
  "next_label": "Armed"
}
```

### `auto_time`

Automatically advances after `auto_time_s` seconds. The countdown is shown via the `[brew:stage_time_remaining]` binding. Use this for bloom stages, steep times, or any timed wait.

```json
{
  "name": "Bloom",
  "type": "auto_time",
  "auto_time_s": 45,
  "instruction": "[brew:stage_time_remaining]s remaining",
  "next_label": "Blooming..."
}
```

## Effects

Effects are side-actions that fire when entering or exiting a stage. Specify them as string arrays in `on_enter` and `on_exit`.

| Effect | Description |
|--------|-------------|
| `tare` | Zero the scale. Commonly used on enter to reset the weight reading. |
| `beep` | Play a short audio cue to alert the user. |
| `capture_dose` | Capture the current weight into the built-in "dose" slot. Used at the end of a dosing stage. |
| `capture_weight` | Capture the current weight into a named slot (requires a `capture` object on the same stage). |
| `marker` | Emit a named marker into the brew series timeline. |

Effects can be combined:

```json
"on_enter": ["tare", "beep"],
"on_exit": ["capture_dose"]
```

## Capture

The `capture` object lets you record a named measurement at a specific point in the brew. It works together with the `capture_weight` effect — when the effect fires (on enter or exit), the current scale weight is saved under the given key.

```json
"on_exit": ["capture_weight"],
"capture": {
  "key": "bloom_water",
  "label": "Bloom Water",
  "unit": "g"
}
```

| Field | Max Length | Description |
|-------|-----------|-------------|
| `key` | 15 chars | Machine key for the captured value (used in brew log data). |
| `label` | 23 chars | Human-readable label shown in the UI and brew log. |
| `unit` | 7 chars | Unit string (e.g. `"g"`, `"ml"`). |

The built-in `capture_dose` effect is a shortcut that captures to the special "dose" slot without needing a `capture` object.

## Binding Tokens in Instructions

The `instruction` field supports `[brew:...]` binding tokens that are resolved live during a brew. These let you show dynamic values:

| Token | Description |
|-------|-------------|
| `[brew:stage_weight_target]` | The `target_weight` of the current stage. |
| `[brew:stage_time_remaining]` | Seconds remaining for `auto_time` stages. |

Example:

```json
"instruction": "Pour to [brew:stage_weight_target]g, then swirl. [brew:stage_time_remaining]s remaining"
```

## Built-in Templates

The firmware includes three built-in templates that cannot be deleted (but can be overridden by uploading a custom template with the same `name`):

### Free Pour (`free_pour`)

Simplest template — two stages:

1. **Ready** (`auto_weight`) — Tares the scale, auto-advances when pour is detected (>2g)
2. **Brewing** (`manual`) — Records until the user taps Done

### V60 Pour-Over (`v60`)

Five-stage V60 flow:

1. **Place cup** (`manual`) — Place empty dosing cup on scale
2. **Dosing** (`manual`) — Tares, then captures dose weight on exit
3. **Prep cup** (`manual`) — Grind, prep, and place V60 on scale
4. **Ready** (`auto_weight`) — Tares, arms for pour detection
5. **Brewing** (`manual`) — Records until Done

### James Rao V60 (`rao_v60`)

Six-stage recipe with bloom and targets:

1. **Place cup** (`manual`) — Place dosing cup
2. **Dose beans** (`manual`) — Tare, dose to 16g target, capture dose
3. **Prep** (`manual`) — Grind, rinse filter, tare
4. **Arm pour** (`auto_weight`) — Tare, arm with 2g threshold
5. **Bloom** (`auto_time`, 45s) — Beep on enter, 60g target, 6 g/s flow, capture bloom water on exit
6. **Main pour** (`manual`) — Beep on enter, 250g target, 5 g/s flow

## Complete Example

Here's a full template for a simple AeroPress brew:

```json
{
  "v": 1,
  "name": "aeropress",
  "display_name": "AeroPress",
  "description": "Standard AeroPress brew with inverted method",
  "start_label": "Start AeroPress",
  "done_label": "Brew Again",
  "stages": [
    {
      "name": "Dose",
      "instruction": "Add 15g of coffee to the inverted AeroPress",
      "next_label": "Log dose",
      "type": "manual",
      "on_enter": ["tare"],
      "on_exit": ["capture_dose"],
      "target_weight": 15.0
    },
    {
      "name": "Add water",
      "instruction": "Place AeroPress on scale and pour to [brew:stage_weight_target]g",
      "next_label": "Start timer",
      "type": "manual",
      "on_enter": ["tare", "beep"],
      "target_weight": 200.0,
      "target_flow_rate": 10.0
    },
    {
      "name": "Steep",
      "instruction": "Stir gently, then wait. [brew:stage_time_remaining]s remaining",
      "next_label": "Steeping...",
      "type": "auto_time",
      "auto_time_s": 60,
      "on_enter": ["beep"]
    },
    {
      "name": "Press",
      "instruction": "Flip and press slowly. Tap Done when finished.",
      "next_label": "Done",
      "type": "manual",
      "on_enter": ["beep"]
    }
  ]
}
```

## Managing Templates

### Web Portal

The **Brews** page has a **Brew Templates** section at the bottom:

- **View**: Click any template card to see its stages visualized in a timeline
- **Upload**: Click "Upload Template" and select a `.json` file
- **Download**: Click the download button on any template card to save its JSON
- **Delete**: Click the delete button on custom templates (built-in templates cannot be deleted)

If you upload a template with the same `name` as a built-in, your custom version overrides it. Deleting that custom version restores the built-in.

### REST API

Templates can also be managed programmatically:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/brew-templates` | List all templates (name, display_name, description, is_dynamic, stage_count) |
| GET | `/api/brew-templates/get?name=xxx` | Download a single template as full JSON |
| POST | `/api/brew-templates` | Upload a template (JSON body, max 8KB) |
| DELETE | `/api/brew-templates?name=xxx` | Delete a custom template |

### Storage

Custom templates are stored on the device's LittleFS filesystem at `/config/brew_templates/<name>.json`. The device supports up to 8 registered templates total (built-in + custom).

## Validation Rules

The parser enforces these rules when uploading a template:

- `v` must equal `1`
- `name` is required and must be non-empty
- `stages` must be a non-empty array (max 16 stages)
- Every stage must have a `name` and a valid `type` (`manual`, `auto_weight`, or `auto_time`)
- Template file size must not exceed 8KB
- Template `name` must not contain `/`, `\`, or `..`

Unknown fields are silently ignored, which allows forward compatibility if new fields are added in future schema versions.
