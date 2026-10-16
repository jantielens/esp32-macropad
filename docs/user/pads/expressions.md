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
or arbitrary functions are accepted.
Expressions support up to 12 nested groups/unary/conditional/function levels.

```text
[expr:[health:heap_free]/1024;%.1f KB]
[expr:[mqtt:solar;power]-[mqtt:grid;power];%.0f W]
[expr:[mqtt:device;state]=="ON"?"Running":"Stopped"]
```

## Map thresholds to colors

The supported function is:

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
