---
description: "Enforced terminology conventions — canonical names for screens, pads, buttons, and widgets in all user-facing and code contexts"
applyTo: "**"
---

# Terminology Conventions (enforced)

Use these terms consistently in user-facing text (UI, docs, log messages, API responses) and in code (identifiers, comments). Avoid retired synonyms.

## Core Hierarchy

| Term | Definition | Scope |
|---|---|---|
| **Screen** | Any UI that can be displayed on the device (splash, info, test, pad, …). | User-facing + code |
| **Pad** | A user-customizable screen containing a grid of buttons. The device supports up to 16 pads (configurable via MAX_PADS). | User-facing + code |
| **Button** | An interactive element in a pad's grid. May host labels, icons, colors, actions, images, and optionally a widget. | User-facing + code |
| **Widget** | A specialized data visualization or interaction mode (gauge, sparkline, bar chart, rocker) rendered inside a button. A button without a widget is just a normal button. | User-facing + code |

## Retired / Internal-Only Terms

| Term | Status | Replacement |
|---|---|---|
| **Page** (as synonym for pad) | Retired from user-facing text | Use **pad** |
| **Tile** (as synonym for button) | Retired from user-facing text | Use **button** |
| **Cell** (as synonym for button in user-facing text) | Retired from user-facing text | Use **button** (or **position** when referring to a grid slot) |
| `ButtonTile` (LVGL struct) | Internal-only | Acceptable in C++ code — not exposed to users |
| `.pad-cell` (CSS class) | Internal-only | Acceptable in DOM — not shown in UI text |
| `tile_w` / `tile_h` (local layout vars) | Internal-only | Acceptable in layout code — not exposed to API or docs |

## Rules

1. **User-facing surfaces** (web UI labels, docs, log messages, REST API field names, HA entity names) must use **screen**, **pad**, **button**, and **widget** exclusively.
2. **Code identifiers** should follow the same convention for new code. Existing internal identifiers (`ButtonTile`, `.pad-cell`, etc.) may remain until a natural refactor opportunity.
3. **API contracts**: The REST endpoint is `/api/pad/button_sizes` with JSON fields `button_w` / `button_h`. The `[pad:name]` binding scheme name is unchanged.
4. **Singular vs plural**: "pad" / "pads", "button" / "buttons" — never "pad page" or "button tile" in user text.
5. When describing the hierarchy in docs, prefer the natural nesting: *"A pad contains a grid of buttons. Buttons can optionally host a widget."*
