#!/usr/bin/env python3
"""Generate PNG previews for gauge start-label geometry.

Why this exists:
- Fast visual harness to validate label anchor/rotation math before firmware changes.
- Lets you inspect all start angles (0..360) and catch quadrant/flip regressions.
- Mirrors the firmware's tangent-based start-label placement behavior.

How to use:
- Run: `python3 tools/gauge_label_preview.py`
- Output: `build/tmp/gauge-label-preview/case_000.png` ... `case_360.png`
- Toggle debug overlays with DEBUG_SHOW_GUIDES / DEBUG_SHOW_RECTANGLES.

This is a developer tool only. It does not ship in firmware.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Iterable
from typing import Literal

from PIL import Image, ImageDraw, ImageFont

OUT_DIR = Path(__file__).resolve().parents[1] / "build" / "tmp" / "gauge-label-preview"
SIZE = (720, 720)
CENTER = (360, 360)
RINGS = [220, 180, 140]
RING_WIDTH = 26
ARC_DEGREES = 300
RING_COLORS = ["#4CAF50", "#2196F3", "#9C27B0"]
PLACEHOLDER_LABELS = ["abc", "def", "ghi"]
CONNECTOR_LEN = 8
RECT_LEN = 44
RECT_THICKNESS = RING_WIDTH
BG = "#101418"
TRACK = "#2A3138"
TEXT = "#D0D7DE"
DEBUG_SHOW_GUIDES = True
DEBUG_SHOW_RECTANGLES = True


def deg_to_screen_xy(cx: int, cy: int, radius: int, deg: float) -> tuple[float, float]:
    rad = math.radians(deg)
    return cx + math.cos(rad) * radius, cy + math.sin(rad) * radius


def draw_rotated_text_left_middle(
    canvas: Image.Image,
    text: str,
    anchor_xy: tuple[float, float],
    angle_deg: float,
    font: ImageFont.ImageFont,
    color: tuple[int, int, int, int],
    anchor_mode: Literal["leading", "trailing"] = "leading",
) -> None:
    # Draw text so a chosen anchor point stays fixed after rotation.
    dummy = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
    dd = ImageDraw.Draw(dummy)
    l, t, r, b = dd.textbbox((0, 0), text, font=font)
    tw, th = (r - l), (b - t)

    pad = 8
    tile_w, tile_h = tw + pad * 2, th + pad * 2
    tile = Image.new("RGBA", (tile_w, tile_h), (0, 0, 0, 0))
    td = ImageDraw.Draw(tile)
    text_x = pad - l
    text_y = pad - t
    td.text((text_x, text_y), text, fill=color, font=font)

    # Horizontal anchor: use text edges, not glyph centers. In flipped cases,
    # the right edge must stay on the connector so the label grows outward.
    if anchor_mode == "leading":
        ax = text_x
        c_l, c_t, c_r, c_b = td.textbbox((text_x, text_y), text[:1] or " ", font=font)
    else:
        ax = text_x + tw
        c_l, c_t, c_r, c_b = td.textbbox((text_x, text_y), text[-1:] or " ", font=font)

    # Vertical anchor: font metrics centerline, not per-letter bbox center.
    # This keeps labels with different glyphs (a/d/g) visually aligned.
    if hasattr(font, "getmetrics"):
        ascent, descent = font.getmetrics()
        ay = text_y + (ascent + descent) / 2.0
    else:
        ay = (c_t + c_b) / 2.0

    # Rotate a 1px anchor marker using the exact same PIL path, then locate
    # that marker in rotated space. This avoids sign/frame mismatches.
    marker = Image.new("L", (tile_w, tile_h), 0)
    md = ImageDraw.Draw(marker)
    msize = 2
    md.rectangle(
        (
            int(round(ax - msize)),
            int(round(ay - msize)),
            int(round(ax + msize)),
            int(round(ay + msize)),
        ),
        fill=255,
    )

    rotated = tile.rotate(angle_deg, resample=Image.Resampling.BICUBIC, expand=True)
    marker_rot = marker.rotate(angle_deg, resample=Image.Resampling.NEAREST, expand=True)
    bbox = marker_rot.getbbox()
    if bbox:
        mx0, my0, mx1, my1 = bbox
        anchor_rx = (mx0 + mx1 - 1) / 2.0
        anchor_ry = (my0 + my1 - 1) / 2.0
    else:
        anchor_rx = rotated.width / 2.0
        anchor_ry = rotated.height / 2.0

    px = int(round(anchor_xy[0] - anchor_rx))
    py = int(round(anchor_xy[1] - anchor_ry))
    canvas.alpha_composite(rotated, (px, py))


def corrected_label_angle(rect_angle_deg: float) -> tuple[float, bool]:
    # Mirror text angle against the x-axis in screen coordinates.
    # This is a single, angle-agnostic rule that matches all validated cases,
    # including 000 where the label should flip by 180 degrees.
    angle = -rect_angle_deg
    flipped_for_readability = False

    # Readability cheat: keep text upright-ish by flipping upside-down angles.
    # This preserves the same anchor because placement is anchor-driven after
    # rotation, not bbox-driven.
    if angle > 90.0 or angle < -90.0:
        angle += 180.0
        flipped_for_readability = True

    # Normalize to [-180, 180) for stable debugging/inspection.
    while angle >= 180.0:
        angle -= 360.0
    while angle < -180.0:
        angle += 360.0
    return angle, flipped_for_readability


def render_case(start_angle: float, filename: str) -> None:
    img = Image.new("RGBA", SIZE, BG)
    draw = ImageDraw.Draw(img)

    cx, cy = CENTER
    start = start_angle
    end = start_angle + ARC_DEGREES

    # Track arcs + colored indicator stroke for all rings.
    for radius, ring_color in zip(RINGS, RING_COLORS):
        box = (cx - radius, cy - radius, cx + radius, cy + radius)
        draw.arc(box, start=start, end=end, fill=TRACK, width=RING_WIDTH)
        draw.arc(box, start=start, end=start + ARC_DEGREES * 0.62, fill=ring_color, width=RING_WIDTH)

    # Optional center marker for geometry debugging.
    if DEBUG_SHOW_GUIDES:
        draw.ellipse((cx - 4, cy - 4, cx + 4, cy + 4), fill="#8B949E")

    try:
        font_main = ImageFont.truetype("DejaVuSans.ttf", 18)
    except Exception:
        font_main = ImageFont.load_default()

    # Draw labels anchored near each ring start point.
    for idx, (radius, ring_color) in enumerate(zip(RINGS, RING_COLORS), start=1):
        sx, sy = deg_to_screen_xy(cx, cy, radius, start_angle)
        # PIL arc stroke is effectively offset from our nominal radius; move
        # inward by half stroke width so anchor math targets the stroke centerline.
        a_rad = math.radians(start_angle)
        sx -= math.cos(a_rad) * (RING_WIDTH / 2.0)
        sy -= math.sin(a_rad) * (RING_WIDTH / 2.0)

        # Draw a tangent-oriented connector line from the ring start dot.
        ux, uy = math.sin(a_rad), -math.cos(a_rad)  # tangent direction
        vx, vy = math.cos(a_rad), math.sin(a_rad)   # perpendicular direction

        # Connector goes from dot center to label anchor point.
        bx = sx + ux * CONNECTOR_LEN
        by = sy + uy * CONNECTOR_LEN

        if DEBUG_SHOW_RECTANGLES:
            half_t = RECT_THICKNESS / 2.0
            x0 = bx - vx * half_t
            y0 = by - vy * half_t
            x1 = bx + vx * half_t
            y1 = by + vy * half_t
            x2 = x1 + ux * RECT_LEN
            y2 = y1 + uy * RECT_LEN
            x3 = x0 + ux * RECT_LEN
            y3 = y0 + uy * RECT_LEN
            draw.polygon(
                [(x0, y0), (x1, y1), (x2, y2), (x3, y3)],
                outline="#6B7280",
                width=1,
            )

        if DEBUG_SHOW_GUIDES:
            draw.line((sx, sy, bx, by), fill="#FF3B30", width=2)

        # Place placeholder text with edge-based alignment so normal cases grow
        # from the left edge and readability-flipped cases grow from the right.
        label = PLACEHOLDER_LABELS[idx - 1] if idx - 1 < len(PLACEHOLDER_LABELS) else f"r{idx}"
        # Distance from ring to label is controlled by CONNECTOR_LEN only.
        rect_angle = math.degrees(math.atan2(uy, ux))
        label_angle, flipped_for_readability = corrected_label_angle(rect_angle)
        anchor_mode: Literal["leading", "trailing"] = "trailing" if flipped_for_readability else "leading"

        draw_rotated_text_left_middle(
            img,
            label,
            (bx, by),
            label_angle,
            font_main,
            (255, 59, 48, 255),
            anchor_mode,
        )

        # Optional red start-point marker for geometry debugging.
        if DEBUG_SHOW_GUIDES:
            draw.ellipse((sx - 4, sy - 4, sx + 4, sy + 4), fill="#FF3B30")

    # Header text.
    draw.text((18, 18), f"start_angle={start_angle:.0f}", fill=TEXT, font=font_main)
    helper = (
        f"guides: {'ON' if DEBUG_SHOW_GUIDES else 'OFF'} "
        f"| rectangles: {'ON' if DEBUG_SHOW_RECTANGLES else 'OFF'}"
    )
    draw.text((18, 40), helper, fill="#9BA7B4", font=font_main)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    img.convert("RGB").save(OUT_DIR / filename, "PNG")


def main() -> None:
    cases: Iterable[tuple[float, str]] = [
        (float(angle), f"case_{angle:03d}.png") for angle in range(0, 361)
    ]
    for angle, name in cases:
        render_case(angle, name)

    print(f"Wrote {len(list(cases))} files to {OUT_DIR}")


if __name__ == "__main__":
    main()
