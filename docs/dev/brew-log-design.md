# Brew Log Design

> Status: Approved — ready for implementation

## Goal

Automatically log every completed brew to on-device storage. Users view brew history and per-brew charts via the web portal. No manual steps — brews are recorded transparently when the brew manager transitions to DONE.

## Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Compile-time gate | `HAS_SENSOR_HX711` | Brew logging is a scale feature, not display-dependent |
| Brews tab visibility | Always present in HTML | Simplest approach; all boards get the tab |
| Chart.js source | CDN (`cdn.jsdelivr.net`) | Zero firmware size impact; graceful offline fallback |
| Brew cap | 200, hardcoded | No user warning; auto-evict oldest silently |
| Export All | Sequential `GET /api/brews/:id` | KISS; client assembles array |
| Timestamp (`ts`) | Epoch seconds; `0` if NTP not synced | UI shows "Unknown date" for `ts == 0` |
| `avg_flow` / `peak_flow` | Computed from `series` data in `brew_log_save()` | No running stats in `brew_manager`; single source of truth |
| Series buffer | PSRAM; allocated at `brew_start()`, freed after save | ~4.8 KB; not held during IDLE |
| NVS brew counter | `uint16_t` via `Preferences` | Simplest mechanism; max 65535, more than enough with 200-file cap |
| `format` field in `fields[]` | Keep | Drives UI rendering; multiple fields can share the same format |
| Single JS file | `portal_brews.js` (includes chart logic) | KISS; split later if needed |
| Feature flag in `/api/info` | Not yet | May add `has_brews` later |

## Design Principles

1. **Self-contained brew reports** — each file has everything the web UI needs to render it. No external template lookup, no dependency on what templates currently exist on the device.
2. **Forward-compatible** — old brews remain valid and renderable after firmware updates, new brew types, or template changes. Fields are additive; the UI skips what it doesn't find.
3. **Data-driven UI** — all brew metadata lives in a single `fields` array. Each entry carries its own label, unit, and format hint. The web UI iterates and renders without hardcoded field knowledge.

## Brew Report Format

### V1 — Free Pour

```json
{
  "v": 1,

  "fields": [
    { "key": "name",      "label": "Brew",      "value": "Free Pour",            "format": "text"     },
    { "key": "ts",        "label": "Date",      "value": 1742572800,             "format": "datetime" },
    { "key": "duration",  "label": "Duration",  "value": 195300, "unit": "ms",   "format": "duration" },
    { "key": "weight",    "label": "Weight",    "value": 285.2,  "unit": "g",    "format": "number"   },
    { "key": "peak_flow", "label": "Peak Flow", "value": 3.8,    "unit": "g/s",  "format": "number"   },
    { "key": "avg_flow",  "label": "Avg Flow",  "value": 1.46,   "unit": "g/s",  "format": "number"   }
  ],

  "series": {
    "interval_ms": 1000,
    "weight": [0.0, 0.3, 2.1, 8.5, 18.2, 32.0],
    "flow":   [0.0, 0.3, 1.8, 3.2, 2.9, 2.5]
  }
}
```

### Future — Guided Brew with Phases

```json
{
  "v": 1,

  "fields": [
    { "key": "name",      "label": "Brew",      "value": "V60 Tetsu 4:6",       "format": "text"     },
    { "key": "ts",        "label": "Date",      "value": 1742659200,             "format": "datetime" },
    { "key": "duration",  "label": "Duration",  "value": 225000, "unit": "ms",   "format": "duration" },
    { "key": "weight",    "label": "Weight",    "value": 310.5,  "unit": "g",    "format": "number"   },
    { "key": "dose",      "label": "Dose",      "value": 20.0,   "unit": "g",    "format": "number"   },
    { "key": "ratio",     "label": "Ratio",     "value": 15.5,   "unit": ":1",   "format": "number"   },
    { "key": "peak_flow", "label": "Peak Flow", "value": 4.1,    "unit": "g/s",  "format": "number"   },
    { "key": "avg_flow",  "label": "Avg Flow",  "value": 1.38,   "unit": "g/s",  "format": "number"   }
  ],

  "phases": [
    { "name": "Bloom",    "color": "FFA726", "target_weight_g": 50,  "target_duration_s": 45,
      "start_idx": 0,  "end_idx": 42,  "weight_g": 52.1, "duration_ms": 43000 },
    { "name": "Pour 1",   "color": "66BB6A", "target_weight_g": 150,
      "start_idx": 43, "end_idx": 95,  "weight_g": 148.3, "duration_ms": 52000 },
    { "name": "Pour 2",   "color": "42A5F5", "target_weight_g": 250,
      "start_idx": 96, "end_idx": 155, "weight_g": 252.7, "duration_ms": 59000 },
    { "name": "Drawdown", "color": "AB47BC",
      "start_idx": 156,"end_idx": 224, "duration_ms": 69000 }
  ],

  "series": {
    "interval_ms": 1000,
    "weight": [],
    "flow":   []
  }
}
```

## Field Reference

### Top-Level Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `v` | int | yes | Schema version (currently `1`) |
| `fields` | array | yes | Ordered list of self-describing key/value pairs (see below) |
| `series` | object | yes | Time-series sensor data (see below) |
| `phases` | array | no | Phase definitions + actuals (future; see below) |

The file has no other top-level data fields — everything about the brew (name, timestamp, stats) lives in `fields`.

### Fields Array

Each entry in the `fields` array describes one piece of brew data for the UI to display.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `key` | string | yes | Machine-readable identifier (for lookups, sorting, filtering) |
| `label` | string | yes | Human-readable display name |
| `value` | string/number | yes | The value (string for text, number for numeric data) |
| `unit` | string | no | Display suffix (e.g. `"g"`, `"g/s"`, `"ms"`) — omit for non-numeric fields |
| `format` | string | yes | Rendering hint (see format types below) |

#### Format Types

| Format | Description | Example |
|--------|-------------|--------|
| `text` | Display as-is | `"Free Pour"` |
| `datetime` | Format epoch seconds as local date/time | `"Mar 19, 2026 10:30"` |
| `duration` | Format milliseconds as m:ss | `"3:15"` |
| `number` | Display with 1 decimal + unit | `"285.2 g"` |

#### V1 Free Pour Fields (Always Present)

| Key | Format | Description |
|-----|--------|-------------|
| `name` | `text` | Brew type display name |
| `ts` | `datetime` | Brew start timestamp |
| `duration` | `duration` | Total brew time |
| `weight` | `number` | Final water weight |
| `peak_flow` | `number` | Maximum flow rate |
| `avg_flow` | `number` | Average flow rate |

Future brew types can add entries (e.g. `dose`, `ratio`, `extraction_yield`) — the UI renders them automatically.

### Phases Array (Optional)

Absent for free pour brews (UI treats the whole brew as one phase). Each entry combines the template definition (plan) and the actual outcome.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | yes | Phase display name |
| `color` | string | no | Hex color for chart overlay (e.g. `"FFA726"`) |
| `target_weight_g` | float | no | Target weight for this phase |
| `target_duration_s` | int | no | Target duration for this phase |
| `start_idx` | int | yes | Index into `series` arrays where phase started |
| `end_idx` | int | yes | Index into `series` arrays where phase ended |
| `weight_g` | float | no | Actual weight achieved in this phase |
| `duration_ms` | int | no | Actual duration of this phase |

### Series Object

Fixed-interval parallel arrays of sensor data recorded during the brew at 1 Hz.

| Field | Type | Description |
|-------|------|-------------|
| `interval_ms` | int | Sample interval (1000 for 1 Hz) |
| `weight` | float[] | Weight in grams at each sample point |
| `flow` | float[] | Flow rate in g/s at each sample point |

Future sensor data (pressure, temperature) can be added as additional arrays without breaking existing brews.

## Time-Series Recording

- **Sample rate**: 1 Hz (one sample per second)
- **Source**: EMA-smoothed weight and device-computed flow rate from the 80 Hz HX711 sensor loop
- **Start**: Recording begins at auto-start (weight > 2 g), not at the moment the user taps "Start"
- **End**: Recording stops when `brew_stop()` is called
- **Storage cost**: ~10 bytes per sample (two floats as JSON). A 4-minute brew ≈ 240 samples ≈ 4–5 KB including metadata.
- **Timestamp**: Uses NTP-synced epoch seconds. If NTP has not synced yet, `ts` is written as `0`. The UI renders `0` as "Unknown date".

## Storage

### Layout

- Individual JSON files at `/brews/NNNN.json` (zero-padded 4-digit ID)
- Matches existing patterns: `/config/pad_N.json`, `/icons/*.png`
- Next ID tracked in NVS as `uint16_t` via `Preferences` (survives reboots, independent of filesystem)
- The numeric ID is derived from the filename — not stored inside the file

### Capacity

- jc4880p433 storage partition: 3.4 MB (LittleFS, shared with pad configs and icons)
- Per brew: ~3–5 KB
- Cap: 200 brews (~1 MB worst case) — auto-evict oldest when full
- Remaining space for pad configs, icons, swipe config: ~2+ MB

### Operations

| Operation | Method |
|-----------|--------|
| Save brew | Write `/brews/NNNN.json` with ArduinoJson |
| List brews | Scan `/brews/` directory, read `fields` array only (skip `series`) |
| Get single brew | Read and parse full file including `series` |
| Delete brew | `LittleFS.remove()` |
| Clear all | Remove all files in `/brews/`, reset NVS counter |

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/brews` | List all brews (newest first) — returns `fields` only, no `series` |
| `GET` | `/api/brews/:id` | Full brew report including `series` (for chart rendering) |
| `DELETE` | `/api/brews/:id` | Delete a specific brew |
| `DELETE` | `/api/brews` | Clear all brew history |
| `POST` | `/api/brews/import` | Import brew(s) from JSON (single object or array) |

### Export Format

Export is a single JSON array of full brew objects (including `series`). Same format for exporting one or many — the client assembles the file and triggers a browser download. No server-side export endpoint needed.

```json
[
  { "v": 1, "fields": [...], "series": {...} },
  { "v": 1, "fields": [...], "series": {...} }
]
```

Import accepts the same format (`POST /api/brews/import`). The server assigns new IDs from the NVS counter.

### List Response Shape

```json
{
  "brews": [
    { "id": 42, "v": 1, "fields": [...] },
    { "id": 41, "v": 1, "fields": [...] }
  ],
  "count": 42,
  "max": 200
}
```

The `id` is derived from the filename and injected by the API — it's not stored inside the file. Each brew entry contains the full `fields` array so the UI can render the list data-driven (e.g. pick `name`, `ts`, `duration` by key for table columns).

## Web Portal Page

New **Brews** tab in the nav bar (`brews.html`).

### List View

- Summary stats at top: total brews, averages across all brews
- Table of all brews: columns derived from `fields` entries (name, date, duration, weight — looked up by key)
- Sorted newest-first (by filename ID, higher = newer)
- Delete button per row, "Clear All" button

### Detail View (Single Brew)

- All fields rendered from the `fields` array (iterate, format, display)
- Weight chart (line) from `series.weight`
- Flow rate chart (line) from `series.flow`
- If `phases` present: colored overlay bands on charts, per-phase actual vs target comparison
- Back button to list

### UI Rendering Logic

```
1. Read brew file (or list response)
2. Render fields: for each entry in fields[], show label + formatted value + unit
3. If series present: draw weight and flow line charts
4. If phases present: overlay phase bands with colors, show target lines
5. No hardcoded knowledge of brew types — everything is data-driven
```

## Changes to Existing Code

### brew_manager.cpp

- Add 1 Hz series recording — PSRAM ring buffer allocated at `brew_start()`, sampled each second during BREWING, freed after `brew_log_save()` completes
- On `brew_stop()`: call `brew_log_save()` with finalized series data
- `brew_log_save()` computes `peak_flow` and `avg_flow` from the recorded `series.flow[]` array (no running stats needed in brew_manager)

### brew_manager.h

- Add `BREW_SERIES_MAX_SAMPLES` constant (e.g. 600 = 10-minute max brew)
- Expose series buffer pointer + sample count for `brew_log_save()` to consume

### New Files

| File | Purpose |
|------|---------|
| `brew_log.cpp/h` | LittleFS I/O: save, load, list, delete, stats, auto-eviction |
| `web_portal_brews.cpp/h` | REST API endpoints for brew CRUD |
| `web/brews.html` | Brew history page (list + detail views) |
| `web/portal_brews.js` | Brew list/detail/chart logic (single file, includes Chart.js rendering) |

### Existing File Edits

| File | Change |
|------|--------|
| `brew_binding.cpp` | Add `peak_flow` key to resolver (reads from last-saved brew log) |
| `web_portal_routes.cpp` | Register brew API routes |
| `web/_nav.html` | Add Brews tab (always visible, second position after Home) |
| `portal_core.js` | Add `brews` to page detection logic |
| `portal_config.js` | Hide Brews tab in Core/AP mode |

## Forward Compatibility Summary

| Future Feature | Impact on Format |
|----------------|-----------------|
| Multi-phase brews | Add `phases` array — old brews lack it, UI treats as one phase |
| User-defined templates | `name` changes — self-contained in each file |
| Dose/ratio | Add entries to `fields` — UI renders automatically |
| Grinder/bean metadata | Add entries to `fields` (e.g. `grinder`, `beans`) |
| Notes/rating | Add entries to `fields` — editable via PATCH API |
| New sensors (pressure, temp) | Add arrays to `series` — old brews just lack them |
| Template schema changes | Doesn't matter — each brew snapshots its own template data |
