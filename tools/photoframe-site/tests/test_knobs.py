"""Parity + sanity tests for the image-processing knobs and live preview.

Run standalone (no pytest needed):

    python3 tests/test_knobs.py

The critical guard is ``test_tone_curve_parity``: it reimplements the browser's
tone-curve twin (see the ``buildLut`` function in ``templates/upload.html``) and
checks it against the authoritative ``gray16._build_tone_lut``. If the Python
pipeline's curve ever changes, this test fails, signalling that the mirrored JS
must be updated too.
"""

import math
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import gray16  # noqa: E402
import knobs  # noqa: E402


def _js_twin_lut(
    gamma: float,
    highlights: float,
    brightness: float = 0.0,
    contrast: float = 1.0,
) -> list[int]:
    """Faithful Python mirror of the browser buildLut() in upload.html.

    Uses JS Math.round semantics (round-half-up) so any difference from the
    authoritative LUT is rounding-only (<=1), not a formula divergence.
    """
    exp = 6.0
    peak = (math.pow(exp / (exp + 1.0), exp)) / (exp + 1.0)
    inv_gamma = 1.0 / gamma if gamma > 0 else 1.0
    lut = []
    for v in range(256):
        norm = v / 255.0
        toned = math.pow(norm, inv_gamma)
        if highlights != 0.0:
            bump = (math.pow(toned, exp) * (1.0 - toned)) / peak
            toned += highlights * bump
        if contrast != 1.0:
            toned = (toned - 0.5) * contrast + 0.5
        if brightness != 0.0:
            toned += brightness
        if toned < 0.0:
            toned = 0.0
        elif toned > 1.0:
            toned = 1.0
        lut.append(_js_round(toned * 255.0))
    return lut


def _clamp_u8(value: int) -> int:
    return 0 if value < 0 else 255 if value > 255 else value


def _js_panel_inverse_lut(resp: tuple[float, ...]) -> list[int]:
    """Mirror of buildPanelInverseLut() in upload.html."""
    last = len(resp) - 1
    lut = []
    for d in range(256):
        target = d / 255.0
        if target <= resp[0]:
            lut.append(0)
            continue
        if target >= resp[last]:
            lut.append(255)
            continue
        for k in range(last):
            if resp[k] <= target <= resp[k + 1]:
                denom = resp[k + 1] - resp[k]
                frac = 0.0 if denom <= 0.0 else (target - resp[k]) / denom
                lut.append(_clamp_u8(_js_round((k + frac) * 17.0)))
                break
    return lut


def _js_panel_forward_lut(resp: tuple[float, ...]) -> list[int]:
    """Mirror of buildPanelForwardLut() in upload.html."""
    last = len(resp) - 1
    lut = []
    for s in range(256):
        pos = s / 17.0
        k = int(math.floor(pos))
        if k >= last:
            reflectance = resp[last]
        else:
            frac = pos - k
            reflectance = resp[k] + frac * (resp[k + 1] - resp[k])
        lut.append(_clamp_u8(_js_round(reflectance * 255.0)))
    return lut


def _js_round(x: float) -> int:
    return int(math.floor(x + 0.5))


def test_tone_curve_parity() -> None:
    """JS twin matches gray16 LUT (black=0, white=255) within rounding tolerance."""
    for gamma in (0.4, 1.0, 2.0):
        for highlights in (-0.5, 0.0, 0.5):
            for brightness in (-0.2, 0.0, 0.2):
                for contrast in (0.75, 1.0, 1.25):
                    authoritative = gray16._build_tone_lut(
                        0, 255, gamma, highlights, brightness, contrast
                    )
                    twin = _js_twin_lut(gamma, highlights, brightness, contrast)
                    max_diff = max(abs(a - b) for a, b in zip(authoritative, twin))
                    assert max_diff <= 1, (
                        f"tone curve drift at gamma={gamma}, highlights={highlights}, "
                        f"brightness={brightness}, contrast={contrast}: "
                        f"max |delta|={max_diff} (JS twin must mirror gray16._build_tone_lut)"
                    )


def test_panel_inverse_lut_js_parity() -> None:
    """buildPanelInverseLut() in upload.html mirrors gray16._build_panel_inverse_lut."""
    resp = gray16.PANEL_RESPONSE_E1003_GC16_V32
    authoritative = gray16._build_panel_inverse_lut(resp)
    twin = _js_panel_inverse_lut(resp)
    max_diff = max(abs(a - b) for a, b in zip(authoritative, twin))
    assert max_diff <= 1, f"panel inverse LUT drift: max |delta|={max_diff}"


def test_panel_forward_lut_js_parity() -> None:
    """buildPanelForwardLut() in upload.html mirrors gray16._build_panel_forward_lut."""
    resp = gray16.PANEL_RESPONSE_E1003_GC16_V32
    authoritative = gray16._build_panel_forward_lut(resp)
    twin = _js_panel_forward_lut(resp)
    max_diff = max(abs(a - b) for a, b in zip(authoritative, twin))
    assert max_diff <= 1, f"panel forward LUT drift: max |delta|={max_diff}"


def test_js_constants_match_gray16() -> None:
    """The highlight-bump constant baked into the JS twin matches gray16."""
    assert gray16.HIGHLIGHT_BUMP_EXP == 6.0, (
        "upload.html hardcodes HIGHLIGHT_BUMP_EXP=6.0; update both if this changes"
    )


def test_display_sim_lut_matches_forward() -> None:
    """gray16.simulate_display renders the measured panel forward response.

    Both the live JS preview and the baked gallery thumbnail simulate the panel
    with the same measured forward LUT, so simulate_display must equal the
    authoritative forward LUT and the JS twin (within rounding).
    """
    from PIL import Image

    resp = gray16.PANEL_RESPONSE_E1003_GC16_V32
    forward = gray16._build_panel_forward_lut(resp)
    twin = _js_panel_forward_lut(resp)
    ramp = Image.frombytes("L", (256, 1), bytes(range(256)))
    out = list(gray16.simulate_display(ramp).getdata())
    for v in range(256):
        assert out[v] == forward[v], (v, out[v], forward[v])
        assert abs(out[v] - twin[v]) <= 1, (v, out[v], twin[v])


def test_panel_calibration_identity_when_off() -> None:
    """strength 0 is a no-op (calibration toggle off keeps the desired gray)."""
    from array import array

    gray = array("h", list(range(256)))
    gray16.apply_panel_calibration(gray, 0.0)
    assert list(gray) == list(range(256))


def test_panel_calibration_darkens_midtones() -> None:
    """The panel renders midtones light, so calibration feeds a darker source."""
    from array import array

    gray = array("h", [128])
    gray16.apply_panel_calibration(gray, 1.0)
    assert gray[0] < 128


def test_panel_calibration_preserves_endpoints_and_monotonicity() -> None:
    """Black->black, white->white, and the mapping never decreases."""
    from array import array

    gray = array("h", list(range(256)))
    gray16.apply_panel_calibration(gray, 1.0)
    assert gray[0] == 0
    assert gray[255] == 255
    for i in range(1, 256):
        assert gray[i] >= gray[i - 1]


def test_knob_defaults_track_pipeline() -> None:
    """Registry defaults come from gray16 so they cannot silently drift."""
    defaults = knobs.defaults()
    assert defaults["gamma"] == gray16.CAL_GAMMA
    assert defaults["highlights"] == gray16.CAL_HIGHLIGHTS
    assert defaults["brightness"] == gray16.CAL_BRIGHTNESS
    assert defaults["contrast"] == gray16.CAL_CONTRAST
    assert defaults["panel_calibration"] == gray16.CAL_PANEL_STRENGTH


def test_panel_calibration_default_on() -> None:
    """Calibration ships on by default (the measured correction is the baseline)."""
    assert gray16.CAL_PANEL_STRENGTH == 1.0
    assert knobs.defaults()["panel_calibration"] == 1.0


def test_to_client_exposes_knob_type() -> None:
    """The template needs each knob's control type to render switch vs slider."""
    by_id = {k["id"]: k for k in knobs.to_client()}
    assert by_id["panel_calibration"]["type"] == "toggle"
    assert by_id["gamma"]["type"] == "range"


def test_parse_values_toggle() -> None:
    """The calibration toggle parses to 0.0/1.0 and falls back to its default."""
    assert knobs.parse_values({"panel_calibration": "1"})["panel_calibration"] == 1.0
    assert knobs.parse_values({"panel_calibration": "0"})["panel_calibration"] == 0.0
    assert knobs.parse_values({})["panel_calibration"] == gray16.CAL_PANEL_STRENGTH


def test_parse_values_clamps_and_falls_back() -> None:
    parsed = knobs.parse_values({"gamma": "99", "highlights": "", "bogus": "1"})
    gamma_knob = knobs.get("gamma")
    assert parsed["gamma"] == gamma_knob.max  # clamped to max
    assert parsed["highlights"] == gray16.CAL_HIGHLIGHTS  # blank -> default
    assert "bogus" not in parsed  # unknown ignored

    parsed2 = knobs.parse_values({"gamma": "not-a-number"})
    assert parsed2["gamma"] == gray16.CAL_GAMMA  # invalid -> default


def test_full_base_preserves_image_aspect() -> None:
    from PIL import Image

    # full_base keeps the IMAGE's own aspect (not the device aspect) so the
    # browser can pan/zoom the whole image behind the framing window.
    src = Image.new("RGB", (1000, 600), (120, 130, 140))
    base = gray16.full_base(src, max_edge=468)
    assert base.mode == "L"
    assert base.size == (468, 281)  # 5:3, longest edge 468

    portrait = Image.new("RGB", (100, 200), (120, 130, 140))
    tall = gray16.full_base(portrait, max_edge=468)
    assert tall.size == (234, 468)  # 1:2, longest edge 468


def _run() -> int:
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as exc:
                failures += 1
                print(f"FAIL {name}: {exc}")
    print(f"\n{'OK' if failures == 0 else 'FAILED'} ({failures} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(_run())
