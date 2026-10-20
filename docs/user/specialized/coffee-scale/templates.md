---
title: Author a brew template
description: Write the complete brew-template JSON language, choose automatic transitions, and upload safely.
---

A brew template defines the stages, prompts, targets, and recorded values of a
brewing workflow. A user-authored JSON template lets you adapt that workflow,
including when a stage advances manually, by weight, or by elapsed time.

Open Brew Templates to inspect stages/Raw JSON or Download a template.
Finish any active brew before Upload Template, reload, or deletion.
Use a distinct internal name: a custom template with a built-in name overrides
it; deleting that override restores the built-in.

## Start with a small template

Save as a JSON file and upload:

```json
{
  "v": 1,
  "name": "manual_pour",
  "display_name": "Manual pour",
  "description": "Start on first pour, then finish manually.",
  "start_label": "Prepare",
  "done_label": "Finished",
  "idle_instruction": "Place the assembled vessel on the scale.",
  "done_instruction": "Remove the vessel.",
  "stages": [
    {
      "name": "wait",
      "type": "auto_weight",
      "instruction": "Pour to start.",
      "auto_threshold": 2,
      "on_enter": ["tare"]
    },
    {
      "name": "pour",
      "type": "manual",
      "instruction": "Pour, then finish.",
      "next_label": "Finish",
      "target_weight": 250,
      "on_exit": ["capture_weight"],
      "capture": {"key": "final", "label": "Final mass", "unit": "g"}
    }
  ]
}
```

Confirm it appears in the list, inspect the parsed stages, select it, and run a
water-only trial before relying on measurements.
Do not expect uploading JSON to start a brew.

## Define stages and effects

The version field is `"v": 1`. Name and nonempty stages are required; at most
16 stages.
display_name, description, start_label, done_label, idle_instruction, and
done_instruction are optional.

| Stage field                          | Meaning                                                     |
| ------------------------------------ | ----------------------------------------------------------- |
| name                                 | Required internal stage name                                |
| type                                 | `manual`, `auto_weight`, or `auto_time`                     |
| instruction / next_label             | Prompt and manual advance caption                           |
| auto_threshold                       | Grams triggering first-pour start/advance for `auto_weight` |
| target_time_s                        | Whole seconds; auto-advances only `auto_time` stages        |
| target_weight                        | Advisory cumulative mass in grams                           |
| target_flow_rate                     | Advisory grams/second                                       |
| on_enter / on_exit                   | Arrays of named effects                                     |
| capture                              | key, label, unit for `capture_weight`                       |
| beep_pattern                         | Stage effect tone pattern                                   |
| countdown_beep / countdown_done_beep | Timed-stage warning/completion patterns                     |
| weight_cue_g / weight_cue_times      | Cue spacing and number of cues before weight target         |
| weight_cue_beep / weight_done_beep   | Cue/weight-reached patterns                                 |

Use a positive operational threshold/time for automatic stages.
target_time_s may be 0 but then cannot trigger an auto-time transition;
its maximum is 4294967 seconds.
On manual/weight stages, a time target is guidance rather than auto-advance.
Weight/flow targets do not automatically finish an auto-time stage.
Weight cue spacing 0 disables cues; default cue count is 1.
With count N and spacing G, cues approach the target at N*G through G grams
remaining; the done cue occurs at/above target.

Effects are `tare`, `capture_dose`, `capture_weight`, `beep`, and `marker`.
They form an effect set, not a guaranteed array execution order.
Use separate stages when order matters. Capture weight needs a capture
definition. Up to eight captured values and sixteen markers are retained.
Stage transitions can also create markers while the timer runs.
Patterns use the [tone language](../../media/audio.md#write-a-tone-pattern).

## Keep strings within their limits

Limits are UTF-8 bytes, not visible characters:

* Template/stage name: 23; display name: 47; description: 127.
* Start/done/next labels and beep-pattern fields: 47.
* Idle/done/stage instructions: 127.
* Capture key: 15; label: 23; unit: 7.

Long fields can truncate rather than reject; keep identifiers short and unique.
After every upload, inspect the stored template and test transitions/cues.
A missing selection can fall back to Free Pour after registry changes.
Keep your authored file separately from [brew-log exports](README.md#review-and-preserve-records).

[Coffee Scale](README.md) | [User guides](../../README.md)
