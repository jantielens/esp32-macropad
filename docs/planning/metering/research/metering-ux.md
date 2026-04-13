# Sensor-Assisted Exposure & Contrast — UX Specification

**Author:** Moe (Darkroom Printer SME)  
**Date:** 2026-04-13  
**Status:** Draft — MVE Scope  
**Features Referenced:** MET08, MET02, MET06, TST01  
**Hardware Required:** 1× TSL2591 light sensor on cable-connected puck (~$7)  
**Scope:** Single-grade printing only (split-grade, dodge/burn out of scope)

---

## 1. Introduction — What This Solves

Every print starts with two questions:

1. **How long should I expose?** (base exposure time in seconds)
2. **What contrast filter should I use?** (multigrade grade 0–5)

Today, most printers answer both by guessing. Experienced printers guess well — they look at the projected image, squint, and say "that looks like a grade 2 negative, maybe 12 seconds." Beginners have no such intuition. They pick 8 seconds and grade 2 because that's what the YouTube video said, run a test strip that's either way too dark or way too light, waste paper, repeat.

A single light sensor on the easel can answer both questions before a single sheet of paper is exposed. This document describes the minimal, achievable UX for doing exactly that.

### What This Does NOT Do (Deferred)

- **Split-grade printing** — no soft/hard filtration breakdown (future firmware update)
- **Zone placement** — no per-area zone mapping (future: 3+ spot readings)
- **Dodge/burn guidance** — no spatial recommendations (future: sensor grid MET15)
- **Paper speed database** — no built-in profiles per paper brand (future: MET06)
- **Light output compensation** — no closed-loop lamp monitoring during exposure (future: MET07)

---

## 2. Overview — Three Phases

The workflow has three phases. Phase 1 is a one-time calibration (once per paper/developer/setup combination). Phases 2 and 3 happen for every print.

```
Phase 1: PAPER CALIBRATION (one-time)
  Input:  nothing (bare enlarger, no negative)
  Output: paper speed constant (Zone V time for this setup)
  Effort: ~5 minutes including developing the test strip

Phase 2: PRINT PREP (per negative)
  Input:  paper speed constant from Phase 1
  Output: recommended exposure time + contrast filter grade
  Effort: ~15–20 seconds (two sensor readings)

Phase 3: PRINT (per print)
  Input:  recommendation from Phase 2
  Output: a finished print (or a targeted test strip)
  Effort: normal printing workflow
```

---

## 3. Phase 1 — Paper Calibration

### 3.1 Purpose

Establish the **paper speed constant** for the user's specific setup: their paper, their developer, their enlarger, their lens aperture, their enlarger height. This anchors the sensor's relative light readings to absolute exposure times in seconds.

Without this calibration, the sensor can tell you "negative A is 1.3 stops denser than negative B" — useful but not actionable. With calibration, it can tell you "expose for 14.2 seconds" — directly usable.

### 3.2 When to Do It

- **First time** using the metering feature
- **When changing paper** (e.g., switching from Ilford MGRC to Foma)
- **When changing developer** (or when developer is significantly exhausted)
- **Optionally** when changing enlarger height or aperture significantly (the sensor recalibration in Phase 2 partially compensates, but a fresh paper test is more reliable)

The timer should store the calibration result and display a reminder: *"Paper calibration: Ilford MGRC IV @ Grade 2, done 3 sessions ago."* The user can re-run it anytime.

### 3.3 Prerequisites

- Enlarger at the user's typical working height and lens aperture
- Contrast filter set to **Grade 2** (the standard "normal" grade — this is the baseline)
- **No negative** in the carrier
- A piece of paper ready for a test strip
- Normal processing chemistry (developer, stop, fix) ready

### 3.4 UX Flow

**Step 1 — Sensor Calibration (bare bulb reference)**

The timer prompts the user to prepare:

```
┌─────────────────────────────────────────┐
│  📋  PAPER CALIBRATION                  │
│                                         │
│  Get ready:                             │
│  • No negative in the enlarger          │
│  • Lens at your working aperture        │
│  • Filter set to Grade 2               │
│  • Place the sensor puck on the easel   │
│                                         │
│  [Start Calibration]                    │
└─────────────────────────────────────────┘
```

User taps **Start Calibration**. The timer turns on the enlarger (via smart plug), takes a sensor reading to record `Lref` (the maximum light intensity at the easel plane with no negative), then turns the enlarger off.

```
┌─────────────────────────────────────────┐
│  📋  PAPER CALIBRATION                  │
│                                         │
│  ✅ Light reading taken!                │
│                                         │
│  Now remove the sensor puck from the    │
│  easel and place a strip of paper.      │
│                                         │
│  The timer will make a test strip:      │
│  7 segments from 2.5s to 10.1s         │
│  (centered on 5.0s, ⅓-stop steps)     │
│                                         │
│  [Ready — Start Test Strip]             │
└─────────────────────────────────────────┘
```

> **Design note:** The test strip range (centered on 5.0s) is chosen to bracket typical Zone V times for most paper/enlarger combinations. If the user's setup is unusual (very bright or very dim enlarger), the timer could auto-adjust the range based on the `Lref` reading — brighter bulb → shorter times, dimmer → longer. For the MVE, a fixed range centered on 5.0s with ⅓-stop steps covers roughly 2.5s–10s, which handles 90%+ of setups.

**Step 2 — Test Strip Exposure**

The timer runs an automated test strip sequence — identical to the normal test strip feature (TST01). The enlarger turns on and off via the smart plug. The timer beeps to guide the user through moving the mask for each segment.

This is the existing test strip machinery. No new firmware needed for the exposure sequence itself.

**Step 3 — Process the Test Strip**

```
┌─────────────────────────────────────────┐
│  📋  PAPER CALIBRATION                  │
│                                         │
│  Develop, stop, fix your test strip.    │
│                                         │
│  Come back when you can evaluate it     │
│  in the light (or under safelight).     │
│                                         │
│  [I'm Ready to Evaluate]                │
└─────────────────────────────────────────┘
```

The timer waits. No rush — the user processes the strip normally.

**Step 4 — Pick the Midtone**

```
┌─────────────────────────────────────────┐
│  📋  PAPER CALIBRATION                  │
│                                         │
│  Look at your test strip. Tap the       │
│  segment that looks like a good         │
│  MEDIUM GREY — not too light, not       │
│  too dark.                              │
│                                         │
│  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│  │2.5s │3.2s │4.0s │5.0s │6.3s │8.0s │10.1s│
│  │  1  │  2  │  3  │  4  │  5  │  6  │  7  │
│  └─────┴─────┴─────┴─────┴─────┴─────┴─────┘
│                                         │
│  Tip: Hold an 18% grey card next to     │
│  the strip if you have one.             │
└─────────────────────────────────────────┘
```

User taps a segment (e.g., segment 5 = 6.3s).

**Step 5 — Confirmation & Storage**

```
┌─────────────────────────────────────────┐
│  📋  PAPER CALIBRATION — Done!          │
│                                         │
│  ✅ Midtone (Zone V) = 6.3s            │
│  at Grade 2, Lref = [stored]           │
│                                         │
│  This calibration will be used for      │
│  all future metering until you          │
│  recalibrate.                           │
│                                         │
│  [Done]  [Redo — That Didn't Look Right]│
└─────────────────────────────────────────┘
```

The timer stores:
- `Lref` — the bare-bulb light intensity
- `T_zoneV` — the time the user selected as midtone (6.3s in this example)
- `paper_speed = T_zoneV × Lref` — a constant that relates light intensity to exposure time for Zone V on this paper

### 3.5 What If None of the Segments Look Right?

If the entire strip is too dark or too light, the user taps **"Redo — That Didn't Look Right"** and the timer offers to shift the range:

```
┌─────────────────────────────────────────┐
│  Was the strip mostly too dark          │
│  or too light?                          │
│                                         │
│  [Too Dark — Use Shorter Times]         │
│  [Too Light — Use Longer Times]         │
└─────────────────────────────────────────┘
```

The timer shifts the test strip center time by 2 stops in the indicated direction and runs again. This should be rare with an auto-adjusted range based on `Lref`.

---

## 4. Phase 2 — Print Prep (Per Negative)

### 4.1 Purpose

Take two light readings through the negative to determine:
1. **Subject Brightness Range (SBR)** — the contrast range of the negative → maps to a **filter/grade recommendation**
2. **Base exposure time** — how long to expose at the recommended grade

### 4.2 Prerequisites

- Paper calibration (Phase 1) completed
- Negative inserted in the enlarger carrier
- Image focused and composed on the easel
- Enlarger at working height and aperture (if height/aperture changed significantly since Phase 1, the sensor recalibration step compensates)

### 4.3 UX Flow

**Step 1 — Sensor Recalibration (bare bulb reference, if needed)**

If the user has changed the enlarger height or aperture since the last calibration, the timer needs a fresh `Lref`. Even if nothing changed, a quick recalibration takes 5 seconds and eliminates lamp warm-up drift.

```
┌─────────────────────────────────────────┐
│  📊  PRINT PREP                         │
│                                         │
│  Before inserting your negative:        │
│  Place the sensor puck on the easel.    │
│                                         │
│  [Recalibrate Light]  [Skip — Same Setup]│
└─────────────────────────────────────────┘
```

If the user taps **Recalibrate Light**, the timer turns on the enlarger (no negative), reads the sensor, stores the new `Lref`, turns off the enlarger. 5 seconds.

If the user taps **Skip**, the timer reuses the stored `Lref` from Phase 1 or the last recalibration.

> **Design note:** The timer could auto-detect whether recalibration is needed by comparing elapsed time since last calibration or checking if the user navigated through enlarger height settings. For the MVE, a simple prompt with skip option is sufficient.

**Step 2 — Insert Negative**

```
┌─────────────────────────────────────────┐
│  📊  PRINT PREP                         │
│                                         │
│  Insert your negative and focus         │
│  the image on the easel.               │
│                                         │
│  [Negative is in — Ready to Meter]      │
└─────────────────────────────────────────┘
```

**Step 3 — Read Bright Spot**

The timer turns on the enlarger so the image is projected on the easel.

```
┌─────────────────────────────────────────┐
│  📊  PRINT PREP — Reading 1 of 2       │
│                                         │
│  Place the sensor puck on the           │
│  BRIGHTEST spot you see on the easel.   │
│                                         │
│  (This is the thinnest part of your     │
│  negative — it will print darkest.)     │
│                                         │
│  [Read Bright Spot]                     │
└─────────────────────────────────────────┘
```

User positions puck on the brightest area of the projected image, taps the button (or footswitch). Timer reads `L_bright`.

```
┌─────────────────────────────────────────┐
│  📊  PRINT PREP — Reading 1 of 2       │
│                                         │
│  ✅ Bright spot recorded                │
│                                         │
└─────────────────────────────────────────┘
```

**Step 4 — Read Dark Spot**

```
┌─────────────────────────────────────────┐
│  📊  PRINT PREP — Reading 2 of 2       │
│                                         │
│  Now place the sensor puck on the       │
│  DARKEST spot you see on the easel.     │
│                                         │
│  (This is the densest part of your      │
│  negative — it will print lightest.)    │
│                                         │
│  [Read Dark Spot]                       │
└─────────────────────────────────────────┘
```

User positions puck on the darkest area, taps. Timer reads `L_dark`. Timer turns off the enlarger.

### 4.4 The Math

```
Density_bright = log10(Lref / L_bright)    # thinnest neg area
Density_dark   = log10(Lref / L_dark)      # densest neg area

SBR = Density_dark - Density_bright        # Subject Brightness Range

Density_mid_estimate = (Density_bright + Density_dark) / 2    # estimated midtone
```

**Grade recommendation** from SBR (standard mapping):

| SBR (density range) | Grade | Description |
|---|---|---|
| ≤ 0.6 | 5 | Very flat negative — needs maximum contrast |
| 0.7 | 4½ | Flat |
| 0.8 | 4 | Flat |
| 0.9 | 3½ | Slightly flat |
| 1.0 | 3 | Slightly flat to normal |
| 1.1 | 2½ | Normal |
| 1.2 | 2 | Normal (most common) |
| 1.3 | 1½ | Slightly contrasty |
| 1.4 | 1½ | Contrasty |
| 1.5 | 1 | Contrasty |
| 1.6 | ½ | Very contrasty |
| 1.7 | ½ | Very contrasty |
| ≥ 1.8 | 0 | Extremely contrasty — needs maximum softening |

> **Design note:** This mapping is a starting approximation based on standard Zone System practice. It is paper-dependent — Ilford MGRC responds slightly differently than Foma or Kentmere. For the MVE, one default mapping is sufficient. A future update (MET06 — Paper Speed Database) could refine per-paper mappings. The user can always override.

**Base exposure time** from midtone estimate:

```
T_base = paper_speed / (Lref_current / L_mid_equivalent)
```

Where:
- `paper_speed` = the constant stored in Phase 1 (`T_zoneV × Lref` at calibration time)
- `Lref_current` = the current bare-bulb reading (may differ from calibration if height/aperture changed)
- `L_mid_equivalent` = derived from the estimated midtone density: `Lref_current / 10^Density_mid_estimate`

In practice this simplifies to:

```
T_base = T_zoneV × (Lref_calibration / Lref_current) × 10^Density_mid_estimate
```

The `Lref_calibration / Lref_current` ratio compensates for any change in enlarger height or aperture since calibration.

### 4.5 Results Screen

```
┌─────────────────────────────────────────┐
│  📊  PRINT PREP — Results               │
│                                         │
│  Negative contrast:  1.25              │
│  ── normal, slightly contrasty ──       │
│                                         │
│  Suggested filter:   Grade 2           │
│  Suggested exposure: 14.2s             │
│                                         │
│  [Print at 14.2s]   [Test Strip First]  │
│                                         │
│  [Adjust Grade ±]   [Adjust Time ±]    │
│  [Re-Meter]                             │
└─────────────────────────────────────────┘
```

### 4.6 Adjustments

The user can override either recommendation before proceeding:

- **Adjust Grade ±** — tap to step through grades in ½-stop increments. The exposure time recalculates automatically (different grades have slightly different paper speeds, but for the MVE a fixed offset per grade is acceptable — or simply don't adjust time when grade changes and let the user deal with it via a test strip).
- **Adjust Time ±** — tap to adjust in ⅓-stop increments (or whatever the user's preferred f-stop resolution is).
- **Re-Meter** — go back to Step 3 and take fresh readings.

### 4.7 Edge Cases

**What if bright and dark readings are very close (SBR < 0.4)?**

The negative is extremely flat, or the user measured two similar areas. The timer warns:

```
  ⚠️ Very low contrast detected (SBR: 0.3)
  Are you sure you measured the brightest
  AND darkest areas?
  [Re-Meter]  [Use Anyway — Grade 5]
```

**What if the readings are inverted (bright reads darker than dark)?**

The user swapped the spots. The timer auto-corrects (swap the values internally) and proceeds normally. No need to bother the user — bright and dark are just labels for the UI guidance.

**What if Lref was never calibrated?**

The timer cannot compute absolute exposure times. It can still compute SBR and give a grade recommendation (that's purely relative), but shows the exposure time as a relative value:

```
  Suggested filter: Grade 2
  Exposure: +0.8 stops from your usual starting point
  (Run Paper Calibration for time in seconds)
```

---

## 5. Phase 3 — Print

### 5.1 Purpose

Use the recommendation from Phase 2 to either make a print directly or run a targeted test strip.

### 5.2 Two Paths

**Path A — Print Directly**

User taps **"Print at 14.2s"**. The timer sets up a single exposure:
- Time: 14.2s
- The user sets their contrast filter to the recommended grade manually (Grade 2 in this example)
- User loads paper, taps Start (or footswitch), timer exposes via smart plug

This path is for confident printers or when you're reprinting a negative you've already dialed in.

**Path B — Targeted Test Strip**

User taps **"Test Strip First"**. The timer pre-populates the test strip settings:
- **Center time:** 14.2s (the metered recommendation)
- **Step interval:** ⅓ stop (default, user can change)
- **Segments:** 7 (default, user can change)
- **Range:** ±1 stop around 14.2s = roughly 7.1s to 28.4s

```
┌─────────────────────────────────────────┐
│  📋  TARGETED TEST STRIP                │
│                                         │
│  Centered on metered value: 14.2s      │
│  Filter: Grade 2 (from meter)          │
│                                         │
│  Segments: 7  │  Step: ⅓ stop          │
│                                         │
│  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│  │8.9s │10.1s│11.3s│14.2s│17.9s│20.1s│22.5s│
│  │ -1  │ -⅔  │ -⅓  │  0  │ +⅓  │ +⅔  │ +1  │
│  └─────┴─────┴─────┴─────┴─────┴─────┴─────┘
│                                         │
│  [Start Test Strip]  [Adjust Settings]  │
└─────────────────────────────────────────┘
```

This is the existing test strip feature (TST01) — the only difference is that the center time is pre-populated from metering instead of manually entered. The test strip range is much tighter than a blind test strip (±1 stop instead of ±2–3 stops), which means the user gets a usable result more often on the first strip.

After developing the test strip, the user picks the best segment and prints at that time — the normal test strip evaluation flow.

---

## 6. Complete Session Example

A typical printing session using all three phases:

```
FIRST SESSION WITH NEW PAPER (do once):
─────────────────────────────────────────
1. Paper Calibration
   - Remove negative, set Grade 2 filter
   - Sensor reads bare bulb               →  5 seconds
   - Timer runs test strip (7 segments)   →  2 minutes
   - Develop, stop, fix the strip         →  3 minutes
   - Tap the medium grey segment          →  5 seconds
   ✅ Paper speed stored. Total: ~5 minutes.

EVERY PRINT SESSION:
─────────────────────────────────────────
2. Print Prep (per negative)
   - Recalibrate light (optional)         →  5 seconds
   - Insert negative, focus
   - Place puck on bright spot, tap       →  5 seconds
   - Place puck on dark spot, tap         →  5 seconds
   ✅ "Grade 2, 14.2s" Total: ~15 seconds.

3. Print
   Path A: Print directly at 14.2s
   Path B: Run targeted test strip (±1 stop around 14.2s)
           → pick best segment → print
```

**Time added to workflow per negative:** ~15 seconds for metering, plus optionally one tightly targeted test strip instead of 1–3 blind test strips.

---

## 7. Hardware Requirements

| Component | Qty | Cost | Notes |
|---|---|---|---|
| TSL2591 light sensor breakout | 1 | ~$7 | Adafruit or generic. 600M:1 dynamic range (0.0002–88k lux). I2C interface. |
| Sensor puck housing | 1 | ~$2 | 3D-printed or simple enclosure. Sensor faces up. Flat bottom to sit on easel. |
| Cable (I2C + power) | 1 | ~$3 | 4-wire cable, ~1–2m length. Connects sensor puck to timer's I2C bus. |
| **Total BOM addition** | | **~$12** | |

The sensor puck is the same hardware that will serve MET07 (light output compensation) and future MET08 extensions (multi-spot zone placement). No wasted hardware investment.

### Sensor Puck Design Considerations

- **Flat bottom** — sits stably on the easel surface
- **Sensor facing straight up** — toward the enlarger lens
- **Narrow acceptance angle preferred** — a small baffle or tube over the sensor reduces off-axis light and gives more precise spot readings. A 10–15mm tube would limit the field of view to roughly the area directly above the puck.
- **Cable long enough** to reach any part of the easel from the timer — 1.5m minimum
- **Light-colored top surface** around the sensor — avoids absorbing/reflecting stray light asymmetrically

---

## 8. Firmware Data Model

### Stored Calibration Data

```
paper_calibration {
    Lref: float           // bare-bulb light intensity at calibration time
    T_zoneV: float        // seconds — user-selected midtone time
    paper_speed: float    // T_zoneV × Lref — derived constant
    grade_at_cal: int     // grade used during calibration (default: 2)
    timestamp: datetime   // when calibration was performed
    label: string         // user-friendly label, e.g. "Ilford MGRC IV"
}
```

### Per-Reading Data (transient, per metering session)

```
meter_session {
    Lref_current: float       // bare-bulb reading (recalibrated or reused)
    L_bright: float           // bright-spot reading
    L_dark: float             // dark-spot reading
    D_bright: float           // computed density (bright)
    D_dark: float             // computed density (dark)
    D_mid_estimate: float     // estimated midtone density
    SBR: float                // Subject Brightness Range
    recommended_grade: float  // 0–5 in ½-stop increments
    recommended_time: float   // seconds
}
```

### SBR-to-Grade Lookup Table

Stored as a configurable table (firmware default, user can adjust in future):

```
sbr_grade_map [
    { sbr_max: 0.60, grade: 5.0 },
    { sbr_max: 0.70, grade: 4.5 },
    { sbr_max: 0.80, grade: 4.0 },
    { sbr_max: 0.90, grade: 3.5 },
    { sbr_max: 1.00, grade: 3.0 },
    { sbr_max: 1.10, grade: 2.5 },
    { sbr_max: 1.20, grade: 2.0 },
    { sbr_max: 1.35, grade: 1.5 },
    { sbr_max: 1.50, grade: 1.0 },
    { sbr_max: 1.65, grade: 0.5 },
    { sbr_max: 999,  grade: 0.0 },
]
```

---

## 9. Upgrade Path

This MVE is designed as the foundation for increasingly sophisticated metering features. Same sensor, same cable, same puck — firmware only.

| Version | Feature | What it adds |
|---|---|---|
| **MVE** (this doc) | 2-spot metering | Grade recommendation + base exposure time |
| **v2** | Optional 3rd reading (midtone) | More accurate exposure (user picks actual midtone instead of estimated) |
| **v2** | Per-grade paper calibration | Run calibration at 2–3 grades for tighter grade recommendation |
| **v2** | MET07 — Light output compensation | Sensor monitors lamp during exposure, auto-compensates for drift |
| **v2** | MET06 — Paper speed database | Built-in profiles for common papers (Ilford, Foma, Kentmere) |
| **v3** | Full MET08 — Multi-spot zone placement | 3–10 user-positioned readings with zone map visualization |
| **v3** | MET15 — Sensor grid array | Hardware add-on: 3×3+ sensor PCB for instant tonal histogram |

Each step builds on the previous one without breaking it. A user who only ever uses the 2-spot MVE still gets valuable results indefinitely.

---

## 10. Honest Limitations

**The grade recommendation is approximate.** The SBR-to-grade mapping is a generalization — it works for most variable-contrast papers but isn't calibrated per paper. Expect accuracy within ±½ grade. This is still enormously better than guessing, and the user can always adjust.

**The exposure time depends on the paper calibration quality.** If the user picks a too-light or too-dark segment as their "midtone" in Phase 1, all subsequent exposure recommendations will be shifted. Mitigation: the 18% grey card tip in the UI, and the "Redo" option.

**The midtone is estimated, not measured.** With only bright and dark readings, the midtone density is the arithmetic mean of the two extremes. In most real images, the true midtone is close to this estimate. In strongly skewed images (e.g., mostly dark with a small bright area), the estimate may be off. A future optional third reading solves this.

**The user must find the actual extremes.** If they put the puck on a "pretty bright" area instead of the "brightest" area, the SBR will be underestimated and the grade recommendation will be slightly too hard. In practice, the brightest and darkest areas of a projected image are visually obvious — the eye is very good at finding them.

**This does not replace test strips entirely.** It makes the first test strip dramatically more targeted. An experienced printer might skip the test strip entirely for straightforward negatives. But for critical prints, a confirmation test strip is always wise — the metering gives you a great starting point, not a guaranteed final answer.
