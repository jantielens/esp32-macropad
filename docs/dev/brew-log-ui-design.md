# Brew Log UI/UX Design

> Status: Approved — ready for implementation
> Related: [brew-log-design.md](brew-log-design.md) (data format, storage, REST API, key decisions)

## Principles

1. **Lightweight on device** — the ESP32 is a JSON file server. All rendering, computation, and interactivity happen client-side in the browser.
2. **Fun and engaging** — brew cards with mini sparklines, animated charts, interactive hover/tap. This is a USP — users should *want* to look at their brew history.
3. **Data-driven** — the UI renders brew reports from their self-describing `fields` array. No hardcoded knowledge of brew types.
4. **Fits the existing portal** — same nav, same CSS conventions, same modular JS pattern.

## Charting: Chart.js via CDN

Charts are rendered client-side using [Chart.js](https://www.chartjs.org/) loaded from a CDN. Zero firmware size impact.

```html
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
```

### Why Chart.js

- Most popular charting library — well-documented, actively maintained, huge community
- Tooltips, legends, responsive resizing, animations built in
- Dual Y-axis (weight left, flow right) is a standard feature
- Phase overlay bands via the [annotation plugin](https://www.chartjs.org/chartjs-plugin-annotation/) (also CDN-hosted)
- Most developers already know it — low barrier for future contributors
- ~65 KB gzipped, cached by the browser after first load

### Offline Fallback

The web portal is accessed from a browser on the same WiFi network — internet is normally available. If the CDN is unreachable (rare offline LAN scenario):

- Charts gracefully degrade: the `<canvas>` area shows "Charts require internet connection for first load"
- All numeric data (fields, stats) still renders — only the visual charts are affected
- Detection: check if `window.Chart` exists after the script tag loads

## Page Architecture

A new **Brews** tab in the nav bar. Single page with two views (SPA-style toggle, no page reload):

Nav tab position: **Home | Brews | Pads | Network | Firmware** (second tab, right after Home).

```
brews.html
├── List View (default)
│   ├── Stats Banner
│   ├── Action Bar
│   └── Brew Cards (scrollable, stats only — no mini charts)
└── Detail View (click a brew card)
    ├── Back Button
    ├── Fields Grid
    ├── Weight Chart (separate, full-width)
    ├── Flow Rate Chart (separate, full-width, linked cursor)
    ├── Phase Timeline (future)
    └── Actions
```

## List View

### Stats Banner

Aggregate stats computed client-side from the list response. Four stat boxes in a responsive row:

```
┌───────────┬───────────┬───────────┬───────────┐
│    42     │   3:22    │  278.5g   │  2.1g/s   │
│   brews   │ avg time  │  avg wt   │ avg flow  │
└───────────┴───────────┴───────────┴───────────┘
```

- Iterate all brews, find `fields` entries by key (`duration`, `weight`, `avg_flow`)
- Compute averages
- If no brews: show "No brews yet — start your first brew from the device!"
- Updates live when brews are deleted

### Action Bar

A row of buttons above the brew cards:

| Button | Action | Notes |
|--------|--------|-------|
| **Export All** | Download all brews as JSON | Fetches full data for all brews, exports as single JSON array file |
| **Import** | File picker → upload JSON | POST to `/api/brews/import`, validates format client-side first |
| **Clear All** | Delete all brews | Confirm dialog → `DELETE /api/brews` |

### Brew Cards

Each brew is a **card** (not a table row). Cards are visual, scannable, and finger-friendly.

```
┌─────────────────────────────────────────────┐
│  ☕ Free Pour                 Mar 19, 10:30  │
│                                              │
│    3:15        285.2g       3.8 g/s peak     │
│    Duration    Weight       Peak Flow        │
│                                        [🗑️]  │
└─────────────────────────────────────────────┘
```

#### Card Contents

- **Name** — from `fields` entry with `key: "name"` (formatted as `text`)
- **Timestamp** — from `fields` entry with `key: "ts"` (formatted as `datetime`)
- **Key stats** — 2–3 stats pulled from `fields` by key: `duration`, `weight`, `peak_flow`
- **Delete button** — trash icon, confirm dialog before delete
- **Click anywhere** → navigates to detail view (where the full charts live)

No mini charts in the list view — keeps the list API fast (one call, `fields` only, no series data). The charts are the reward for tapping into a brew.

### Card Layout (Responsive)

- **Mobile / narrow** (< 600px): single column, full-width cards
- **Tablet / medium** (600–900px): two-column card grid
- **Desktop / wide** (> 900px): two-column cards within the 900px max-width container

## Detail View

Opened when a user taps/clicks a brew card. Single `GET /api/brews/:id` call fetches the full brew including `series`.

### Back Button

Top of the page — returns to list view (SPA navigation, no page reload). Preserves scroll position in the list.

### Fields Grid

All entries from the `fields` array rendered as a responsive stat grid:

```
┌──────────┬──────────┬──────────┬──────────┐
│ Duration │ Weight   │ Peak Flow│ Avg Flow │
│   3:15   │  285.2g  │  3.8g/s  │  1.5g/s  │
└──────────┴──────────┴──────────┴──────────┘
```

- Iterate `fields` array
- Format each by `format` type: `text` as-is, `datetime` as locale date, `duration` as m:ss, `number` with 1 decimal + unit
- Skip `name` and `ts` (already shown in the page header)
- 2–4 columns depending on screen width, wraps naturally with CSS grid

### Charts

Two Chart.js line charts, stacked vertically, sharing the same X-axis (time):

#### Weight Chart

- **Type**: Line chart
- **X-axis**: Time in seconds (derived from `series.interval_ms`)
- **Y-axis**: Weight in grams
- **Line**: Smooth, filled area below (subtle gradient fill)
- **Color**: Blue gradient (#667eea → #764ba2, matching portal theme)
- **Tooltip**: On hover/tap, shows crosshair with exact time + weight
- **Responsive**: fills container width, fixed aspect ratio

#### Flow Rate Chart

- **Type**: Line chart
- **X-axis**: Same time axis as weight chart (aligned)
- **Y-axis**: Flow rate in g/s
- **Line**: Smooth, no fill
- **Color**: Dynamic by pour zone (using threshold coloring):
  - Gray (< 1.5 g/s) — idle
  - Green (1.5–2.5 g/s) — sweet spot
  - Orange (2.5–3.5 g/s) — high
  - Red (> 3.5 g/s) — too fast
- **Tooltip**: On hover/tap, shows crosshair with exact time + flow rate
- **Segment coloring**: Chart.js supports per-segment line colors via `segment` callback — the line color changes in real-time based on the flow value

#### Chart Interaction

- **Hover/tap crosshair** — vertical line follows cursor, tooltip shows both weight and flow at that point in time
- **Linked cursors** — hovering on one chart highlights the same time point on the other (Chart.js interaction mode: `index`)
- **Animations** — smooth draw-in animation when the detail view opens (Chart.js default)
- **Touch-friendly** — tap to lock the crosshair position on mobile

### Phase Timeline (Future)

When `phases` is present in the brew report, rendered below the charts:

```
┌──────────┬──────────┬──────────┬──────────┐
│  Bloom   │  Pour 1  │  Pour 2  │ Drawdown │
│   0:43   │   0:52   │   0:59   │   1:09   │
│  52.1g   │  148.3g  │  252.7g  │          │
│ ▓▓▓▓▓▓▓▓ │ ████████ │ ████████ │ ░░░░░░░░ │
└──────────┴──────────┴──────────┴──────────┘
```

- Color blocks match `phases[].color`
- Duration and weight shown per phase
- Target vs actual comparison (if targets present)
- Colored vertical bands overlaid on the charts (via Chart.js annotation plugin)

Not implemented in V1 (no phases in free pour brews). The UI code simply skips this section when `phases` is absent.

### Detail Actions

| Button | Action |
|--------|--------|
| **Export** | Download this brew as `brew_NNNN.json` (client-side blob from the already-fetched data) |
| **Delete** | Confirm dialog → `DELETE /api/brews/:id` → return to list view |

## Visual Design

### Consistency with Existing Portal

- Same `portal.css` — gradients, card borders, button styles, nav tabs
- Same responsive breakpoints (900px max-width container)
- Same loading overlay pattern (spinner while data loads)
- Brew cards use the existing `.section` card style as a base
- Stats banner uses a new `.brew-stats` grid (similar to the health badge row)

### Color Palette (Brew-Specific)

| Element | Color | Notes |
|---------|-------|-------|
| Weight line | `#667eea` | Matches portal gradient |
| Weight fill | `rgba(102, 126, 234, 0.1)` | Subtle area fill |
| Flow idle | `#808080` | Gray |
| Flow sweet spot | `#00FF00` → `#66BB6A` | Green (softened for chart) |
| Flow high | `#FFAA00` → `#FFA726` | Orange |
| Flow too fast | `#FF0000` → `#EF5350` | Red (softened) |
| Card hover | `rgba(102, 126, 234, 0.05)` | Subtle highlight |
### Animations

- **Card entrance**: staggered fade-in when the list loads (CSS `animation-delay` per card)
- **Chart draw**: Chart.js default "grow from left" animation on render
- **View transition**: smooth crossfade between list and detail views
- **Delete**: card shrinks and fades out before removal

## File Structure

| File | Est. Lines | Purpose |
|------|-----------|---------|
| `brews.html` | ~100 | Page structure: list skeleton, detail skeleton, templates |
| `portal_brews.js` | ~550 | All brew logic: API calls, card rendering, stats, export/import, Chart.js weight + flow charts, linked cursors |
| Additions to `portal.css` | ~150 | Brew cards, stats banner, chart containers, responsive layout |
| Addition to `_nav.html` | ~1 | Brews nav tab (second position: Home \| **Brews** \| Pads \| Network \| Firmware) |

**Total**: ~800 lines of new code. No firmware size increase for Chart.js (CDN-hosted).

## API Calls Summary

| View | API Call | Device Work |
|------|----------|-------------|
| List load | `GET /api/brews` | Scan dir, read `fields` from each file (no series) |
| Detail load | `GET /api/brews/:id` | Read one file, stream full JSON |
| Delete one | `DELETE /api/brews/:id` | Remove one file |
| Clear all | `DELETE /api/brews` | Remove all files in `/brews/` |
| Import | `POST /api/brews/import` | Validate + write file(s) |
| Export one | (client-side only) | None — uses already-fetched detail data |
| Export all | Sequential `GET /api/brews/:id` | Read each file; client assembles single JSON array |
| Stats | (client-side only) | None — computed from list data |

## Responsive Behavior

| Breakpoint | Layout |
|------------|--------|
| < 600px | Single-column cards, stacked stat boxes, full-width charts |
| 600–900px | Two-column card grid, 2×2 stat boxes, full-width charts |
| > 900px | Two-column cards within 900px container |

## Accessibility

- Cards are focusable and keyboard-navigable (Enter to open detail)
- Charts have aria-labels with summary text ("Weight chart showing 285g over 3 minutes")
- Delete confirmations use native `confirm()` dialog (works everywhere)
- Color-blind safe: flow zone colors are distinguishable by brightness, not just hue
