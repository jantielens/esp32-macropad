# Metering Feature — PRD Overview

**Project:** Darkroom Enlarger Timer  
**Feature:** Sensor-Assisted Exposure & Contrast Metering  
**Date:** 2026-04-13  
**Status:** Planning  
**Hardware:** 1× TSL2591 light sensor on cable-connected puck (~$7 BOM addition)

---

## What We're Building

A light sensor that helps the user find the right **exposure time** and **contrast filter
grade** before making a print. Two spot readings through the negative → the timer
recommends a time and a grade. One paper calibration (once per paper/setup) anchors the
readings to absolute seconds.

**Research & design docs:** See `research/` folder for the full product vision
(`metering-ux.md`) and firmware-level UX specification (`metering-firmware-ux.md`).

---

## PRD Breakdown

Four PRDs, implemented in order. Each is independently shippable and testable.

```
PRD 01 ──→ PRD 02 ──→ PRD 03 ──→ PRD 04
Infra      Paper Cal   Meter      Strip Pad
```

| PRD | Title | Scope | Depends On |
|-----|-------|-------|------------|
| [PRD 01](prd01-infra.md) | Infrastructure | TSL2591 driver + shared memory binding + integration scaffolding | — |
| [PRD 02](prd02-papercal.md) | Paper Calibration | `cal` binding (Phase 1a): read Lref, write to shared memory | PRD 01 |
| [PRD 03](prd03-meter.md) | Print Prep Meter | `meter` binding (Phase 2): bright/dark readings → SBR, grade, time | PRD 01 |
| [PRD 04](prd04-strippad.md) | Bare-Bulb Strip Pad + Navigation | Pre-configured strip pad (5.0s) + Home pad nav buttons | PRD 01–03 |

---

## Data Flow

```
Phase 1a (PRD 02)         Phase 1b (PRD 04)         Phase 2 (PRD 03)         Phase 3 (existing)
┌──────────────┐         ┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│  Cal pad     │         │  Strip pad   │         │  Meter pad   │         │ Expose/Strip │
│              │         │  (bare-bulb) │         │              │         │    pad       │
│ Read Lref    │──mem──→ │              │         │ Lref (auto)  │         │              │
│ (sensor)     │         │ Run strip    │         │ Zone V (+/-) │         │ Set time     │
│              │         │ Note Zone V  │──user──→│              │         │ Set grade    │
│              │         │              │         │ Focus ON     │         │ (physical)   │
│              │         │              │         │ Read BRIGHT  │         │              │
│              │         │              │         │ Read DARK    │──user──→│ Print        │
│              │         │              │         │ → SBR, Grade │         │              │
│              │         │              │         │ → Time       │         │              │
└──────────────┘         └──────────────┘         └──────────────┘         └──────────────┘
```

**Shared memory (`mem`)** carries Lref automatically from Phase 1a → Phase 2.  
**User** carries Zone V time (from strip table) and recommended time/grade (from results).

---

## Estimated Scope

| PRD | New files | Modified files | Lines (est.) | Complexity |
|-----|-----------|---------------|-------------|------------|
| 01 — Infrastructure | 4 | 5 | ~200 | Low |
| 02 — Paper Cal | 2 | 1 (pad config) | ~100 | Low |
| 03 — Meter | 2 | 1 (pad config) | ~250 | Medium |
| 04 — Strip Pad | 0 | 1 (pad config) | ~50 | Low |
| **Total** | **8** | **~7** | **~600** | |

For reference: `test_strip.cpp` alone is 809 lines. The entire metering feature is smaller.

---

## Key Decisions Made

1. **Two spot readings (bright + dark)** — minimum that unlocks both grade and time
2. **Shared memory** for Lref (auto) — eliminates manual entry friction
3. **Zone V entered manually** — user notes it from strip table, enters via ±0.1s/±1s buttons
4. **SBR works without calibration** — grade recommendation always available, time needs cal
5. **No PAPER_SPEED_CONSTANT** — bare-bulb strip at 5.0s center brackets most setups
6. **SBR-to-grade table needs real-hardware tuning** — flagged for bring-up
7. **RAM-only shared memory** — no NVS persistence in MVE, v2 enhancement
