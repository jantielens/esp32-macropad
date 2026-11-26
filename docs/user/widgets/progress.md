---
title: Progress
description: Show a numeric value as ordered units with configurable layout, appearance, and transitions.
---

Progress shows a numeric value as an ordered field of units. It is a display,
not a timer: a binding or another source supplies the value. Unit count is
independent of the minimum and maximum, so a countdown of 900 seconds can use
25 units, while a year can use 365 or 366.

Progress is available when the firmware enables it. A build with Progress
disabled cannot display existing Progress widgets and rejects saves of pads
that still contain them. Back up or update those pads before switching builds.

## Configure Progress

1. Select a button in **Pads**, then choose **Progress** in **Content**.
2. Set **Current value**, **Maximum / goal**, and **Unit count**. Each accepts
   a literal number or a binding. The minimum defaults to 0; you can change it
   under **Range and interpretation**. The maximum must exceed the minimum,
   and unit count must be a whole number from 1 to 1024.
3. Optionally open **Sample looks**, select a look, and confirm. This replaces
   visual settings, including color bindings, but keeps the value source,
   range, inversion, and unit count.
4. Open the tuning groups to adjust units, appearance, fill and order, and
   animation. Each reset icon restores its individual default; Auto adapts
   geometry, color, or treatment.
5. Select **Save Pad**. No reboot is needed. The editor grid shows a widget
   symbol rather than a live animation. Button labels, icons, and ordinary tap
   and hold actions remain available.

For example, enter `[mqtt:work/progress]` as the current value, `60` as the
maximum, and `60` as the unit count to show sixty successive units. Bind both
maximum and count to the same year-length source when the number of days can
change. A named pad binding can shorten longer expressions. For clock minutes,
`[time:%M]` with maximum and count `60` shows completed minutes (0–59);
`[expr:[time:%M]+1]` instead highlights the current minute (1–60).

Unavailable or invalid values, an invalid range, insufficient space, and
allocation failures are reported instead of appearing as zero. Increase the
widget region or reduce spacing if the configured units do not fit. The unit
count is never silently reduced; available space and memory may limit practical
counts below 1024. A valid source resumes the display without
replaying missed transitions. Explicit binding fallbacks are treated as valid
values when they resolve successfully.

## Range and interpretation

* **Current value**, **Minimum**, and **Maximum / goal** accept finite numbers
  or bindings. Defaults are 0, 0, and 100. Values outside the range are clamped.
* **Unit count** accepts a whole number or binding, defaults to 25, and is
  independent of the range. The supported limit is 1–1024, subject to space and
  memory.
* **Invert progress** reverses the mapping: the minimum fills all units and the
  maximum empties them.

## Units and layout

* **Arrangement** places units in a grid, horizontal strip, or vertical stack.
  **Columns** applies to grids: choose Auto or a whole number from 1 to 1024.
* **Shape** selects circles or blocks. For blocks in a grid, **Block width /
  height** sets the width-to-height ratio from 0.1 to 10; 1 is square.
  **Corner rounding** ranges from 0% for square corners to 50% for rounded ends.
* **Strip thickness** controls the cross-axis size of block strips: Auto or
  1–100%. The setting does not affect circles.
* **Horizontal spacing** and **Vertical spacing** are Auto or 0–256 pixels
  between units. **Match horizontal and vertical spacing** copies the horizontal
  setting to the vertical setting. With matching off and both spacings Auto,
  units spread independently across the available width and height. With either
  spacing numeric, that spacing stays fixed and the other Auto spacing uses the
  standard adaptive gap.

## Unit appearance

* **Active color** and **Inactive color** accept a color or binding. Auto uses
  the button foreground for active units and a muted foreground or panel ink
  for inactive units.
* **Inactive treatment** chooses Auto, Filled, Outline, or Hidden. Auto uses
  outlines in one-bit mode and filled units otherwise. Hidden leaves empty
  positions without moving other units.
* **Inactive size** is 1–100% of a full unit. It also influences the starting
  size of a partially active unit when partial mode is Size.

## Fill and order

* **Partial unit** chooses Whole, Size, or Fill. Whole activates only complete
  units; Size scales a partial unit; Fill reveals part of its shape.
* **Activation pattern** orders units by rows, columns, alternating snake rows
  or columns, inward or outward spiral, center outward, edges inward, diagonal,
  or random. Decreases reverse the order. **Starting corner** orients the
  pattern. **Reshuffle** chooses a new repeatable random order.
* **Within-unit fill direction** controls partial Fill and the Wipe effect.
  Choose left, right, up, or down; Auto follows the arrangement and starting
  corner. This is separate from the activation pattern.

## Animation

**Effect** chooses None, Fade, Grow, Pop, Wipe, or Flip. Grow scales a unit;
Pop also briefly overshoots its settled size when surrounding space permits.
Fade changes opacity, Wipe reveals the unit in the fill direction, and Flip
transitions between inactive and active faces. **Duration** is the time per
unit, from 0 to 5000 ms. **Stagger span** is the total spread between the
first and last unit starts, from 0 to 1000 ms, not a delay per unit.

Partial-unit appearance is independent of animation. None, duration 0, e-paper,
and device-wide animation disable update immediately without staggering.

[Widget chooser](README.md) | [User guides](../README.md)
