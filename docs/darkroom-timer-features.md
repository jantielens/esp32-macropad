# Darkroom Enlarger Timer — Feature Analysis & Ranking
**Project:** DIY Darkroom Enlarger Timer (ESP32-P4 + C6, GUITION JC4880P433 4.3" Touch Display)  
**Date:** 2025-04-12 (Updated: 2025-04-13)  
**Lead:** Burns  
**Contributors:** Moe (Darkroom Printer SME), Homer (Hardware Engineer), Frink (Firmware Dev), Smithers (UI Dev)

---

## Project Vision & Market Context

### What We're Building

The **first open-source touchscreen darkroom enlarger timer** designed for advanced hobby photographers who demand professional features without commercial price tags. We're delivering RH Designs functionality (f-stop timing, split-grade automation, test strip generation) at Darkroom Automation pricing (~$300-400), with the modern touchscreen UX of Chronodev — in dedicated, darkroom-safe hardware.

This isn't another basic Arduino countdown timer. It's a **data-driven printing workstation** that logs sessions, stores print recipes, automates complex dodge/burn sequences, and helps photographers iterate toward perfect prints through intelligent exposure analysis.

### Hardware Platform: GUITION JC4880P433

**Display:** 4.3" capacitive touch IPS (800x480), 16M colors  
**Processors:** Dual-core ESP32-P4 (400MHz RISC-V) + ESP32-C6 (160MHz WiFi/BLE coprocessor)  
**Graphics:** LVGL 9.x framework with hardware acceleration  
**Connectivity:** WiFi 6, Bluetooth 5.3, USB-C  
**I/O:** GPIO expansion for relays, footswitches, sensors  

**Why This Platform Matters:**
- **First touchscreen darkroom timer** — no commercial or DIY competitor uses capacitive touch
- **LVGL framework** — professional-grade embedded UI with gesture support, animations, themes
- **Dual processors** — UI remains responsive during network operations or sensor polling
- **Hardware acceleration** — smooth 60fps animations even with complex dodge/burn visualizations
- **ESP32 ecosystem** — PlatformIO toolchain, thousands of open-source libraries, active community

### Market Position: The Touchscreen Opportunity

**We have a 12-18 month exclusive window.** Extensive competitive research (see `COMPETITIVE-ANALYSIS.md`) confirms **zero touchscreen hardware timers exist** — commercial or DIY.

**Commercial Landscape:**
- Premium f-stop timers ($400-2,000) use physical buttons/knobs and primitive LED/LCD displays
- RH Designs StopClock Pro (£389) is industry gold standard — but proprietary, button-driven, UK-centric
- Heiland Splitgrade (€1,750) offers ultimate automation — but extreme overkill for most printers
- Darkroom Automation ($305) proves US market accepts this price point for f-stop + memory

**DIY Landscape:**
- 80+ GitHub projects, mostly Arduino + 16x2 LCD or 7-segment displays
- Best firmware: lo1ol Red_Ray (9 timer modes, mask sequences) — still 16x2 LCD
- Best hardware: Printalyzer (isolated power board, metering) — TM1638 display
- Most advanced: Emberlite (spectral sensing, DMX512) — web UI only, no dedicated display

**Mobile Apps:**
- Chronodev (iOS) pioneered app-based timer with smart plug control — but faces "phone in darkroom" resistance
- DarkroomLog demonstrates unmet demand for session logging + equipment library + print photo attachment
- Screen brightness and "phone distraction" concerns limit app adoption

**Our Sweet Spot:**  
Professional features at accessible pricing, with modern touchscreen UX, in dedicated darkroom-safe hardware. We're not competing with $80 Paterson basic timers (wrong audience) or $1,900 Heiland automation (overkill). We sit in the $300-400 prosumer tier with **unique technology no competitor can match** for 12-18 months.

### Competitive Landscape: Key Players

**Top Commercial Timers:**

| Product | Price | Key Differentiator | Our Advantage |
|---------|-------|-------------------|---------------|
| RH Designs StopClock Pro | $445 | Industry gold standard, dual channels, dry-down compensation | We match features, add touchscreen, open-source, $100-150 cheaper |
| Darkroom Automation F-Stop Timer | $305 | Best US value, 300-step memory | We match price, add touchscreen + session logging |
| Heiland Splitgrade | $1,900 | Ultimate automation, 30+ paper calibrations | We target same workflow at 1/5 price with simpler UX |
| Filmomat F-Stop Timer | $750 | Premium build, 1000W output | We match features at half price with better UI |
| GraLab Model 300 | $375 | Electromechanical workhorse, glow-in-dark dial | We add digital precision + f-stop mode + memory |
| Paterson 2000D | $80-120 | Budget basic timer | Different market — we target advanced printers |

**Top DIY Projects:**

| Project | Platform | Key Differentiator | Our Advantage |
|---------|----------|-------------------|---------------|
| lo1ol Red_Ray (RR-1) | Arduino Nano | 9 timer modes, mask sequences, lag time compensation | We match firmware complexity, add touchscreen + recipe storage |
| Printalyzer | ESP32 | Custom PCB, isolated power board, metering integration | We adopt safety architecture, add modern UI |
| Emberlite | ESP32 | Spectral sensing (AS7343), DMX512, web UI | We focus on practical features, add dedicated touch display |
| Timerino | Arduino | F-stop + linear modes, dual hardware variants | We exceed feature set with better ergonomics |

**Mobile Apps:**

| App | Platform | Key Differentiator | Our Advantage |
|-----|----------|-------------------|---------------|
| Chronodev | iOS | First app-based timer, HomeKit integration | We eliminate phone dependency, add offline reliability |
| DarkroomLog | iOS/Android | Equipment library, print photo attachment | We integrate logging into hardware, add CSV export |

**Market Insight:** Session logging is an **unmet need** — zero commercial timers track exposure history. Apps prove demand (DarkroomLog users log every print), but photographers resist phones in darkroom. We uniquely address this gap.

**For full competitive analysis and 33 feature gaps identified, see:** `COMPETITIVE-ANALYSIS.md`

---

## Scoring Methodology

Each feature scored on five dimensions (1-5 scale, equal weight):

| Dimension | 1 (Low) | 5 (High) | Perspective |
|-----------|---------|----------|-------------|
| **Usefulness (U)** | Nice curiosity | Essential for printing | Moe: How much do printers need this? |
| **Feasibility (F)** | Research project | Straightforward to implement | Frink: How hard is the firmware/software? |
| **Hardware Simplicity (H)** | Complex custom hardware | No extra hardware needed | Homer: Hardware complexity *inverted* (5=best) |
| **UX Impact (UX)** | Minimal UX value | Transforms the experience | Smithers: Interface improvement? |
| **Differentiator (D)** | Every timer has this | No timer does this well | Burns: Market differentiation? |

**Composite Score** = U + F + H + UX + D (max 25)

---

## Core Timer Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| TIM01 | **Countdown Timer** | Classic count-down from set time to zero | 5 | 5 | 5 | 4 | 1 | **20** |
| TIM02 | **Pause/Resume** | Stop mid-exposure, resume without losing print | 5 | 5 | 5 | 5 | 2 | **22** |
| TIM03 | **Preset Banks (10+)** | Save named exposure times (e.g., "8x10 Grade 2") | 5 | 5 | 5 | 4 | 2 | **21** |
| TIM04 | **Quick-Add Increments** | +1s, +2s, +5s buttons for fast tweaking | 4 | 5 | 5 | 5 | 2 | **21** |
| TIM05 | **Count-Up Timer** | Stopwatch mode for developing trays | 4 | 5 | 5 | 3 | 1 | **18** |
| TIM06 | **Split Time Display** | Show "3s elapsed / 9s remaining" during exposure | 4 | 5 | 5 | 4 | 2 | **20** |
| TIM07 | **Pre-Exposure Warning** | 1-second beep before enlarger turns on | 4 | 5 | 4 | 4 | 2 | **19** |
| TIM08 | **End-of-Exposure Alert** | Audio + visual signal when time hits zero | 5 | 5 | 4 | 4 | 1 | **19** |
| TIM09 | **Footswitch Override** | Manual control, bypass timer for focus/framing | 5 | 4 | 4 | 3 | 2 | **18** |
| TIM10 | **Interval Timer** | Repeat exposure cycles (e.g., 8s × 5 test strips) | 4 | 4 | 5 | 4 | 2 | **19** |
| TIM11 | **Multi-Cycle Presets** | Save sequences with count (e.g., "5× 8s test strips") | 4 | 4 | 5 | 4 | 3 | **20** |
| TIM12 | **Haptic Feedback** | Vibration on button press/completion | 3 | 4 | 4 | 4 | 3 | **18** |
| TIM13 | **Lag Time Calibration** | Account for enlarger lamp warm-up delay (milliseconds) for precision timing | 4 | 4 | 5 | 4 | 3 | **20** |
| TIM14 | **Buzzer Every Second During Exposure** | Continuous audible tick during countdown for timing feedback without looking | 3 | 5 | 4 | 3 | 2 | **17** |
| TIM15 | **Metronome for Dodge/Burn Timing** | Continuous tick for manual dodging/burning rhythm | 3 | 5 | 4 | 4 | 3 | **19** |

---

## Exposure Control Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| EXP01 | **F-Stop Timer Mode** | Adjust exposure in ±½, ±1, ±2 stop increments | 5 | 4 | 5 | 5 | 4 | **23** |
| EXP02 | **Linear Timer Mode** | Traditional second-based timing (default) | 5 | 5 | 5 | 4 | 1 | **20** |
| EXP03 | **Reciprocity Compensation** | Auto-add time for long exposures (>30s) | 4 | 3 | 5 | 4 | 4 | **20** |
| EXP04 | **Contrast Grade Compensation** | Auto-adjust time when switching filter grades | 4 | 3 | 5 | 4 | 4 | **20** |
| EXP05 | **Percentage Adjustment** | Increase/decrease base time by % (+10%, -20%) | 4 | 5 | 5 | 4 | 2 | **20** |
| EXP06 | **Multi-Step Exposure Memory** | Program sequence (5s base + 3s burn + 2s dodge) | 5 | 4 | 5 | 5 | 3 | **22** |
| EXP07 | **Exposure Calculator** | Enter aperture/time, get equivalent at new aperture | 4 | 4 | 5 | 4 | 3 | **20** |
| EXP08 | **Base Time Reference** | Set baseline, all adjustments relative to base | 4 | 5 | 5 | 4 | 2 | **20** |
| EXP09 | **Dry-Down Compensation** | Adjusts exposure to account for print density changes as paper dries (wet prints appear darker) | 4 | 3 | 5 | 4 | 4 | **20** |

---

## Test Strip Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| TST01 | **Automated Test Strip Sequence** | Expose strip in steps (2s, 4s, 6s, 8s, 10s) | 5 | 4 | 5 | 5 | 4 | **23** |
| TST02 | **Exposure Wedge Generator** | Fine-grained increments (0.5s steps) | 4 | 4 | 5 | 4 | 3 | **20** |
| TST03 | **Test Strip Time Calculator** | Analyze strip, recommend full-frame time | 5 | 3 | 5 | 5 | 4 | **22** |
| TST04 | **Test Strip Templates** | Vertical/horizontal/grid patterns | 4 | 4 | 5 | 4 | 3 | **20** |
| TST05 | **Split-Filter Test Strip** | Run Grade 2 + Grade 4 sequences on same sheet | 4 | 4 | 5 | 4 | 4 | **21** |
| TST06 | **Test Strip Overlay** | Visual grid showing which segment is which time | 4 | 4 | 5 | 5 | 4 | **22** |
| TST07 | **Incremental vs. Cumulative** | Choose between adding time or cumulative totals | 4 | 4 | 5 | 4 | 3 | **20** |

---

## Dodging & Burning Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| DB01 | **Dodge/Burn Sequence Timer** | Multi-step timer with named steps | 5 | 4 | 5 | 5 | 3 | **22** |
| DB02 | **Visual Zone Map** | Draw areas on screen, assign times to each zone | 4 | 3 | 5 | 5 | 5 | **22** |
| DB03 | **Dodge Hold Alert** | Audio cue when it's time to remove dodging tool | 4 | 5 | 5 | 4 | 3 | **21** |
| DB04 | **Burn Countdown** | "Burn corner: 3s remaining" voice/visual cue | 4 | 4 | 5 | 5 | 3 | **21** |
| DB05 | **Multi-Stage Burn** | Chain burns (sky 3s → corner 2s → edge 1s) | 5 | 4 | 5 | 5 | 3 | **22** |
| DB06 | **Save Dodge/Burn Recipe** | Store complete sequence with names | 5 | 4 | 5 | 4 | 3 | **21** |

---

## Split-Grade Printing Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| SPL01 | **Split-Grade Dual Timer** | Set Grade 2 time + Grade 4 time, run both | 5 | 4 | 5 | 5 | 4 | **23** |
| SPL02 | **Grade Balance Slider** | Adjust contrast ratio (more G2 vs. more G4) | 4 | 4 | 5 | 5 | 4 | **22** |
| SPL03 | **Split-Grade Preset Library** | Save named configs (e.g., "High Key Portrait") | 5 | 4 | 5 | 4 | 3 | **21** |
| SPL04 | **Filter Change Reminder** | Prompt to change filter between exposures | 4 | 5 | 5 | 4 | 2 | **20** |
| SPL05 | **Exposure Ratio Calculator** | Compute G2:G4 ratio from test prints | 4 | 3 | 5 | 4 | 4 | **20** |

---

## Metering & Analysis Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| MET01 | **Light Meter Integration** | External sensor measures enlarger brightness | 4 | 3 | 3 | 4 | 4 | **18** |
| MET02 | **Auto Exposure Calculation** | Meter negative, suggest base time | 5 | 2 | 2 | 5 | 5 | **19** |
| MET03 | **Print Densitometer Mode** | Measure print density with sensor | 3 | 2 | 2 | 3 | 5 | **15** |
| MET04 | **Negative Analysis** | Scan negative, recommend exposure/grade | 4 | 1 | 1 | 5 | 5 | **16** |
| MET05 | **Enlargement Factor Compensation** | Adjust time for height changes | 4 | 4 | 5 | 4 | 3 | **20** |
| MET06 | **Paper Speed Database** | Built-in profiles for common papers | 4 | 4 | 5 | 4 | 3 | **20** |
| MET07 | **Light Output Compensation / Integrator** | Closed-loop monitoring of enlarger light at easel; compensates for lamp warm-up, voltage fluctuations, aging | 5 | 3 | 3 | 5 | 5 | **21** |
| MET08 | **Grey Scale Tone Indicator (Single Probe)** | Single TSL2591 probe on easel; user positions at highlight/shadow/midtone areas; computes SBR, recommends exposure time + contrast grade; zone placement preview (RH Analyser Pro approach) | 5 | 3 | 3 | 5 | 5 | **21** |
| MET09 | **Densitometer Function** | Measure negative density in transmission mode; handles Pyro stains | 3 | 2 | 2 | 3 | 5 | **15** |
| MET10 | **User Override / Creative Adjustments** | Allow user to intentionally deviate from metered recommendation for creative effect | 4 | 4 | 5 | 4 | 3 | **20** |
| MET11 | **Automatic Grade Control Display** | Real-time preview of how tonal values change as you adjust paper grade | 4 | 3 | 5 | 5 | 4 | **21** |
| MET12 | **Filter Spectral Sampling / Library** | Measure and save spectral characteristics of filters; match filters to desired output | 2 | 2 | 2 | 3 | 5 | **14** |
| MET13 | **Closed-Loop Calibration (CCAL)** | Auto-adjust exposure for paper/developer batch variations over time | 3 | 2 | 3 | 4 | 5 | **17** |
| MET14 | **B&W G/B Channel Estimation** | Intelligent metering for split-grade printing (estimate green/blue channel needs) | 4 | 3 | 3 | 4 | 4 | **18** |
| MET15 | **Multi-Sensor Easel Array** | Grid of 5-8 TSL2591 sensors via TCA9548A I2C multiplexer on flat board; instant tonal histogram of projected image; automatic highlight/shadow/midtone detection; dodge/burn candidate identification; no manual probing required | 4 | 2 | 2 | 5 | 5 | **18** |

### MET08 — Grey Scale Tone Indicator (Single Probe)

**Concept:** A single TSL2591 high-dynamic-range light sensor on a cable-connected probe that the user positions on the easel under the projected negative image. The timer takes spot readings at key areas (highlight, shadow, midtone), computes the Subject Brightness Range (SBR), and recommends both exposure time and contrast filter grade — enabling zone placement preview BEFORE making an exposure. This is the approach proven by the **RH Designs Analyser Pro** and **ZoneMaster**.

**Algorithm:**
1. **Reference reading (paper white):** No negative in carrier, lens open, sensor on easel → record `Lref` (maximum light at easel plane)
2. **Spot readings:** User positions probe at 3-10 key areas of projected image, presses button for each → record `Lspot` values
3. **Effective density:** For each spot, compute `D = log10(Lref / Lspot)` — this yields the effective printing density at that image area
4. **Subject Brightness Range:** `SBR = max(D) - min(D)` across all measured spots
5. **Contrast grade recommendation:** SBR maps to paper grade — higher range → lower grade (softer), lower range → higher grade (harder). Standard mapping examples: ~1.0 density range → grade 5, ~1.4 → grade 3, ~1.8 → grade 2, ~2.4 → grade 0
6. **Exposure time:** The "key tone" reading (user-selected, typically midtone/Zone V area) combined with paper speed profile (MET06) determines base exposure time
7. **Zone placement preview:** Display shows where each measured spot falls on the Zone 0–X scale at the recommended grade

**Hardware:**
- 1× TSL2591 sensor on small breakout PCB (~$7), connected via 3-4 wire cable (I2C + power) to timer
- Probe housing: small puck or wand that sits flat on easel, sensor facing up toward enlarger lens
- Same sensor can serve MET07 (light output compensation) when repositioned or during exposure — dual-purpose hardware

**Dependencies:** MET06 (Paper Speed Database) for accurate time recommendations; enhanced by MET10 (User Override) for creative deviation from recommendation; feeds into EXP04/EXP05 (test strip modes) for focused test strips

**UX Flow:**
1. User enters "Meter" mode on touchscreen
2. Timer prompts: "Remove negative, open lens" → takes reference reading → confirms Lref
3. Timer prompts: "Insert negative" → user positions probe → taps screen (or footswitch) per spot
4. Each spot reading appears on a zone scale graphic in real-time
5. After 3+ readings, timer displays: recommended grade, recommended base time, zone map of all readings
6. User can accept recommendation → goes directly to exposure, or → goes to focused test strip (±½ stop around recommendation)

**Competitive Reference:** RH Designs Analyser Pro (£389), RH Designs ZoneMaster (£499), Heiland TRD-2 — all use single-probe approach. Our advantage: touchscreen zone visualization, integrated with timer workflow, open-source calibration.

---

### MET15 — Multi-Sensor Easel Array

**Concept:** A flat board with 5-8 TSL2591 sensors arranged in a grid pattern, connected via a TCA9548A I2C multiplexer. Place the board on the easel, press one button, and get an instant tonal histogram of the entire projected image — automatic highlight/shadow/midtone detection with no manual probing. This is a **novel approach that no commercial darkroom timer has implemented**.

**How It Extends MET08:**
- MET08 (single probe) requires the user to manually position and take sequential spot readings
- MET15 takes simultaneous readings across the image area in one pass
- The same algorithm applies (reference → density → SBR → grade/time), but with richer spatial data

**Hardware:**
- 1× TCA9548A I2C multiplexer (~$2) — supports up to 8 identical-address I2C devices on one bus
- 5-8× TSL2591 sensors (~$7 each) soldered to a flat PCB in a grid pattern (e.g., 3×3 minus center = 8, or 2×3 = 6)
- Flat board sized to fit within common easel sizes (e.g., 8×10" or 11×14")
- Cable back to timer (I2C bus + power, 4 wires)
- **Estimated BOM for sensor board:** $40-60

**Algorithm (extends MET08):**
1. **Reference reading:** All sensors read simultaneously with no negative → per-sensor `Lref` calibration (accounts for light falloff toward edges)
2. **Image reading:** All sensors read simultaneously through negative → per-sensor `Lspot`
3. **Density map:** Compute `D = log10(Lref_i / Lspot_i)` for each sensor position
4. **Tonal histogram:** Display distribution of densities across zones — shows how many sensor positions fall in shadows, midtones, highlights
5. **SBR + recommendation:** Same as MET08 but computed from full array data — more reliable than 3 manual spot readings
6. **Dodge/burn candidates:** Sensor positions that fall outside desired zone range are flagged — "Position 3 (upper-left) reads Zone IX — consider burning 1 stop"

**UX Flow:**
1. User places sensor board flat on easel
2. Calibration: timer takes reference reading (no negative) — one-time per session or enlarger height change
3. User inserts negative, presses "Analyze" on touchscreen
4. Timer reads all sensors simultaneously (~200-600ms integration time)
5. Display shows: tonal histogram, zone map overlay (grid matching sensor positions), recommended grade + time, flagged dodge/burn areas
6. User accepts → exposure, or → focused test strip, or → adjusts and re-reads

**Key Advantages Over Single Probe:**
- Speed: one button press vs. 5-10 manual probe positions
- Consistency: eliminates user positioning variability
- Spatial data: reveals light falloff, vignetting, and tonal distribution across the print
- Dodge/burn guidance: identifies areas that need special attention

**Dependencies:** MET08 must be validated first (algorithm, calibration, paper profiles). MET06 (Paper Speed Database). Enhanced by DB02 (Visual Zone Map) — overlay sensor readings on a print thumbnail on the touchscreen.

**R&D Considerations:**
- Sensor board layout must account for enlarger light cone angle (sensors should have narrow acceptance angle or baffles)
- Per-sensor calibration needed to normalize for manufacturing variation and position-dependent light falloff
- Board size vs. print size: may need different boards for different print sizes, or a "representative sample" approach
- Integration with MET07: array can also serve as multi-point light integrator during exposure

**Competitive Reference:** No commercial darkroom timer offers this. The closest concept is the Heiland Splitgrade's sensor-driven automation, but that uses a single sensor. Multi-point light metering exists in camera systems (e.g., evaluative/matrix metering) but has never been applied to enlarger printing.

---

## Paper & Chemistry Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| CHM01 | **Paper Profiles** | Save speed/contrast curves for each paper | 4 | 4 | 5 | 4 | 3 | **20** |
| CHM02 | **Developer Timer** | Separate countdown for dev/stop/fix | 5 | 5 | 5 | 4 | 2 | **21** |
| CHM03 | **Chemistry Temperature Monitor** | Display dev tray temp via probe | 4 | 4 | 3 | 4 | 3 | **18** |
| CHM04 | **Development Time Adjustment** | Auto-adjust for temp (e.g., 68°F = +20s) | 4 | 3 | 3 | 4 | 4 | **18** |
| CHM05 | **Chemistry Age Tracking** | Log when developer was mixed, warn when old | 3 | 4 | 5 | 3 | 3 | **18** |
| CHM06 | **Multi-Timer Mode** | Run exposure + dev + stop + fix timers in parallel | 5 | 4 | 5 | 5 | 3 | **22** |
| CHM07 | **Chemistry Dilution/Volume Calculator** | Integrated chemistry mixing tools (volume, dilution ratios) | 3 | 4 | 5 | 3 | 2 | **17** |
| CHM08 | **Agitation Interval Reminders** | Per-step agitation reminders (e.g., "agitate 5s every 30s") | 4 | 4 | 5 | 4 | 3 | **20** |

---

## Workflow & Automation Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| WRK01 | **Session Logging** | Record all exposures with metadata | 5 | 4 | 5 | 4 | 3 | **21** |
| WRK02 | **Print Recipe Storage** | Save complete print formula (times, filters, etc.) | 5 | 4 | 5 | 5 | 3 | **22** |
| WRK03 | **Batch Printing Mode** | Repeat same exposure for multiple prints | 5 | 5 | 5 | 4 | 2 | **21** |
| WRK04 | **CSV Export** | Export session logs for analysis | 3 | 4 | 5 | 3 | 2 | **17** |
| WRK05 | **Recipe Import/Export** | Share recipes with other printers | 4 | 4 | 5 | 4 | 3 | **20** |
| WRK06 | **Negative Library** | Tag negatives, link to print recipes | 4 | 3 | 5 | 4 | 4 | **20** |
| WRK07 | **Print Counter** | Track total prints per session/lifetime | 3 | 5 | 5 | 3 | 2 | **18** |
| WRK08 | **Auto-Save State** | Resume session after power loss | 5 | 4 | 4 | 5 | 3 | **21** |
| WRK09 | **Equipment Library** | Store enlarger, lens, paper, chemistry details for fast session setup | 5 | 4 | 5 | 5 | 4 | **23** |
| WRK10 | **Print Photo Attachment** | Attach photo of finished print to exposure record for visual reference | 4 | 3 | 5 | 4 | 4 | **20** |

---

## UI/UX Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| UIX01 | **Red Safe Mode** | Deep red display (625-700nm) for paper safety | 5 | 5 | 5 | 5 | 2 | **22** |
| UIX02 | **Amber Safe Mode** | Amber display for less sensitive materials | 4 | 5 | 5 | 4 | 2 | **20** |
| UIX03 | **Brightness Control** | Dim display without changing color temp | 5 | 5 | 5 | 5 | 1 | **21** |
| UIX04 | **Large Display Numerals** | Huge time readout for dark viewing | 5 | 5 | 5 | 5 | 2 | **22** |
| UIX05 | **Touch Gestures** | Swipe/pinch for nav and adjustment | 4 | 4 | 5 | 5 | 3 | **21** |
| UIX06 | **Rotary Encoder Support** | Physical dial for time adjustment | 4 | 4 | 4 | 5 | 3 | **20** |
| UIX07 | **Glove-Friendly Touch** | Large buttons, wet-hand compatible | 5 | 4 | 5 | 5 | 3 | **22** |
| UIX08 | **One-Handed Mode** | Critical controls in thumb zone | 5 | 4 | 5 | 5 | 3 | **22** |
| UIX09 | **Audio Feedback** | Tones for button press, warnings, completion | 5 | 5 | 4 | 4 | 2 | **20** |
| UIX10 | **Screensaver Mode** | Dim/blank screen during long inactivity | 4 | 5 | 5 | 4 | 2 | **20** |
| UIX11 | **High Contrast Mode** | Black/white for low vision users | 3 | 5 | 5 | 4 | 2 | **19** |
| UIX12 | **Customizable Layout** | Move buttons, resize widgets | 3 | 3 | 5 | 4 | 3 | **18** |
| UIX13 | **Undo/Redo** | Revert accidental changes | 4 | 4 | 5 | 5 | 2 | **20** |
| UIX14 | **Melody/Musical Alerts for Steps** | Different melodies for filter changes, step transitions (richer than beep) | 3 | 4 | 5 | 4 | 3 | **19** |
| UIX15 | **Whole-Screen Touch Button** | Entire screen acts as start/stop button (glove/wet-hand friendly) | 4 | 4 | 5 | 5 | 4 | **22** |
| UIX16 | **Display Blackout/Cover** | Physical or software cover to block ALL light for color printing | 3 | 4 | 5 | 3 | 3 | **18** |
| UIX17 | **Glow-in-the-Dark Controls** | Phosphorescent labels/switch positions for total darkness operation | 2 | 4 | 4 | 3 | 3 | **16** |
| UIX18 | **Display Flicker-Free at Low Brightness** | PWM frequency high enough to avoid visible flicker in darkroom | 4 | 4 | 5 | 5 | 3 | **21** |

---

## Connectivity Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| CON01 | **WiFi Setup (Web UI)** | Configure timer via phone/laptop browser | 4 | 4 | 5 | 5 | 3 | **21** |
| CON02 | **BLE Remote Control** | Phone app for wireless control | 4 | 3 | 4 | 5 | 4 | **20** |
| CON03 | **Cloud Recipe Sync** | Backup/restore recipes to cloud | 3 | 3 | 5 | 4 | 3 | **18** |
| CON04 | **MQTT Integration** | Publish events for home automation | 2 | 3 | 5 | 2 | 4 | **16** |
| CON05 | **OTA Firmware Updates** | Update firmware over WiFi | 4 | 4 | 5 | 4 | 3 | **20** |
| CON06 | **Remote Display Mirror** | View timer screen on phone/tablet | 3 | 3 | 5 | 4 | 4 | **19** |
| CON07 | **Multi-Timer Network** | Link multiple timers in one darkroom | 2 | 2 | 4 | 3 | 5 | **16** |
| CON08 | **Recipe Sharing Community** | Upload/download recipes from community | 3 | 2 | 5 | 4 | 4 | **18** |
| CON09 | **Background Notifications** | Alerts if you leave web UI during timer; wash timer continues in background | 3 | 4 | 5 | 3 | 3 | **18** |

---

## Hardware Add-ons Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| HW01 | **Relay Output (Mains)** | Control AC enlarger lamp (solid-state relay, 1000W+ rating) | 5 | 4 | 3 | 3 | 2 | **17** |
| HW02 | **Foot Switch Input** | Start/pause with foot pedal (1/4" jack, sustain pedal type) | 5 | 5 | 4 | 5 | 2 | **21** |
| HW03 | **Second Foot Switch** | Second pedal for burn/dodge control | 4 | 5 | 4 | 4 | 3 | **20** |
| HW04 | **External Beeper** | Loud buzzer for noisy darkrooms | 4 | 5 | 4 | 4 | 2 | **19** |
| HW05 | **Light Sensor Input** | BH1750 or TSL2561 for metering | 4 | 4 | 3 | 4 | 4 | **19** |
| HW06 | **Temperature Probe Input** | DS18B20 for chemistry monitoring | 4 | 5 | 4 | 4 | 3 | **20** |
| HW07 | **GPIO Expansion Header** | Add custom sensors/controls | 3 | 4 | 4 | 3 | 4 | **18** |
| HW08 | **I2C/SPI Breakout** | Connect advanced sensors | 2 | 4 | 4 | 2 | 3 | **15** |
| HW09 | **USB-C Power + Data** | Single cable for power and programming | 5 | 5 | 5 | 4 | 2 | **21** |
| HW10 | **Safelight Auto-On/Off** | Automatically turn safelight OFF during exposure, ON afterward (dual relay or second channel) | 5 | 4 | 3 | 5 | 4 | **21** |
| HW11 | **DMX512 Lighting Control** | Control RGBW LED enlarger head via DMX512 protocol (replaces physical filters) | 2 | 2 | 2 | 3 | 5 | **14** |
| HW12 | **Wall-Mount / Tabletop Flexibility** | Mounting options for different darkroom layouts | 3 | 5 | 4 | 3 | 2 | **17** |
| HW13 | **Separate Enlarger + Safelight Outlets** | Independent control of two AC outlets (150W safelight + 1000W enlarger) | 5 | 4 | 3 | 4 | 3 | **19** |
| HW14 | **Universal Voltage (100-240V AC)** | Global voltage compatibility | 3 | 3 | 3 | 2 | 3 | **14** |
| HW15 | **1.54" E-Paper Display** | Secondary display for zero-emission timer info during printing (reflective, unreadable in darkness) | 2 | 2 | 3 | 2 | 4 | **13** |

---

## Safety & Reliability Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| SAF01 | **Watchdog Timer** | Auto-restart if firmware hangs | 5 | 5 | 5 | 3 | 2 | **20** |
| SAF02 | **Power Backup (Supercap)** | Save state during brief outages | 4 | 3 | 3 | 4 | 3 | **17** |
| SAF03 | **Mains Isolation** | Opto-isolated relay control for safety | 5 | 4 | 3 | 2 | 2 | **16** |
| SAF04 | **Fail-Safe Relay** | Enlarger turns OFF on system failure | 5 | 4 | 3 | 3 | 2 | **17** |
| SAF05 | **Over-Temperature Shutdown** | Protect hardware from overheating | 4 | 5 | 5 | 2 | 2 | **18** |
| SAF06 | **Auto-Shutoff in View/Focus Mode** | Automatically turn off enlarger if left in focus mode (prevents lamp burnout) | 5 | 4 | 5 | 4 | 3 | **21** |
| SAF07 | **CRC32 Data Integrity** | EEPROM data verification to prevent corruption | 4 | 4 | 5 | 2 | 3 | **18** |

---

## Advanced/Visionary Features

| ID | Feature | Description | U | F | H | UX | D | **Score** |
|------|---------|-------------|---|---|---|----|----|-----------|
| ADV01 | **Voice Control** | Verbal commands ("pause", "add 2 seconds") | 3 | 2 | 4 | 5 | 5 | **19** |
| ADV02 | **AI Exposure Recommendations** | ML model suggests settings from negative | 3 | 1 | 3 | 5 | 5 | **17** |
| ADV03 | **Computer Vision Test Strip Analysis** | Camera analyzes strip, picks best segment | 4 | 1 | 2 | 5 | 5 | **17** |
| ADV04 | **Motorized Filter Drawer** | Auto-insert/remove filters | 3 | 2 | 1 | 4 | 5 | **15** |
| ADV05 | **Enlarger Head Height Sensor** | Ultrasonic measures height, compensates exposure | 3 | 3 | 3 | 4 | 4 | **17** |
| ADV06 | **Negative Scanning Integration** | Import scans, compute ideal print settings | 3 | 1 | 3 | 4 | 5 | **16** |
| ADV07 | **Spectral Analysis** | Measure light spectrum for color printing | 2 | 1 | 2 | 3 | 5 | **13** |
| ADV08 | **Print Quality Feedback Loop** | Learn from past prints, improve recommendations | 3 | 1 | 4 | 4 | 5 | **17** |
| ADV09 | **Shutter Tester Mode** | Test camera shutters (1/2000 to 6s) — dual-use tool | 3 | 3 | 5 | 3 | 4 | **18** |

---

## Global Top 25 (Best MVE Candidates)

Ranked by composite score (re-evaluated with new features):

| Rank | ID | Feature | Category | U | F | H | UX | D | **Score** |
|------|------|---------|----------|---|---|---|----|----|-----------|
| 1 | EXP01 | F-Stop Timer Mode | Exposure Control | 5 | 4 | 5 | 5 | 4 | **23** |
| 2 | TST01 | Automated Test Strip Sequence | Test Strips | 5 | 4 | 5 | 5 | 4 | **23** |
| 3 | SPL01 | Split-Grade Dual Timer | Split-Grade | 5 | 4 | 5 | 5 | 4 | **23** |
| 4 | WRK09 | Equipment Library | Workflow | 5 | 4 | 5 | 5 | 4 | **23** |
| 5 | TIM02 | Pause/Resume | Core Timer | 5 | 5 | 5 | 5 | 2 | **22** |
| 6 | EXP06 | Multi-Step Exposure Memory | Exposure Control | 5 | 4 | 5 | 5 | 3 | **22** |
| 7 | DB01 | Dodge/Burn Sequence Timer | Dodging & Burning | 5 | 4 | 5 | 5 | 3 | **22** |
| 8 | DB02 | Visual Zone Map | Dodging & Burning | 4 | 3 | 5 | 5 | 5 | **22** |
| 9 | DB05 | Multi-Stage Burn | Dodging & Burning | 5 | 4 | 5 | 5 | 3 | **22** |
| 10 | TST03 | Test Strip Time Calculator | Test Strips | 5 | 3 | 5 | 5 | 4 | **22** |
| 11 | TST06 | Test Strip Overlay | Test Strips | 4 | 4 | 5 | 5 | 4 | **22** |
| 12 | SPL02 | Grade Balance Slider | Split-Grade | 4 | 4 | 5 | 5 | 4 | **22** |
| 13 | CHM06 | Multi-Timer Mode | Paper & Chemistry | 5 | 4 | 5 | 5 | 3 | **22** |
| 14 | WRK02 | Print Recipe Storage | Workflow | 5 | 4 | 5 | 5 | 3 | **22** |
| 15 | UIX01 | Red Safe Mode | UI/UX | 5 | 5 | 5 | 5 | 2 | **22** |
| 16 | UIX04 | Large Display Numerals | UI/UX | 5 | 5 | 5 | 5 | 2 | **22** |
| 17 | UIX07 | Glove-Friendly Touch | UI/UX | 5 | 4 | 5 | 5 | 3 | **22** |
| 18 | UIX08 | One-Handed Mode | UI/UX | 5 | 4 | 5 | 5 | 3 | **22** |
| 19 | UIX15 | Whole-Screen Touch Button | UI/UX | 4 | 4 | 5 | 5 | 4 | **22** |
| 20 | TIM03 | Preset Banks (10+) | Core Timer | 5 | 5 | 5 | 4 | 2 | **21** |
| 21 | TIM04 | Quick-Add Increments | Core Timer | 4 | 5 | 5 | 5 | 2 | **21** |
| 22 | DB03 | Dodge Hold Alert | Dodging & Burning | 4 | 5 | 5 | 4 | 3 | **21** |
| 23 | DB04 | Burn Countdown | Dodging & Burning | 4 | 4 | 5 | 5 | 3 | **21** |
| 24 | DB06 | Save Dodge/Burn Recipe | Dodging & Burning | 5 | 4 | 5 | 4 | 3 | **21** |
| 25 | TST05 | Split-Filter Test Strip | Test Strips | 4 | 4 | 5 | 4 | 4 | **21** |

**Notable Changes from Initial Top 25:**
- **WRK09 (Equipment Library)** enters at Rank 4 with score 23 — competitive analysis revealed this is a major unmet need (DarkroomLog app proves demand)
- **UIX15 (Whole-Screen Touch Button)** enters at Rank 19 with score 22 — perfect for touchscreen, glove-friendly UX
- **CHM02 (Developer Timer)** drops out — still important (score 21) but now at Rank 26
- **SPL03 (Split-Grade Preset Library)** drops out — also still valuable (score 21) but now at Rank 27

---

## High-Differentiator Features (D=5)

Features with D=5 (unique to market, no competitor does this well):

| ID | Feature | Category | U | F | H | UX | D | **Score** | Notes |
|------|---------|----------|---|---|---|----|----|-----------|-------|
| DB02 | Visual Zone Map | Dodging & Burning | 4 | 3 | 5 | 5 | 5 | **22** | **MVE candidate** — touchscreen enables drawing zones on print thumbnail |
| MET02 | Auto Exposure Calculation | Metering | 5 | 2 | 2 | 5 | 5 | **19** | Requires light sensor (v2) |
| MET03 | Print Densitometer Mode | Metering | 3 | 2 | 2 | 3 | 5 | **15** | Niche use case, low feasibility |
| MET04 | Negative Analysis | Metering | 4 | 1 | 1 | 5 | 5 | **16** | Visionary, R&D required |
| MET07 | Light Output Compensation / Integrator | Metering | 5 | 3 | 3 | 5 | 5 | **21** | **High priority v2** — RH Vario/Metrolux II have this, separates pro timers from toys |
| MET08 | Grey Scale Tone Indicator (Single Probe) | Metering | 5 | 3 | 3 | 5 | 5 | **21** | **Tier 2** — proven approach (RH Analyser Pro), single TSL2591 probe, zone placement + grade/time recommendation |
| MET12 | Filter Spectral Sampling / Library | Metering | 2 | 2 | 2 | 3 | 5 | **14** | Future/experimental |
| MET13 | Closed-Loop Calibration (CCAL) | Metering | 3 | 2 | 3 | 4 | 5 | **17** | Emberlite pioneered this, medium priority v2 |
| MET15 | Multi-Sensor Easel Array | Metering | 4 | 2 | 2 | 5 | 5 | **18** | **Tier 3** — novel (no commercial timer does this), TCA9548A + 5-8× TSL2591 grid, instant tonal histogram |
| ADV01 | Voice Control | Advanced | 3 | 2 | 4 | 5 | 5 | **19** | Novelty value, feasible with ESP32 voice wake |
| ADV02 | AI Exposure Recommendations | Advanced | 3 | 1 | 3 | 5 | 5 | **17** | Visionary, defer to v3+ |
| ADV03 | Computer Vision Test Strip Analysis | Advanced | 4 | 1 | 2 | 5 | 5 | **17** | Visionary, defer to v3+ |
| ADV04 | Motorized Filter Drawer | Advanced | 3 | 2 | 1 | 4 | 5 | **15** | Visionary, complex hardware |
| ADV06 | Negative Scanning Integration | Advanced | 3 | 1 | 3 | 4 | 5 | **16** | Visionary, defer to v3+ |
| ADV07 | Spectral Analysis | Advanced | 2 | 1 | 2 | 3 | 5 | **13** | Visionary, Emberlite territory |
| ADV08 | Print Quality Feedback Loop | Advanced | 3 | 1 | 4 | 4 | 5 | **17** | Visionary, ML required |
| HW11 | DMX512 Lighting Control | Hardware | 2 | 2 | 2 | 3 | 5 | **14** | Emberlite has this, very niche |
| CON07 | Multi-Timer Network | Connectivity | 2 | 2 | 4 | 3 | 5 | **16** | Future, multi-darkroom scenarios |

**Key Insight:** **DB02 (Visual Zone Map, score 22)**, **MET07 (Light Output Compensation, score 21)**, and **MET08 (Grey Scale Tone Indicator, score 21)** are all D=5 AND high-scoring enough to warrant serious Tier 2/v2 consideration. MET08 shares hardware with MET07 (same TSL2591 sensor), making them a natural pair. MET15 (Multi-Sensor Easel Array, score 18) extends MET08 into novel territory no commercial timer has explored — strong Tier 3 candidate.

---

## MVE Scoping Recommendations

Re-evaluated based on complete feature set + competitive analysis insights:

### Tier 1: Core MVE (Must Have)
**Scope:** Basic timer that works flawlessly for straight prints + foundational differentiators

**Essential Timer Foundation:**
- TIM01: Countdown Timer (score: 20)
- TIM02: Pause/Resume (score: 22)
- TIM03: Preset Banks 10+ (score: 21)
- TIM04: Quick-Add Increments (score: 21)
- TIM08: End-of-Exposure Alert (score: 19)

**Core Differentiators (Cannot Launch Without):**
- EXP01: F-Stop Timer Mode (score: 23) — **#1 differentiator, table stakes for prosumer market**
- EXP02: Linear Timer Mode (score: 20) — baseline compatibility
- UIX01: Red Safe Mode (score: 22) — darkroom safety requirement
- UIX04: Large Display Numerals (score: 22) — dark viewing essential
- UIX07: Glove-Friendly Touch (score: 22) — touchscreen must work with wet/gloved hands
- UIX08: One-Handed Mode (score: 22) — ergonomic requirement

**Hardware Must-Haves:**
- HW01: Relay Output Mains, 1000W (score: 17) — core functionality
- HW02: Foot Switch Input, 1/4" jack (score: 21) — darkroom standard expectation
- SAF01: Watchdog Timer (score: 20) — reliability baseline

**Why:** Without these 14 features, we're not a credible darkroom timer. F-stop mode is non-negotiable for our target market.

### Tier 2: Differentiators (Should Have for Competitive Edge)
**Scope:** Features that make us stand out from RH Designs and DIY competition

**High-Value Automation (Scores 22-23):**
- TST01: Automated Test Strip Sequence (score: 23) — massive pain point, RH/Filmomat have this
- SPL01: Split-Grade Dual Timer (score: 23) — professional tier requirement, matches RH StopClock Pro
- EXP06: Multi-Step Exposure Memory (score: 22) — complex prints require sequences
- DB01: Dodge/Burn Sequence Timer (score: 22) — intermediate-to-advanced workflow support
- DB05: Multi-Stage Burn (score: 22) — chained burns essential for nuanced printing

**Workflow Memory (Scores 21-23):**
- WRK02: Print Recipe Storage (score: 22) — save complete formulas, major differentiator
- WRK09: Equipment Library (score: 23) — **competitive analysis revealed this is unmet market need** (DarkroomLog app proves demand)
- WRK01: Session Logging (score: 21) — **zero commercial timers log exposures**, huge gap
- WRK08: Auto-Save State (score: 21) — power-loss protection for print in progress

**UX Polish:**
- UIX15: Whole-Screen Touch Button (score: 22) — **new gap feature**, perfect for touchscreen + gloves
- UIX03: Brightness Control (score: 21) — darkroom ergonomics
- UIX05: Touch Gestures (score: 21) — leverage touchscreen platform
- CON01: WiFi Setup Web UI (score: 21) — modern expectation, simplifies config

**Hardware (If feasible):**
- HW10: Safelight Auto-On/Off (score: 21) — **found in most professional timers**, standard expectation. **Requires dual relay or second channel** — if we're doing isolated power board (Printalyzer architecture), add this outlet.

**Metering (Competitive Edge — Requires TSL2591 Sensor):**
- MET08: Grey Scale Tone Indicator / Single Probe (score: 21) — **proven by RH Analyser Pro & ZoneMaster**, single TSL2591 probe on easel, user takes spot readings at highlight/shadow/midtone, timer computes SBR and recommends exposure time + contrast grade. Zone placement preview eliminates test strips. **Shares hardware with MET07** (same sensor), making them a natural pair.

**Why:** These 15 features (or 16 with HW10, 17 with MET08) represent our **competitive moat**. Equipment library + session logging address unmet market need. Split-grade + test strips + recipes match RH Designs functionality at lower price. Touchscreen UX features (whole-screen button, gestures) leverage our unique platform advantage. MET08 shares hardware with MET07, so if we add a TSL2591 for light compensation, zone placement metering comes essentially "for free" in firmware.

**Critical Decision Point:** If hardware supports dual relay (enlarger + safelight), include HW10. It's table stakes for professional tier.

### Tier 3: Post-MVE v2 (Nice to Have)
**Scope:** Advanced workflow + metering features for power users

**Metering & Analysis (Post-Hardware v2):**
- MET07: Light Output Compensation / Integrator (score: 21) — **RH Vario/Metrolux II have this**, separates pro timers from toys. Requires light sensor hardware.
- MET15: Multi-Sensor Easel Array (score: 18, D=5) — **novel, no commercial timer does this**. Grid of 5-8× TSL2591 sensors via TCA9548A I2C multiplexer on flat board placed on easel. Instant tonal histogram of projected image, automatic highlight/shadow/midtone detection, dodge/burn candidate identification. Depends on MET08 (single-probe) being validated first.
- MET11: Automatic Grade Control Display (score: 21) — real-time tone preview, great for split-grade
- MET05: Enlargement Factor Compensation (score: 20) — height change compensation
- MET06: Paper Speed Database (score: 20) — built-in profiles for Ilford, Foma, etc.

**Advanced Workflow:**
- TST03: Test Strip Time Calculator (score: 22) — analyze strip, recommend time
- TST06: Test Strip Overlay (score: 22) — visual grid showing segment times
- DB02: Visual Zone Map (score: 22, D=5) — **unique touchscreen capability**, draw zones on print
- CHM06: Multi-Timer Mode (score: 22) — exposure + dev + stop + fix in parallel
- WRK10: Print Photo Attachment (score: 20) — DarkroomLog app has this, proven useful

**Safety & Reliability:**
- SAF06: Auto-Shutoff in View/Focus Mode (score: 21) — **lo1ol Red_Ray has this**, prevents lamp burnout
- TIM13: Lag Time Calibration (score: 20) — lamp warm-up delay precision

**Connectivity:**
- CON05: OTA Firmware Updates (score: 20) — continuous improvement path
- WRK04: CSV Export (score: 17) — data-driven iteration

**Why:** These features add depth for power users but aren't launch-critical. Metering requires additional hardware sensor ($10-20 BOM increase). MET15 (multi-sensor array) extends MET08's single-probe approach into novel territory — automatic tonal mapping without manual probing. Visual zone mapping is uniquely enabled by touchscreen — strong v2 candidate.

### Tier 4: Future/Visionary (R&D)
**Scope:** Exploratory features requiring research, v3+ or special editions

**AI/ML (Feasibility F=1-2):**
- ADV02: AI Exposure Recommendations (score: 17)
- ADV03: Computer Vision Test Strip Analysis (score: 17)
- ADV08: Print Quality Feedback Loop (score: 17)

**Advanced Metering (Requires complex sensors):**
- MET13: Closed-Loop Calibration (score: 17) — Emberlite has CCAL
- MET14: B&W G/B Channel Estimation (score: 18) — split-grade metering
- MET12: Filter Spectral Sampling (score: 14) — spectral library

**Motorization & Novelty:**
- ADV04: Motorized Filter Drawer (score: 15)
- ADV01: Voice Control (score: 19) — "Alexa for darkroom"
- HW11: DMX512 Lighting Control (score: 14) — Emberlite RGBW territory

**Why:** High differentiator (D=4-5) but low feasibility (F=1-2). These are "iPhone moment" visions that require R&D breakthroughs. Great for post-MVE special editions (e.g., "AI Edition" with camera + ML) or community contributions.

---

## Key Insights

**Updated based on competitive analysis and gap features:**

1. **Equipment Library is Critical:** WRK09 scored 23/25 (now tied for #1). Competitive analysis revealed **zero commercial timers have this**, but DarkroomLog app proves strong demand (users log enlarger, lens, paper, chemistry per session). This is a **high-value, low-complexity differentiator** — add to Tier 2.

2. **Session Logging is Unmet Market Need:** WRK01 (score 21) + WRK04 CSV export (score 17) address gap **no commercial timer fills**. Forum users explicitly request "timer that remembers everything I print." Pair with equipment library for complete data-driven workflow.

3. **Safelight Auto-On/Off is Professional Expectation:** HW10 (score 21) found in RH Analyser, Beseler 8197, Filmomat, most pro timers. If we're building isolated power board with dual relay (Homer's task), **this outlet is table stakes**. Don't compete without it.

4. **Light Output Compensation Separates Tiers:** MET07 (score 21, D=5) is what separates $400+ professional timers (RH Vario, Metrolux II) from $200-300 basic timers. Closed-loop lamp monitoring compensates for voltage, aging, warm-up. **Defer to v2 (requires light sensor)**, but this is upgrade path to premium tier.

5. **F-Stop Timing Remains Non-Negotiable:** EXP01 still scores 23/25. Every advanced printer thinks in stops, not seconds. Zero budget timers have this; all premium timers do. **MVE must include.**

6. **Touchscreen Enables Unique UX Features:** UIX15 (Whole-Screen Touch Button) scored 22 — entire screen acts as start/stop for glove/wet-hand use. DB02 (Visual Zone Map) scored 22 with D=5 — draw dodge/burn zones on print thumbnail, assign times. **These leverage our platform advantage.**

7. **Test Strip + Split-Grade Cluster:** TST01 (23), SPL01 (23), TST05 (21) form tight cluster. This workflow (make split-grade test strip, analyze, set dual timer) is core advanced printing technique. **All three in Tier 2.**

8. **Lag Time Calibration is Pro Detail:** TIM13 (score 20) — lo1ol Red_Ray has this (millisecond lamp warm-up delay). Matters for precision timing at <2s exposures. Easy to implement (EEPROM setting), **add to Tier 2 or 3**.

9. **Hardware Simplicity Still Wins:** Top 25 average H=4.7 (minimal extra hardware). Gap features maintain this pattern — only metering features drop below H=3. **Keep MVE hardware simple: relay(s) + footswitch + display.**

10. **Visionary Features Validated but Deferred:** AI/ML features (scores 13-19, D=5, F=1-2) confirmed as differentiators but low feasibility. Emberlite's spectral analysis (ADV07) and CCAL (MET13) prove these are viable R&D paths — **defer to v3+ or community**.

11. **Flicker-Free Display Matters:** UIX18 (score 21) — competitive analysis noted premium timers avoid visible flicker at low brightness. High PWM frequency is firmware detail but **impacts perceived quality**. Homer should validate this in hardware bring-up.

12. **Dry-Down Compensation is Medium Priority:** EXP09 (score 20) — RH StopClock Pro has this (adjust exposure for wet-to-dry density shift). Useful but not essential. **Defer to v2 unless trivial to implement** (could be simple % offset in paper profile).

13. **Metering Upgrade Path — MET08 → MET15:** The TSL2591 sensor serves triple duty: MET07 (light compensation), MET08 (single-probe zone placement), and MET15 (multi-sensor array). **MET08 (score 21, Tier 2)** is proven technology (RH Analyser Pro approach) — once TSL2591 is added for MET07, zone placement metering is essentially free in firmware. **MET15 (score 18, Tier 3)** extends this into novel territory no commercial timer has explored: a flat board with 5-8 sensors via I2C multiplexer gives instant tonal histograms without manual probing. This progression (single probe → sensor array) provides a clear upgrade path from competitive parity to market leadership in metering.

---

## Hardware Design

This section documents the hardware architecture, component options, and their relationship to features. The darkroom timer is built on the **GUITION JC4880P433** platform (ESP32-P4 + ESP32-C6, 4.3" capacitive touch display) with external smart plug control and optional sensors to enable advanced features.

### Hardware Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  GUITION JC4880P433 (Main Controller)                       │
│  ┌──────────────────┐         ┌───────────────────┐         │
│  │  ESP32-P4        │  SDIO   │  ESP32-C6         │         │
│  │  (Display/Logic) │◄───────►│  (WiFi/Bluetooth) │─────────┼─► WiFi ─► Smart Plug ─► Enlarger
│  │  + LVGL UI       │         │                   │         │
│  └──────┬───────────┘         └───────────────────┘         │
│         │                                                    │
│         ├── 4.3" IPS LCD (480×800, capacitive touch)        │
│         ├── I2C Bus (sensors)                               │
│         ├── GPIO (footswitch, buzzer, buttons)              │
│         └── 1-Wire (temperature probes)                     │
└─────────────────────────────────────────────────────────────┘
                  │        │        │
                  ▼        ▼        ▼
            Footswitch  Temp    Light
            (hands-free) Sensor  Sensor
                        (CHM)   (MET)
```

**Key Architecture Decisions:**
1. **Smart plug for enlarger control** — OFF-the-shelf WiFi plug controlled via local API (NOT a custom relay/SSR). UL/CE certified, zero mains voltage in device.
2. **ESP32-P4 as brain** — Runs LVGL UI, timing logic, sensor processing. Delegates wireless to ESP32-C6 coprocessor.
3. **Modular sensor expansion** — I2C/GPIO sensors add features incrementally. MVE works with display + smart plug + footswitch only.

---

### Smart Plug Integration

**Architecture:** ESP32-C6 sends HTTP/MQTT commands to smart plug over local WiFi (no cloud dependency). Smart plug controls enlarger AC power.

#### Smart Plug Comparison

| Plug | API | Latency | Local Control | Power Rating | Price | Features Enabled | Recommendation |
|------|-----|---------|---------------|--------------|-------|------------------|----------------|
| **Shelly Plus Plug S** | Native REST + CoAP | ≤70ms | ✅ Yes | 10-15A (1800W) | $15-20 | TIM01-08, EXP01-08, SPL01-05, all core timing | ⭐⭐⭐⭐⭐ **Best for timing accuracy** |
| **Sonoff S31 (Tasmota)** | HTTP + MQTT | 20-80ms | ✅ Yes | 15A (1800W) | $15-20 | Same + power monitoring (lamp tracking) | ⭐⭐⭐⭐⭐ **Best for energy monitoring** |
| **Sonoff S26 (Tasmota)** | HTTP + MQTT | 100-300ms | ✅ Yes | 10A (1200W) | $7-12 | Basic timing only (slower latency) | ⭐⭐⭐ Acceptable for non-critical |
| **TP-Link Kasa (HS110/KP115)** | Unofficial local | 10-100ms | ⚠️ Unofficial | 15A (1800W) | $20-30 | Timing + energy data | ⭐⭐⭐ Risk: future firmware lockout |
| **Tuya/Smart Life** | LocalTuya | <100ms | ⚠️ Setup complex | 10-16A | $8-15 | Basic timing | ⭐⭐⭐ Setup friction, firmware variations |

**Recommendation:**
- **Primary:** Shelly Plus Plug S (lowest latency ≤70ms, official local API, no flashing required)
- **Secondary:** Sonoff S31 with Tasmota (20-80ms, adds power monitoring for lamp burn-time tracking)
- **Dual-outlet setup (HW10):** 2× Shelly Plus Plug S (enlarger + safelight independent control)

**Timing Accuracy & Latency:**
- Smart plug adds **20-80ms latency** (network + relay switching) vs. <5ms for direct relay control
- **Mitigation:** 
  - Pre-connect at startup (keep-alive MQTT/TCP connection)
  - Empirically measure round-trip time during setup (store calibration in NVS)
  - Compensate for network + relay latency in exposure calculations
  - Display warning if latency >100ms
- **Failsafe:** Watchdog timer detects WiFi drops, emergency shutoff if connection lost mid-exposure

**Features Enabled:**
- **All core timing features** (TIM01-08, EXP01-08)
- **F-stop mode** (EXP01) — latency compensated in calculations
- **Split-grade dual timer** (SPL01-05) — sequential or simultaneous exposures
- **Safelight auto-on/off** (HW10) — requires dual smart plug setup

---

### LCD Display Safety — Paper Fogging Mitigation

**THIS IS CRITICAL.** An unfiltered 4.3" IPS LCD WILL fog photographic paper, especially multigrade/VC papers. Multiple mitigation layers are REQUIRED for safe darkroom operation.

#### The Problem

**LCD Spectrum Hazard:**
- White LED backlight peaks: **460nm (blue)**, 530nm (green), 630nm (red)
- Even with "red" pixels displayed, backlight still emits full blue/green spectrum
- Color filters in LCD panel are imperfect — 5-10% blue/green leakage

**Photographic Paper Sensitivity:**

| Paper Type | Blue Sensitivity | Green Sensitivity | Safe Zone | Notes |
|------------|------------------|-------------------|-----------|-------|
| **Graded B&W** (Ilford MG IV Graded, Foma) | 425-500nm (HIGH) | Minimal | >580nm | Red filter required, amber marginal |
| **Multigrade/VC** (Ilford MG, Foma Variant) | 425-480nm (HIGH) | 520-560nm (MEDIUM) | >600nm | **Most sensitive** — green controls soft contrast |
| **Color RA-4** (Kodak Ektacolor, Fuji) | **Panchromatic** (400-700nm) | **Panchromatic** | **NONE** | Display MUST be off during all paper handling |
| **Alternative Process** (Cyanotype, Pt/Pd) | UV + blue (<500nm) | Minimal | >580nm | Less sensitive than modern silver papers |

**Critical Finding (Ilford Technical Data):**
> "Multigrade papers contain two emulsions. Blue light (450-480nm) controls hard contrast. Green light (520-560nm) controls soft contrast. Any light below 580nm can expose the paper."

#### Mitigation Options

| Option | Effectiveness | Cost | Complexity | Touch Compatible? | Notes |
|--------|---------------|------|------------|-------------------|-------|
| **Display off during exposure** | ⭐⭐⭐⭐⭐ | $0 | Low | ✅ Yes | **PRIMARY mitigation** — zero fogging risk when it matters |
| **Software dimming (PWM backlight)** | ⭐⭐⭐⭐ | $0 | Low | ✅ Yes | 10-20% brightness reduces total light output |
| **Physical red gel filter** (Rosco #27, Lee #106) | ⭐⭐⭐⭐⭐ | $10-15 | Low | ✅ Yes (5-10% sensitivity loss) | Blocks <600nm, transmits >620nm (80-90% red) |
| **Physical red acrylic filter** (3mm sheet) | ⭐⭐⭐⭐⭐ | $10-30 | Medium | ⚠️ Marginal (20-30% loss) | More durable, very dim (10-20% red transmission) |
| **Software red-only mode** | ⭐⭐ | $0 | Low | ✅ Yes | **INEFFECTIVE ALONE** — backlight still emits blue |
| **Distance/angle** | ⭐ | $0 | N/A | ✅ Yes | Requires impractical distances (3-5m), use as supplement only |
| **Motorized shutter** | ⭐⭐⭐⭐ | $10 | High | ✅ Yes | Adds bulk, moving parts, potential failure |
| **Backlight LED swap** (red LEDs) | ⭐⭐⭐⭐⭐ | $5-10 | **Very High** | ✅ Yes | Permanent mod, LCD damage risk, **NOT recommended** |

#### Recommended Layered Approach

**MINIMUM (Tier 1 — Graded B&W Papers):**
1. **Display-off mode during exposure** (default ON) — screen blanks when enlarger turns on, returns after exposure
2. **Software brightness limit** — user adjustable 5-15%, default 10%, max 20%
3. **Distance warning** — user manual + startup screen: "Keep timer at least 2 feet from paper"

**RECOMMENDED (Tier 2 — Multigrade/VC Papers):**
All of above PLUS:
4. **Physical red filter** — Rosco #27 (Medium Red) or Lee #106 (Primary Red) gel, included with device
5. **Red-optimized UI** — all elements red (#FF0000) on black, no blue/green (designed for filtered display)
6. **Audio feedback** — beeps for start/stop when display off (user still informed of timer state)
7. **Fogging test mode** — built-in self-test (see below)

**SAFEST (Color RA-4 Papers):**
8. **Display MUST be off during ALL paper handling** — no exceptions, even with red filter
9. **Timer in separate room or blackout cover** — screen only on for setup, OFF when darkroom door closes

#### Distance Guidelines (Inverse Square Law)

| Paper Type | Minimum Distance | Recommended Distance | Risk Level |
|------------|------------------|----------------------|------------|
| Graded B&W (with red filter + low brightness) | 18 inches | 24-36 inches | Low |
| VC/Multigrade (with red filter + display-off) | 24 inches | 36-48 inches | Low |
| Color RA-4 | **N/A** | **N/A** | **Display OFF required** |
| Alternative process (Cyanotype, Pt/Pd) | 12 inches | 18-24 inches | Very Low (test first) |

**Why 2-3 feet?**
- Inverse square law: 12" → 24" = 75% light reduction
- Community consensus: 3 feet is "safe" threshold for phone apps with red filter
- Practical: User can still see/touch screen comfortably at 2-3 feet

#### Fogging Test Mode (Built-In Self-Test)

**Feature:** Settings > Safelight Test

**Procedure (automated in firmware):**
1. User selects "Safelight Test" from settings menu
2. Screen displays instructions:
   ```
   SAFELIGHT TEST MODE
   
   This test checks if your timer's screen can fog paper.
   
   YOU WILL NEED:
   - 1 sheet of photo paper
   - Developer
   
   STEPS:
   1. In darkness, give paper minimal exposure
   2. Place paper 12" from this screen
   3. Press START
   4. Screen will display all-white for 5 min
   5. Develop paper normally
   
   RESULT:
   - No darkening? SAFE
   - Visible gray? UNSAFE - increase distance
   
   [START TEST]  [CANCEL]
   ```
3. User presses START → screen displays all-white (or all-red if filtered) at current brightness
4. 5-minute countdown (audio beeps each minute)
5. After 5 min: "Test complete. Develop paper and check for fogging."

**Benefits:**
- Shifts responsibility appropriately (user tests THEIR setup)
- Builds confidence (empirical evidence, not manufacturer claims)
- Accounts for variability (paper type, distance, ambient safelight)
- Annual recalibration (when changing paper brands or setup)

**Features Enabled:** SAF04 (Paper-Safe Display), UIX07 (Adjustable Brightness)

---

### Sensor Catalog — What They Enable

Sensors unlock advanced features. MVE requires NONE (smart plug + footswitch only). Each sensor tier adds capabilities.

#### Temperature Sensors → Chemistry Monitoring Features

| Component | Interface | Price | Features Enabled | MVE? | Notes |
|-----------|-----------|-------|------------------|------|-------|
| **DS18B20 (waterproof probe)** | 1-Wire | $3-5 | CHM03, CHM04 | ⚠️ Tier 2 | Developer temp monitoring, temp-compensated dev time |
| **DHT22** (air temp/humidity) | Digital | $3-5 | CHM05, ENV01 | ❌ v2 | Ambient air only, not waterproof |
| **BME280** (temp/humidity/pressure) | I2C | $5-8 | CHM05, ENV01, WRK08 | ❌ v2 | Full environment monitoring, film storage alerts |
| **TMP36** (analog) | ADC | $1-2 | CHM05 | ❌ v2 | Budget air temp, lower accuracy |

**Feature Cross-Reference:**
- **CHM03 (Developer Timer with Temp Monitoring):** DS18B20 probe in developer tray, live temp display
- **CHM04 (Temp-Compensated Dev Time):** Auto-adjust dev time for temp variations (e.g., +10% time at 18°C vs. 20°C)
- **CHM05 (Ambient Temp/Humidity Tracking):** BME280/DHT22 for darkroom conditions
- **ENV01 (Environmental Monitoring Dashboard):** BME280 for comprehensive logging

**Recommended:** DS18B20 waterproof probe (Tier 2 priority) — low cost, high value for serious printers.

#### Light Sensors → Metering Features

| Component | Interface | Price | Features Enabled | MVE? | Notes |
|-----------|-----------|-------|------------------|------|-------|
| **TSL2591** (high dynamic range) | I2C | $6-8 | MET07, MET01, MET08, MET15 | ❌ v2 | 600M:1 range (0.0002-88k lux), under-enlarger metering |
| **TCA9548A** (I2C multiplexer) | I2C | $2-3 | MET15 | ❌ v3 | Enables 5-8× TSL2591 sensors on one I2C bus (multi-sensor easel array) |
| **VEML7700** (ultra-low light) | I2C | $4-6 | SAF05, MET01 | ❌ v2 | 0.004 lux minimum, safelight safety monitoring |
| **BH1750** (basic) | I2C | $2-4 | MET01 | ❌ v3 | 1-65k lux (no sub-lux), NOT suitable for darkroom |

**Feature Cross-Reference:**
- **MET07 (Light Output Compensation/Integrator):** TSL2591 monitors lamp brightness, auto-compensates for voltage/aging/warm-up (RH Vario feature, D=5)
- **MET01 (Ambient Light Monitoring):** TSL2591/VEML7700 detects white light leaks during exposure
- **MET08 (Grey Scale Tone Indicator — Single Probe):** Single TSL2591 on cable probe; user positions at highlight/shadow/midtone areas; computes SBR, recommends exposure time + contrast grade; zone placement preview (Tier 2)
- **MET15 (Multi-Sensor Easel Array):** 5-8× TSL2591 via TCA9548A multiplexer on flat board; instant tonal histogram, automatic zone detection, dodge/burn candidate identification; no manual probing (Tier 3, depends on MET08)
- **SAF05 (Safelight Brightness Alert):** VEML7700 monitors for excessive safelight levels (>2 lux warning)

**Recommended:** TSL2591 (v2 priority) — separates pro timers from budget timers. Single probe (MET08) shares hardware with MET07, making zone placement metering essentially free in firmware once sensor is added. Multi-sensor array (MET15) is a v3 upgrade path with novel market positioning. VEML7700 optional for safelight safety paranoia.

#### Environmental Sensors → Workflow Features

| Component | Interface | Price | Features Enabled | MVE? | Notes |
|-----------|-----------|-------|------------------|------|-------|
| **BME280** (combo) | I2C | $5-8 | CHM05, ENV01, WRK09 | ❌ v2 | Temp + humidity + pressure, best value |
| **SHT31** (precision) | I2C | $5-7 | CHM05, ENV01 | ❌ v3 | Higher accuracy than BME280 (±2% RH vs. ±3%) |
| **DHT22** | Digital | $3-5 | CHM05, ENV01 | ❌ v3 | Budget option, ±2-5% RH accuracy |

**Feature Cross-Reference:**
- **ENV01 (Environmental Monitoring Dashboard):** Track darkroom temp, humidity, pressure over time
- **WRK09 (Equipment Library):** Log session conditions with print recipes
- **CHM05:** (Covered above under Temperature Sensors)

**Recommended:** BME280 (v2) — three sensors in one, saves I2C addresses and board space.

#### Input Devices → Hands-Free Operation

| Component | Interface | Price | Features Enabled | MVE? | Notes |
|-----------|-----------|-------|------------------|------|-------|
| **Footswitch** (1/4" TRS sustain pedal) | GPIO | $10-20 | UIX09, TIM03, TIM05 | ✅ **Tier 1** | **Non-negotiable** for wet/gloved hands |
| **Rotary encoder** (KY-040) | GPIO (3 pins) + PCNT | $2-3 | UIX10, TIM02 | ❌ v2 | Tactile backup for touchscreen, glove-friendly |
| **External buttons** (12mm tactile) | GPIO | $1-2 each | UIX09, UIX11 | ❌ v2 | Panel-mounted start/stop/focus |
| **Piezo buzzer** (passive) | PWM/LEDC | $1-2 | UIX12, TIM08, UIX14 | ✅ **Tier 1** | **Essential** for audio feedback when display off |

**Feature Cross-Reference:**
- **UIX09 (One-Handed Operation):** Footswitch for hands-free start/stop (wet hands, holding paper)
- **TIM03 (Foot Switch Integration):** Press to expose, release to stop (focus mode: hold to turn on enlarger)
- **TIM05 (Pause/Resume):** Footswitch tap to pause mid-exposure
- **UIX10 (Glove-Friendly Touch Targets):** Rotary encoder as alternative to touchscreen (large knob, tactile clicks)
- **UIX12 (Visual & Audio Alerts):** Buzzer for beeps (start/stop, countdown milestones)
- **TIM08 (Countdown with Alerts):** Beep every 10 sec, final 5 sec countdown (audio-only when display off)
- **UIX14 (Melody Alerts):** Different tones for different events (error beeps, completion jingles)

**Recommended:**
- **Footswitch:** Tier 1 (MVE critical) — professional workflow expectation
- **Buzzer:** Tier 1 (MVE critical) — audio feedback when display off during exposure
- **Rotary encoder:** Tier 2 — backup navigation, glove-friendly, low cost
- **External buttons:** Tier 3 — nice-to-have, panel-mount aesthetics

---

### GPIO & Expansion

**ESP32-P4 Platform Resources (GUITION JC4880P433):**
- **Total GPIOs:** 55 (GPIO0-GPIO54), all 3.3V-only ⚠️ **NOT 5V tolerant**
- **After display/touch/C6 coprocessor:** ~20-30 GPIOs available for sensors/I/O
- **Strapping pins:** GPIO34-38 (boot mode selection, avoid strong pull-up/down at boot)
- **USB-JTAG:** GPIO24-25 (default debugging, freeing disables USB programming)

#### I2C Bus

**Hardware:**
- **2× High-Performance I2C controllers** + 1× Low-Power I2C (3 buses total)
- **Flexible pin mapping** via IO_MUX (any GPIO can be I2C SDA/SCL)
- **Typical mapping:** SDA=GPIO21, SCL=GPIO22 (verify GUITION schematic)
- **Pull-up resistors:** 2.2kΩ - 3.3kΩ for 400kHz Fast Mode (only ONE set per bus)

**Devices on I2C (typical darkroom timer):**
1. Touch controller (pre-allocated by GUITION board)
2. BME280 (temp/humidity/pressure) — address 0x76 or 0x77
3. TSL2591 (light sensor / single probe for MET07, MET08) — address 0x29
4. VEML7700 (safelight monitor) — address 0x10
5. Optional: TCA9548A (I2C multiplexer for MET15 multi-sensor array) — address 0x70-0x77
6. Optional: RTC (DS3231), I/O expander

**Total:** 3-6 devices = well within limits (127 max per bus). TCA9548A enables 5-8× additional TSL2591 sensors for MET15.

**Level Shifting (if mixing 5V devices):**
- ESP32-P4 is **3.3V ONLY** — NOT 5V tolerant
- Use bidirectional level shifter (BSS138 MOSFET, TXS0108E, PCA9306)

#### Power Rails

**Available on expansion header:**
- **3.3V rail:** 500mA+ typical (verify GUITION schematic)
- **5V rail:** USB-C input, shared with display/board (2A total budget)

**Sensor power budget (example):**
- DS18B20: <1.5mA (normal mode)
- BME280: <3.7mA (1 sample/sec)
- TSL2591: <0.5mA (typical)
- VEML7700: <0.2mA (typical)
- Piezo buzzer: <30mA (tone on)
- **Total sensors:** <40mA @ 3.3V (negligible vs. 500mA available)

#### GPIO Allocation Example (MVE + v2 Sensors)

| Peripheral | GPIO Count | Pins | Notes |
|------------|------------|------|-------|
| I2C bus (SDA, SCL) | 2 | GPIO21-22 | Shared bus for all I2C sensors |
| Footswitch (1/4" jack) | 1 | GPIO19 | INPUT_PULLUP mode |
| Piezo buzzer | 1 | GPIO18 | PWM/LEDC channel |
| DS18B20 (1-Wire) | 1 | GPIO17 | 4.7kΩ pull-up required |
| Rotary encoder (CLK, DT, SW) | 3 | GPIO15-16, GPIO14 | PCNT peripheral for CLK/DT |
| External buttons (Start, Stop, Focus) | 3 | GPIO11-13 | INPUT_PULLUP mode |
| Backlight PWM (if not via display driver) | 1 | GPIO21 | LEDC peripheral (verify schematic) |
| **Total used** | **12-13** | | |
| **Remaining margin** | **10-20+** | | Plenty for future expansion |

**Recommendation:** GPIO availability is NOT a constraint — MVE uses <15 pins, leaving 15+ for future features.

#### USB-C Capabilities

**Dual USB-C ports (GUITION JC4880P433):**
1. **Debug/JTAG:** USB-C for firmware flashing, serial console, debugging (GPIO24-25)
2. **Power:** 5V input (2A recommended for display + peripherals), board regulates to 3.3V

**Use cases:**
- Firmware updates via PlatformIO/ESP-IDF
- Serial console debugging during development
- USB-C power supply (production) or battery backup (future)

#### SD Card (if available)

**Check GUITION schematic** — some dev boards include microSD slot (SPI-based), others require external module.

**If available:**
- **WRK04 (Session Export CSV):** Export exposure logs to SD card
- **WRK03 (Print Recipe Storage):** Backup/restore recipes via SD card
- **Firmware updates:** Load new firmware from SD card (bootloader feature)

**If NOT available:**
- Add external SPI microSD module (~$3-5) for v2
- Alternative: WiFi file transfer to PC/phone

---

### Hardware BOM Estimates

Bill of materials for two tiers: MVE (minimum viable experience) and Full Feature Set.

#### MVE Hardware (Tier 1 — Core Timer)

| Component | Quantity | Unit Price | Total | Notes |
|-----------|----------|------------|-------|-------|
| **GUITION JC4880P433** (ESP32-P4 board) | 1 | $40-60 | $50 | 4.3" touch display, dual-core |
| **Smart plug** (Shelly Plus Plug S or Sonoff S31) | 1 | $15-20 | $18 | Enlarger control, local WiFi API |
| **Footswitch** (1/4" sustain pedal, e.g., M-Audio SP-2) | 1 | $10-20 | $12 | Hands-free start/stop |
| **Piezo buzzer** (passive) | 1 | $1-2 | $1.50 | Audio alerts when display off |
| **Red gel filter** (Lee #106 or Rosco #27, cut to size) | 1 | $10-15 | $12 | LCD fogging mitigation, included with device |
| Resistors, wire, connectors, enclosure | — | — | $10 | Misc hardware |
| **MVE Total** | | | **~$103** | **Target: <$150** ✅ |

**Features Enabled (MVE):**
- All core timing (TIM01-08, EXP01-08, SPL01-05)
- F-stop mode, test strips, split-grade, presets, recipes
- Hands-free operation (footswitch)
- Paper-safe display (red filter + display-off mode)

#### Full Hardware (Tier 1 + Tier 2 Sensors + Expansion)

| Component | Quantity | Unit Price | Total | Notes |
|-----------|----------|------------|-------|-------|
| **All MVE components** (above) | — | — | $103 | Base system |
| **DS18B20 waterproof probe** | 2 | $3-5 | $8 | Developer + fixer temp monitoring (CHM03-04) |
| **TSL2591 light sensor** (Adafruit breakout) | 1 | $6-8 | $7 | Under-enlarger metering, light compensation, zone placement (MET07, MET08) |
| **BME280** (temp/humidity/pressure) | 1 | $5-8 | $6 | Ambient monitoring, film storage alerts (ENV01, CHM05) |
| **Rotary encoder** (KY-040) | 1 | $2-3 | $2.50 | Tactile input backup, glove-friendly (UIX10) |
| **Smart plug #2** (for safelight auto-on/off, HW10) | 1 | $15-20 | $18 | Dual-outlet setup |
| Enclosure upgrade (panel-mount jacks, cutouts) | — | — | $15 | Professional finish |
| **Full Hardware Total** | | | **~$159** | **Target: <$200 BOM** ✅ |

**Features Enabled (Full):**
- **MVE features** (above)
- **Chemistry monitoring** (CHM03-05): Temp-compensated dev times, probe in tray
- **Metering** (MET07, MET08): Light output compensation (pro feature, D=5), single-probe zone placement + grade/time recommendation
- **Environment** (ENV01, WRK08): Darkroom conditions logging, humidity alerts
- **Safelight auto-on/off** (HW10): Professional timer expectation, dual-relay setup
- **Tactile input** (UIX10): Rotary encoder backup for wet gloves

#### Competitive Benchmark

| Product | Price | Includes | Our Advantage |
|---------|-------|----------|---------------|
| **Darkroom Automation F-Stop Timer** | $305 | Timer + relay, no display | We add touchscreen, sensors, WiFi for 50% less |
| **RH Designs StopClock Pro** | £389 (~$445) | F-stop timer, LED display | We match features + open-source + modern UX |
| **Beseler 8197** | $120-150 | Digital timer, LED display | We add f-stop mode, touchscreen, smart features |
| **GraLab 300** (analog) | $80-120 | Mechanical timer, glow dial | We're digital with f-stop, recipes, logging |
| **Our MVE Hardware** | **~$100 BOM** → **$300-400 retail** | Touchscreen + f-stop + smart plug + sensors + open-source | **Unique market position** |

**Pricing Strategy:**
- **BOM target:** <$150 (MVE achievable at $103, Full at $159)
- **Retail target:** $300-400 (validated by Darkroom Automation at $305)
- **Margin:** 2-3× BOM (industry standard for electronics)
- **Value proposition:** First touchscreen darkroom timer with f-stop mode, session logging, and open-source firmware

---

**End of Feature Analysis**
