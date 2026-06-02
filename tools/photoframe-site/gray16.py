"""Calibrated E1003 Gray16 (G16P) image pipeline.

Copied from ``tools/epaper-prep-gray16.py`` so the photoframe site and the CLI
share the exact same panel calibration. The tonal path is intentionally fixed:

    BT.709 luma -> percentile auto-stretch -> gamma -> anchored highlights bump
    -> nearest-16 + Floyd-Steinberg error diffusion -> G16P pack

IT8951 Gray16 is monotonic (nibble 0x0 = black, 0xF = white), so tone-curved luma
maps straight to nibble value. Keep this module dependency-light (Pillow + stdlib)
and in lockstep with the CLI tool; do not "improve" the curve here in isolation.

NOTE (greenfield): this is a verbatim copy for the MVE. The long-term plan is to
extract a single shared module imported by both the tool and the site.
"""

from __future__ import annotations

import struct
import zlib
from array import array
from math import ceil, isfinite

from PIL import Image

# --- Panel + format constants -------------------------------------------------

PANEL_W = 1872
PANEL_H = 1404
MAGIC = b"G16P"
VERSION = 1

# Lightroom-style tone curve defaults (see the CLI tool for the rationale).
CAL_BLACK_PCT = 0.5
CAL_WHITE_PCT = 99.5
CAL_GAMMA = 0.8
CAL_HIGHLIGHTS = -0.1
HIGHLIGHT_BUMP_EXP = 6.0
_HIGHLIGHT_BUMP_PEAK = (
    (HIGHLIGHT_BUMP_EXP / (HIGHLIGHT_BUMP_EXP + 1.0)) ** HIGHLIGHT_BUMP_EXP
) / (HIGHLIGHT_BUMP_EXP + 1.0)


# --- Low-level helpers --------------------------------------------------------


def _clamp_u8(value: int) -> int:
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


def _luma_bt709(red: int, green: int, blue: int) -> int:
    return (6966 * red + 23436 * green + 2366 * blue) >> 15


def _nearest_gray16(gray: int) -> int:
    value = (gray + 8) // 17
    if value < 0:
        return 0
    if value > 15:
        return 15
    return value


def _percentile_bounds(gray: array, low_pct: float, high_pct: float) -> tuple[int, int]:
    histogram = [0] * 256
    for value in gray:
        histogram[_clamp_u8(value)] += 1

    total = len(gray)
    low_target = total * low_pct / 100.0
    high_target = total * high_pct / 100.0

    black = 0
    cumulative = 0
    for value in range(256):
        cumulative += histogram[value]
        if cumulative >= low_target:
            black = value
            break

    white = 255
    cumulative = 0
    for value in range(256):
        cumulative += histogram[value]
        if cumulative >= high_target:
            white = value
            break

    if white <= black:
        white = min(255, black + 1)
    return black, white


def _build_tone_lut(black: int, white: int, gamma: float, highlights: float = 0.0) -> array:
    lut = array("h", [0]) * 256
    span = white - black
    inv_gamma = 1.0 / gamma if gamma > 0 else 1.0
    for value in range(256):
        if value <= black:
            lut[value] = 0
        elif value >= white:
            lut[value] = 255
        else:
            norm = (value - black) / span
            toned = norm ** inv_gamma
            if highlights != 0.0:
                bump = (toned ** HIGHLIGHT_BUMP_EXP) * (1.0 - toned) / _HIGHLIGHT_BUMP_PEAK
                toned += highlights * bump
                if toned < 0.0:
                    toned = 0.0
                elif toned > 1.0:
                    toned = 1.0
            lut[value] = _clamp_u8(round(toned * 255))
    return lut


# --- Image fit ----------------------------------------------------------------


def _flatten_to_rgb(img: Image.Image) -> Image.Image:
    """Composite any transparency over white and return an RGB image."""
    if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
        rgba = img.convert("RGBA")
        background = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
        return Image.alpha_composite(background, rgba).convert("RGB")
    return img.convert("RGB")


# Resampling filters offered for the panel-fit downscale. NEAREST is the historical
# default (matches the CLI tool and avoids resample blur before dithering); the
# others trade hard edges for smoother detail. Keys are the stable names stored in
# meta and submitted by the upload form. Order here is the order shown in the UI.
RESAMPLERS: dict[str, Image.Resampling] = {
    "nearest": Image.Resampling.NEAREST,
    "box": Image.Resampling.BOX,
    "bilinear": Image.Resampling.BILINEAR,
    "hamming": Image.Resampling.HAMMING,
    "bicubic": Image.Resampling.BICUBIC,
    "lanczos": Image.Resampling.LANCZOS,
}
DEFAULT_RESAMPLER = "nearest"


def resampler_choices() -> list[str]:
    """Stable resampler names for the upload UI (in display order)."""
    return list(RESAMPLERS.keys())


def resolve_resampler(name: str | None) -> Image.Resampling:
    """Map a resampler name to its PIL filter, falling back to the default."""
    return RESAMPLERS.get((name or "").lower(), RESAMPLERS[DEFAULT_RESAMPLER])


def fit_rgb_to_panel(
    img: Image.Image,
    width: int = PANEL_W,
    height: int = PANEL_H,
    *,
    resample: Image.Resampling | None = None,
) -> Image.Image:
    """Flatten transparency over white and cover-crop to the panel size.

    ``resample`` selects the downscale filter (default NEAREST, matching the CLI
    tool: avoids resample blur before dithering). Smoother filters (BICUBIC,
    LANCZOS) retain more detail at the cost of softer edges -- experiment per
    image via the upload page.
    """
    img = _flatten_to_rgb(img)
    resample = resample or RESAMPLERS[DEFAULT_RESAMPLER]

    if img.size != (width, height):
        scale = max(width / img.width, height / img.height)
        resized_w = max(width, ceil(img.width * scale))
        resized_h = max(height, ceil(img.height * scale))
        img = img.resize((resized_w, resized_h), resample)

        left = (resized_w - width) // 2
        top = (resized_h - height) // 2
        img = img.crop((left, top, left + width, top + height))

    return img.convert("RGB")


def apply_orientation(img: Image.Image, transform: dict) -> Image.Image:
    """Apply per-device orientation: rotate_deg then mirror_x then mirror_y.

    Pure geometry; no tonal effect. Rotation is the only step that may change
    dimensions, so callers should fit to the panel after orientation.
    """
    if not transform:
        return img
    rotate_deg = int(transform.get("rotate_deg", 0) or 0) % 360
    if rotate_deg:
        # expand=True keeps the whole image; subsequent fit re-crops to panel.
        img = img.rotate(-rotate_deg, expand=True)
    if bool(transform.get("mirror_x", False)):
        img = img.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    if bool(transform.get("mirror_y", False)):
        img = img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    return img


def apply_crop(img: Image.Image, crop: dict | None) -> Image.Image:
    """Frame to a normalized rect ``{x, y, w, h}`` (fractions of width/height).

    A falsy, invalid, or full-frame crop is a no-op, so callers can always pass
    the crop through unconditionally. The rect may extend OUTSIDE the image
    (negative x/y or x+w > 1, y+h > 1): the region beyond the image is filled
    white, which lets the user zoom out and letterbox a portrait inside a
    landscape frame (or vice versa). Pure geometry; no tonal effect. Apply after
    orientation and before fitting to the panel so the framing survives the fit.
    """
    if not crop:
        return img
    try:
        x = float(crop.get("x", 0.0))
        y = float(crop.get("y", 0.0))
        w = float(crop.get("w", 1.0))
        h = float(crop.get("h", 1.0))
    except (TypeError, ValueError, AttributeError):
        return img
    if not (isfinite(x) and isfinite(y) and isfinite(w) and isfinite(h)):
        return img
    if w <= 0.0 or h <= 0.0:
        return img
    src_w, src_h = img.size
    left = round(x * src_w)
    top = round(y * src_h)
    right = round((x + w) * src_w)
    bottom = round((y + h) * src_h)
    if right - left < 1 or bottom - top < 1:
        return img
    if left == 0 and top == 0 and right == src_w and bottom == src_h:
        return img
    if left >= 0 and top >= 0 and right <= src_w and bottom <= src_h:
        return img.crop((left, top, right, bottom))
    # Window extends beyond the image: paste the overlapping part onto a white
    # canvas so the uncovered margins become letterbox/pillarbox bars.
    win_w = right - left
    win_h = bottom - top
    canvas = Image.new("RGB", (win_w, win_h), (255, 255, 255))
    canvas.paste(_flatten_to_rgb(img), (-left, -top))
    return canvas


def describe_crop(crop: dict | None, src_w: int, src_h: int) -> str:
    """One-line summary of what ``apply_crop`` would do for these dims.

    Diagnostic only: mirrors the branch logic in ``apply_crop`` so logs show the
    chosen action (noop / inside-crop / letterbox) and the pixel window, without
    decoding the image. ``src_w/src_h`` are the post-orientation source size.
    """
    if not crop:
        return "noop(no-crop)"
    try:
        x = float(crop.get("x", 0.0)); y = float(crop.get("y", 0.0))
        w = float(crop.get("w", 1.0)); h = float(crop.get("h", 1.0))
    except (TypeError, ValueError, AttributeError):
        return "noop(invalid)"
    if not (isfinite(x) and isfinite(y) and isfinite(w) and isfinite(h)):
        return "noop(non-finite)"
    if w <= 0.0 or h <= 0.0:
        return f"noop(w<=0|h<=0 w={w:.3f} h={h:.3f})"
    left = round(x * src_w); top = round(y * src_h)
    right = round((x + w) * src_w); bottom = round((y + h) * src_h)
    win = f"norm(x={x:.3f},y={y:.3f},w={w:.3f},h={h:.3f}) px[L={left},T={top},R={right},B={bottom}] win={right-left}x{bottom-top}"
    if right - left < 1 or bottom - top < 1:
        return f"noop(sub-1px) {win}"
    if left == 0 and top == 0 and right == src_w and bottom == src_h:
        return f"noop(full-frame) {win}"
    if left >= 0 and top >= 0 and right <= src_w and bottom <= src_h:
        return f"inside-crop {win}"
    pad_l = max(0, -left); pad_t = max(0, -top)
    pad_r = max(0, right - src_w); pad_b = max(0, bottom - src_h)
    return f"letterbox(padL={pad_l},T={pad_t},R={pad_r},B={pad_b}) {win}"


# --- Tone + dither ------------------------------------------------------------


def _luma_array(rgb: Image.Image, width: int, height: int) -> array:
    pixels = rgb.tobytes()
    pixel_count = width * height
    gray = array("h", [0]) * pixel_count
    for pixel_index in range(pixel_count):
        source_index = pixel_index * 3
        gray[pixel_index] = _luma_bt709(
            pixels[source_index],
            pixels[source_index + 1],
            pixels[source_index + 2],
        )
    return gray


def _dither_to_nibbles(gray: array, width: int, height: int) -> bytearray:
    packed = bytearray((width * height) // 2)
    for y in range(height):
        row = y * width
        for x in range(width):
            idx = row + x
            old = _clamp_u8(gray[idx])
            q = _nearest_gray16(old)
            packed_index = idx >> 1
            if x & 1:
                packed[packed_index] |= q
            else:
                packed[packed_index] = q << 4

            err = old - (q * 17)
            if x + 1 < width:
                gray[idx + 1] = _clamp_u8(gray[idx + 1] + err * 7 // 16)
            if y + 1 < height:
                if x > 0:
                    gray[idx + width - 1] = _clamp_u8(gray[idx + width - 1] + err * 3 // 16)
                gray[idx + width] = _clamp_u8(gray[idx + width] + err * 5 // 16)
                if x + 1 < width:
                    gray[idx + width + 1] = _clamp_u8(gray[idx + width + 1] + err // 16)
    return packed


def calibrated_gray_levels(
    rgb: Image.Image,
    *,
    width: int = PANEL_W,
    height: int = PANEL_H,
    gamma: float = CAL_GAMMA,
    highlights: float = CAL_HIGHLIGHTS,
    auto_stretch: bool = True,
) -> array:
    """Return the tone-curved (pre-dither) 8-bit luma for an already-fit RGB image."""
    gray = _luma_array(rgb, width, height)
    if auto_stretch:
        black, white = _percentile_bounds(gray, CAL_BLACK_PCT, CAL_WHITE_PCT)
    else:
        black, white = 0, 255
    tone_lut = _build_tone_lut(black, white, gamma, highlights)
    for i in range(width * height):
        gray[i] = tone_lut[_clamp_u8(gray[i])]
    return gray


def gray_levels_to_preview(gray: array, width: int, height: int) -> Image.Image:
    """Build an 8-bit 'L' preview image from tone-curved luma (for thumbnails)."""
    return Image.frombytes("L", (width, height), bytes(_clamp_u8(v) for v in gray))


# Display-simulation gamma: the real E1003 e-paper panel renders its gray levels
# noticeably lighter than an sRGB monitor, so the encoded (device-faithful) tone
# looks darker on screen than on the panel. This gamma (<1 lightens) approximates
# the panel's response for on-screen previews and gallery thumbnails. It is a
# DISPLAY-ONLY correction: never apply it before encoding G16P. Single source of
# truth -- the upload page reads this value from the server so the live JS preview
# and the baked thumbnail stay in sync.
PREVIEW_DISPLAY_GAMMA = 0.75
_DISPLAY_SIM_LUT = bytes(
    _clamp_u8(round((v / 255.0) ** PREVIEW_DISPLAY_GAMMA * 255.0)) for v in range(256)
)


def simulate_display(img: Image.Image) -> Image.Image:
    """Lighten an 'L' image to approximate how the e-paper panel renders it.

    Mirror of the browser DISPLAY_LUT in upload.html (same PREVIEW_DISPLAY_GAMMA).
    Display-only: used for gallery thumbnails so they match the live preview.
    """
    return img.convert("L").point(_DISPLAY_SIM_LUT)


# Longest edge of the small grayscale base served for the live upload preview.
PREVIEW_MAX = 468


def full_base(
    img: Image.Image,
    *,
    transform: dict | None = None,
    max_edge: int = PREVIEW_MAX,
) -> Image.Image:
    """Server half of the live preview: orient -> luma -> auto-stretch (no crop).

    Returns a small grayscale ('L') image of the WHOLE oriented image at IDENTITY
    tone (gamma=1, highlights=0), preserving the image's own aspect ratio (NOT the
    device aspect). The browser pans/zooms a device-aspect window over this base
    and applies the tone + display knobs live on top, so neither the tone curve
    nor the framing is baked in here. Auto-stretch IS applied (image-dependent,
    not a knob) so the browser LUT operates on normalized luma like the
    authoritative pipeline. Dither is intentionally omitted.

    The base is fetched once per image; all subsequent framing is client-side, so
    the on-screen brightness stays stable while panning. The final encode applies
    auto-stretch over the chosen crop, so a tight crop may differ slightly from
    the preview's full-image stretch -- an accepted, subtle tradeoff.
    """
    oriented = apply_orientation(img, transform or {})
    rgb = _flatten_to_rgb(oriented)
    src_w, src_h = rgb.size
    if src_w >= src_h:
        pv_w = max_edge
        pv_h = max(1, round(max_edge * src_h / src_w))
    else:
        pv_h = max_edge
        pv_w = max(1, round(max_edge * src_w / src_h))
    small = rgb.resize((pv_w, pv_h), Image.Resampling.NEAREST)
    gray = calibrated_gray_levels(small, width=pv_w, height=pv_h, gamma=1.0, highlights=0.0)
    return gray_levels_to_preview(gray, pv_w, pv_h)



def pack_g16p(packed_nibbles: bytes, width: int, height: int) -> bytes:
    """Prepend the 18-byte G16P header to packed nibble payload."""
    header = struct.pack(
        "<4sBBHHII",
        MAGIC,
        VERSION,
        0,
        width,
        height,
        len(packed_nibbles),
        zlib.crc32(packed_nibbles) & 0xFFFFFFFF,
    )
    return header + bytes(packed_nibbles)


def encode_g16p(
    img: Image.Image,
    *,
    width: int = PANEL_W,
    height: int = PANEL_H,
    transform: dict | None = None,
    crop: dict | None = None,
    gamma: float = CAL_GAMMA,
    highlights: float = CAL_HIGHLIGHTS,
    resampler: str | None = None,
) -> tuple[bytes, Image.Image]:
    """Full upload pipeline: orientation -> fit -> calibrated tone -> dither -> G16P.

    Returns ``(g16p_bytes, preview_L)`` where ``preview_L`` is the post-tone,
    pre-pack grayscale image suitable for generating a gallery thumbnail.
    ``resampler`` names the panel-fit downscale filter (see ``RESAMPLERS``).
    """
    oriented = apply_orientation(img, transform or {})
    framed = apply_crop(oriented, crop)
    rgb = fit_rgb_to_panel(framed, width, height, resample=resolve_resampler(resampler))
    gray = calibrated_gray_levels(rgb, width=width, height=height, gamma=gamma, highlights=highlights)
    preview = gray_levels_to_preview(gray, width, height)
    nibbles = _dither_to_nibbles(gray, width, height)
    return pack_g16p(nibbles, width, height), preview
