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


def _js_twin_lut(gamma: float, highlights: float) -> list[int]:
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
            if toned < 0.0:
                toned = 0.0
            elif toned > 1.0:
                toned = 1.0
        lut.append(_js_round(toned * 255.0))
    return lut


def _js_round(x: float) -> int:
    return int(math.floor(x + 0.5))


def test_tone_curve_parity() -> None:
    """JS twin matches gray16 LUT (black=0, white=255) within rounding tolerance."""
    for gamma in (0.4, 0.8, 1.0, 1.5, 2.0):
        for highlights in (-0.5, -0.1, 0.0, 0.25, 0.5):
            authoritative = gray16._build_tone_lut(0, 255, gamma, highlights)
            twin = _js_twin_lut(gamma, highlights)
            max_diff = max(abs(a - b) for a, b in zip(authoritative, twin))
            assert max_diff <= 1, (
                f"tone curve drift at gamma={gamma}, highlights={highlights}: "
                f"max |delta|={max_diff} (JS twin must mirror gray16._build_tone_lut)"
            )


def test_js_constants_match_gray16() -> None:
    """The highlight-bump constant baked into the JS twin matches gray16."""
    assert gray16.HIGHLIGHT_BUMP_EXP == 6.0, (
        "upload.html hardcodes HIGHLIGHT_BUMP_EXP=6.0; update both if this changes"
    )


def test_display_sim_lut_matches_gamma() -> None:
    """gray16.simulate_display mirrors the JS DISPLAY_LUT (same gamma + rounding).

    Both the live JS preview and the baked gallery thumbnail must lighten the
    image identically, so the panel-simulation LUT must equal pow(v/255, gamma).
    """
    from PIL import Image

    gamma = gray16.PREVIEW_DISPLAY_GAMMA
    ramp = Image.frombytes("L", (256, 1), bytes(range(256)))
    out = list(gray16.simulate_display(ramp).getdata())
    for v in range(256):
        expected = _js_round(math.pow(v / 255.0, gamma) * 255.0)
        assert out[v] == expected, (v, out[v], expected)


def test_knob_defaults_track_pipeline() -> None:
    """Registry defaults come from gray16 so they cannot silently drift."""
    defaults = knobs.defaults()
    assert defaults["gamma"] == gray16.CAL_GAMMA
    assert defaults["highlights"] == gray16.CAL_HIGHLIGHTS


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
