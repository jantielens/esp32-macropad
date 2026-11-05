---
title: Calculate values and dynamic colors
description: Use the complete expression language, nested bindings, thresholds, formatting, and safe missing-data handling.
---

Expressions turn literal values and other bindings into calculated numbers,
text, or colors. Use them to combine readings, compare a value with a limit,
or choose different feedback for different states.

Use `[expr:expression;format]`. The format is optional and follows the outer
expression, not an inner token. Numeric inner results become numbers;
non-numeric results are quoted as strings automatically.
An unresolved inner value makes the expression unavailable.

## Use the portal Expression builder

In a field with Binding tools, choose **Build an expression**. The same menu
also opens the binding builder and Binding Reference. Custom color pickers use
their separate workflow. Enter the expression body without the outer
`[expr:...]` wrapper.

Use **Insert binding** to add a raw binding token, **Insert function** to add a
supported function or conditional snippet, and **Insert color** to add a
quoted hex color at the current selection. Use **?** to open Binding Reference
for expression basics and the bindings available on the device. Enable
**Numeric formatting** to show the decimal-place and unit-suffix controls.
Formatting sets 0 through 6 decimal places and a display-only unit suffix.
A literal percent suffix is escaped as `%%`; units are not converted.

The builder can reopen one supported expression and its numeric format anywhere
in a field. For example, `Temperature: [expr:1 + 2] °C` loads `1 + 2` for editing.
Surrounding text, whitespace, and other bindings stay unchanged. Check
**Resulting field value**, then choose **Replace expression** to apply the edit.
The length limit includes the surrounding content.

Multiple expressions, expression fallbacks, and unsupported formats cannot be
loaded. The dialog explains why the existing value cannot be edited. Choose
**Start new expression** to build a replacement from scratch, then **Replace
field** to replace the entire value. Cancel to keep the original instead.
Plain text or a single non-expression binding also opens an empty editor for
building a replacement.

The Result preview uses the device's live binding resolver, not a browser
expression evaluator. Missing values and request failures are shown as
unavailable or failed previews; they do not block a structurally valid
expression from being applied. The Result preview resolves the expression only,
not the surrounding text. Portal checks do not prove that the firmware
will accept every expression. Applying updates only the selected field; choose
**Save Pad** to persist it. **Cancel** leaves the field unchanged.

## Operators and values

| Precedence, highest first        | Meaning                                  |
| -------------------------------- | ---------------------------------------- |
| `(expression)`                   | Grouping                                 |
| Unary `-`, `+`                   | Numeric negation/positive                |
| `*`, `/`, `%`                    | Multiply, divide, remainder              |
| `+`, `-`                         | Add/subtract numbers                     |
| `>`, `<`, `>=`, `<=`, `==`, `!=` | Comparisons; result is 1 or 0            |
| `condition ? value : value`      | Conditional selection; right-associative |

Use decimal numbers or double-quoted strings. String equality is case-sensitive.
Arithmetic and ordered comparisons require numbers; `+` is not string
concatenation. This is not JavaScript: do not assume `&&`, `||`, assignments,
or functions beyond those listed here are accepted.
Expressions support up to 12 nested groups/unary/conditional/function levels.

```text
[expr:[health:heap_free]/1024;%.1f KB]
[expr:[mqtt:solar;power]-[mqtt:grid;power];%.0f W]
[expr:[mqtt:device;state]=="ON"?"Running":"Stopped"]
```

## Update runtime state

The **Set variable** action can use an expression as its Value. A counter
starts at zero when unset and increments on each activation:

```text
[expr:[var:counter|0] + 1]
```

Set the same Name to the literal `0` to reset it. A toggle can switch a
numeric variable between zero and one:

```text
[expr:[var:enabled|0] == 0 ? 1 : 0]
```

Runtime variables store strings, while expressions interpret numeric text
using the same numeric rules as other bindings. See
[Set a runtime variable](actions.md#set-a-runtime-variable) for the complete
button workflow and reboot behavior.

## Use numeric functions

Numeric functions accept numbers and return numbers. `round()` rounds halfway
values away from zero. `floor()` rounds toward negative infinity, and `ceil()`
rounds toward positive infinity. `clamp()` requires `low` to be less than or
equal to `high`.

| Function | Result |
| -------- | ------ |
| `abs(x)` | Absolute value |
| `min(a, b)` | Smaller of two values |
| `max(a, b)` | Larger of two values |
| `clamp(x, low, high)` | `x` limited to the inclusive range from `low` to `high` |
| `round(x)` | Nearest integer, with halfway values away from zero |
| `floor(x)` | Integer at or below `x` |
| `ceil(x)` | Integer at or above `x` |

```text
[expr:abs([mqtt:solar;power]-[mqtt:grid;power]);%.0f W]
[expr:clamp([mqtt:room;humidity],0,100);%.0f%%]
[expr:floor([mqtt:room;temp])]
```

## Map thresholds to colors

Use `threshold()` to map a number to a color:

```text
threshold(value, color0, threshold1, color1, ..., thresholdN, colorN)
```

Thresholds must be strictly ascending numbers; colors are quoted strings.
Below the first threshold, use color0. At a threshold, switch to its following
color; at/above the last, use the last color.

```text
[expr:threshold([mqtt:room;temp],"#4CAF50",28,"#FF9800",35,"#FF0000")]
```

This is green below 28, orange from 28 to below 35, and red from 35.
The color picker's Generate Color by Threshold helper builds the same language.
It can be used in supported button, label, and widget color fields.

## Handle unavailable and invalid inputs

Use a neutral outer fallback, such as:

```text
[expr:[mqtt:room;temp]>30?"#FF4444":"#4CAF50"|#333333]
```

> [!IMPORTANT]
> Both ternary branches are evaluated, and inner bindings resolve before branch
> selection. A condition does not short-circuit an invalid division or missing
> token in its unselected branch.

To avoid division by zero, make the denominator itself safe, rather than
putting an unsafe division in a supposedly unused branch. For an optional
ratio display that returns zero when capacity is zero:

```text
[expr:[mqtt:tank;capacity]==0?0:[mqtt:tank;level]/([mqtt:tank;capacity]==0?1:[mqtt:tank;capacity])*100;%.0f%%]
```

For consequential controls, reject bad upstream data instead of inventing a
usable reading. Fix depth, numeric-type, syntax, and divide/modulo-by-zero errors;
a fallback alone does not make an invalid control calculation correct.

## Check the result

Test below, exactly at, and above each threshold. Check the disconnected/startup
state too. Expressions must fit their destination field; use
[named pad bindings](bindings.md#name-a-source-on-the-pad) to keep short color
fields readable. Refresh cadence and physical e-paper updates are not
guarantees that every intermediate value appears on the panel.

[Pad tasks](README.md) | [User guides](../README.md)
