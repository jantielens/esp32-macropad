# PRD 04 — Bare-Bulb Strip Pad + Navigation

**Feature:** Pre-configured Strip Pad for Paper Calibration + Home Navigation  
**Depends on:** PRD 01–03  
**Estimated scope:** ~50 lines (pad config JSON only, no new firmware)

---

## Goal

Complete the end-to-end metering experience by providing:
1. A **pre-configured Strip pad** optimized for bare-bulb paper calibration (5.0s center)
2. **Navigation buttons** on the Home pad linking all metering pads together

After this PRD, the full Phase 1a → 1b → 2 → 3 flow is accessible from the Home screen
with clear navigation between steps.

---

## Deliverables

### 1. Bare-Bulb Strip Pad

**Modified file:** `sample/deviceexport.json` — add a "Bare-Bulb Strip" pad.

This is a copy of the existing Strip pad with **different defaults**:

| Setting | Normal Strip | Bare-Bulb Strip | Why |
|---------|-------------|----------------|-----|
| Base time | 8.0s | 5.0s | Zone V under bare bulb typically 3–8s |
| Segments | 7 | 7 | Same — good default |
| Step | ⅓ stop | ⅓ stop | Same — good default |
| Range | 4.0–16.0s | 2.5–10.1s | Shifted to bracket bare-bulb Zone V |

**Implementation:** Same buttons as the existing Strip pad, but the pad config sets the
initial base time via a boot action or the strip defaults are overridden when navigating
to this specific pad. Simplest approach: duplicate the Strip pad JSON with a
`strip:set_base:5.0` action attached to a "Reset to 5.0s" button.

**Alternative (simpler):** Don't create a separate pad. Instead, add a "Set 5.0s" quick
button on the existing Strip pad or rely on the Cal pad's on-screen instructions ("set
base time to 5.0s"). This avoids pad duplication.

**Recommendation:** Start with the simpler alternative (on-screen instruction only).
Add a dedicated pad only if user testing shows the manual step is confusing.

### 2. Home Pad Navigation

**Modified file:** `sample/deviceexport.json` — update the Home pad.

Add navigation buttons linking the metering workflow:

```
┌─────────────────────────────────────────────────────────┐
│  HOME PAD                                                │
│                                                          │
│  [Expose Timer]  [Test Strip]  [Paper Cal]  [Meter]     │
│                                                          │
│  ... (existing buttons) ...                              │
└─────────────────────────────────────────────────────────┘
```

Each button is a simple screen navigation action:

```jsonc
{ "label_center": "Paper Cal",
  "actions": [{ "type": "screen", "screen_id": "pad:Paper Cal" }] },
{ "label_center": "Meter",
  "actions": [{ "type": "screen", "screen_id": "pad:Meter" }] }
```

### 3. End-to-End Flow Validation

With all PRDs complete, the full workflow should be testable:

```
Home → [Paper Cal]
  → Tap "Read Lref" → see lux value
  → Navigate back to Home

Home → [Test Strip]  (or [Bare-Bulb Strip] if dedicated pad exists)
  → Set base time to 5.0s
  → Run test strip → develop → identify Zone V segment time

Home → [Meter]
  → Lref auto-populated ✅
  → Set Zone V time (e.g., 6.3s)
  → Insert negative → Focus ON → BRIGHT → DARK
  → See: SBR, Grade, Time

Home → [Expose Timer]
  → Set time to recommended value
  → Print
```

**Test criteria:**
- All four pads reachable from Home
- Full flow completes without errors
- Lref carries from Cal → Meter via shared memory
- Grade recommendation shown even without calibration (SBR-only mode)
- Exposure time shown when both Lref and Zone V are set

---

## Out of Scope

- Auto-populate Expose pad with recommended time (v2)
- Auto-navigate between pads (v2 — needs `ACTION_TYPE_NAVIGATE_PAD`)
- NVS persistence (v2)
