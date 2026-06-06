"""Declarative registry of image-processing knobs exposed in the upload UI.

Single source of truth for the tunable parameters of the calibrated pipeline
(see ``gray16.py``). The server uses it to validate and clamp upload parameters;
the upload template serializes it to JSON so the browser can auto-render the
sliders and rebuild the live-preview tone LUT without any per-knob JS.

Adding a knob = add one ``Knob`` entry here. If the knob is ``tier="tone"`` it is
applied live in the browser by the tone-curve JS, so its math must also be
mirrored in ``templates/upload.html`` (guarded by ``test_knobs.py``). If it is
``tier="base"`` it changes the server-rendered normalized base and the browser
must refetch the base PNG when it changes.

Defaults are sourced from ``gray16`` so the knobs and the authoritative pipeline
can never drift on their default values.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import gray16


@dataclass(frozen=True)
class Knob:
    id: str
    label: str
    min: float
    max: float
    step: float
    default: float
    # "tone": applied live via the browser tone-curve LUT (mirrors gray16 math).
    # "base": changes the server-rendered base; browser must refetch on change.
    tier: str
    # "range": a slider. "toggle": a 0/1 switch (rendered as a checkbox, parsed as
    # 0.0/1.0). The control type only affects rendering/parsing, not the math.
    type: str = "range"
    # Panel-specific: applies the E1003's measured panel response and is meaningless
    # for devices that do their own grayscale/dither (e.g. Inkplate JPEG). Such
    # knobs are hidden from the upload UI for non-g16z output formats.
    panel_only: bool = False
    help: str = ""

    def clamp(self, value: float) -> float:
        return max(self.min, min(self.max, value))


# Order here is the order shown in the upload form.
KNOBS: tuple[Knob, ...] = (
    Knob(
        id="panel_calibration",
        label="Panel calibration",
        min=0.0,
        max=1.0,
        step=1.0,
        default=gray16.CAL_PANEL_STRENGTH,
        tier="tone",
        type="toggle",
        panel_only=True,
        help="Correct for the panel's measured tonal response. Leave on for the "
        "most natural-looking photos; turn off to send the raw tone curve.",
    ),
    Knob(
        id="brightness",
        label="Brightness",
        min=-0.2,
        max=0.2,
        step=0.02,
        default=gray16.CAL_BRIGHTNESS,
        tier="tone",
        help="Shift the whole image lighter or darker.",
    ),
    Knob(
        id="contrast",
        label="Contrast",
        min=0.75,
        max=1.25,
        step=0.05,
        default=gray16.CAL_CONTRAST,
        tier="tone",
        help="Higher deepens shadows and brightens highlights; lower flattens.",
    ),
    Knob(
        id="midtone",
        label="Midtone contrast",
        min=0.0,
        max=6.0,
        step=0.5,
        default=gray16.CAL_MIDTONE,
        tier="tone",
        help="Spreads muted midtones toward black and white with a soft rolloff, "
        "so flat photos use more of the tonal range without clipping detail.",
    ),
    Knob(
        id="highlights",
        label="Highlights",
        min=-0.5,
        max=0.5,
        step=0.05,
        default=gray16.CAL_HIGHLIGHTS,
        tier="tone",
        help="Negative recovers blown highlights; positive lifts them.",
    ),
    Knob(
        id="gamma",
        label="Gamma",
        min=0.4,
        max=2.0,
        step=0.05,
        default=gray16.CAL_GAMMA,
        tier="tone",
        help="Lower brightens midtones; higher darkens them.",
    ),
)

_BY_ID = {k.id: k for k in KNOBS}


def get(knob_id: str) -> Knob | None:
    return _BY_ID.get(knob_id)


def defaults() -> dict[str, float]:
    """Knob id -> default value."""
    return {k.id: k.default for k in KNOBS}


def parse_values(raw: dict[str, str | float | None]) -> dict[str, float]:
    """Validate/clamp incoming knob values, falling back to defaults.

    Unknown keys are ignored; missing or non-numeric values use the knob default.
    """
    result: dict[str, float] = {}
    for knob in KNOBS:
        value = raw.get(knob.id)
        if value is None or value == "":
            result[knob.id] = knob.default
            continue
        try:
            result[knob.id] = knob.clamp(float(value))
        except (TypeError, ValueError):
            result[knob.id] = knob.default
    return result


def to_client(*, include_panel_only: bool = True) -> list[dict]:
    """Serializable knob descriptors for the upload template / browser.

    ``include_panel_only=False`` drops panel-specific knobs (panel calibration),
    used for resize-only output formats (e.g. Inkplate JPEG) whose device handles
    its own tonal response.
    """
    return [asdict(k) for k in KNOBS if include_panel_only or not k.panel_only]
