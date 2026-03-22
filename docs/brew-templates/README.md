# Brew Templates

Ready-to-use brew templates for the ESP32 Macropad. Upload any of these to your device via the web portal's Brews page.

For the full field reference and DSL syntax, see the [Brew Template Guide](../brew-template-guide.md).

---

## V60 Multi-Pour

**File:** [v60_multi_pour.json](v60_multi_pour.json)

A classic Hario V60 pour-over with a bloom phase and two structured pours. Designed for a single cup using a 1:15.6 ratio.

| Parameter | Value |
|-----------|-------|
| Coffee | 16g, medium-fine grind |
| Water | 250g total |
| Ratio | 1:15.6 |
| Brew time | ~2:30 guided |

### How it works

1. **Setup** — Place your V60 dripper and server on the scale. The scale tares automatically.

2. **Dose** — Grind and add 16g of coffee. The display shows your target weight. When you tap *Start*, the dose weight is captured to the brew log.

3. **Start Pour** — Begin pouring hot water (~93°C). As soon as the scale detects more than 3g, it tares (zeroing out the dry coffee), starts the brew timer, and automatically advances to the Bloom stage. A double-beep confirms the brew has started.

4. **Bloom (45s)** — Pour gently in concentric circles until you reach 50g. Weight cues chirp at 10g and 5g remaining to help you slow down. A long tone confirms you've hit the weight target. A countdown beep plays in the last ~1.5 seconds before the stage auto-advances, with a confirmation tone at expiry. The bloom water weight is captured to the log.

5. **Pour 1 (45s)** — Resume pouring in slow, steady circles up to 150g. Weight cues chirp at 10g and 5g remaining, with a confirmation tone at target. A countdown beep signals the end of the stage, with a confirmation tone at expiry.

6. **Final Pour** — Pour the remaining water to reach 250g total. Weight cues fire at 10g and 5g remaining, with a confirmation tone when you hit 250g. Tap *Done* when finished. A triple-beep confirms the brew is complete, and the total water weight is captured.

### Audio cues summary

| Cue | When | Sound |
|-----|------|-------|
| Brew started | Bloom begins | Long tone (1200 Hz, 500ms) |
| Weight approaching | Near each stage target | Short chirps (1500 Hz, 200ms) at 10g and 5g remaining |
| Weight target hit | Weight reaches stage target | Long tone (1500 Hz, 1000ms) |
| Countdown warning | Last ~1.5s of timed stages | Two short beeps (800 Hz, 200ms) |
| Countdown done | Timer reaches zero | Long tone (800 Hz, 1000ms) |
| Brew complete | Tap Done on final pour | Triple beep (1200 Hz) |

### What gets logged

The brew log captures:
- **Dose weight** — actual coffee weight when you start
- **Bloom water** — water added during the bloom phase
- **Total water** — final water weight
- **Weight + flow rate** at 1 Hz throughout the brew
- **Stage markers** at each transition for the brew chart
