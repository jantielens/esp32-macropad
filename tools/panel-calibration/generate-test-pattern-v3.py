#!/usr/bin/env python3
"""Generate the E1003 bench-measurement-3 Gray16 test pattern (Pattern v3).

Pattern v3 replaces the simple black-square corner fiducials of v2 with four
OpenCV ArUco markers (DICT_4X4_50, IDs 0-3) placed inset 150px from the panel
edges. This enables robust automated perspective correction in sample-v3.py.

The G16P writer / packing / quantizer code mirrors the feature/e1003
tools/epaper-prep-gray16.py container format: 1872x1404, G16P magic, version 1,
packed Gray16 nibbles with even-x pixels in the high nibble and odd-x pixels in
the low nibble. That code is carried over verbatim from bench-measurement-2.

Layout (canonical 1872x1404, level 15 = white background):

    +--------------------------------------------------------------+
    | [M0]  refTL   dither-ramp   uniformity   refTR        [M1]   |
    |                                                              |
    |   0|1|2|3|4|5|6|7|8|9|A|B|C|D|E|F   (16 bar+white-rail pairs)|
    |   | | each gray bar is paired with a full-height white rail |
    |   0|1|2|3|4|5|6|7|8|9|A|B|C|D|E|F                            |
    |                                                              |
    | [M2]  refBL       checkerboard        refBR          [M3]   |
    +--------------------------------------------------------------+

Pattern v3.1 layout change: the wedge is the only artifact that feeds the LUT,
so it is given the entire interior between the markers (full width, ~744px
tall). The markers are pushed toward the corners and the support content
(reference patches, dither ramp, uniformity patch, registration checkerboard)
is relocated into the top and bottom horizontal bands beside the markers. The
sampler measures each large bar with a glare-robust estimator (reject bright
specular outliers, average the even plateau) instead of a naive mean.

Pattern v3.2 layout change: each gray bar is narrowed and a full-height white
rail (level 15) is placed beside it, separated by a guard gap. This gives a
true white reference co-located at every bar's own y (the centre illumination
band), in a SINGLE frame. The corner white refs only cover the top/bottom
bands, so a bilinear white field cannot model the centre-peaked illumination
hump and bright bars read reflectance > 1.0 (highlight clamp). The co-located
rails let the sampler fit a quadratic-in-y white surface that captures the
hump, fixing highlight measurement. Black stays as the corner bilinear field
(shadows validate at RMS 0.0066 and are weakly sensitive to the hump).

Acceptance-relevant guarantees verified by verify():
  * G16P payload round-trips exactly (bit-identical packed nibbles).
  * Each wedge bar sample_rect contains exactly one distinct level (0..15).
  * Each white-rail sample_rect contains exactly level 15.
  * Re-detecting ArUco markers on the rendered 8-bit preview finds all four
    IDs at corner coordinates within +/-1px of the recorded canonical corners.

Author: Marge / coding agent — bench-measurement-3.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from array import array
from pathlib import Path
from typing import Iterable

try:
    import numpy as np
except Exception as exc:  # pragma: no cover
    print("ERROR: numpy is required. Install with: python3 -m pip install --user numpy", file=sys.stderr)
    print(f"Details: {exc}", file=sys.stderr)
    sys.exit(2)

try:
    import cv2
except Exception as exc:  # pragma: no cover
    print("ERROR: opencv-python (cv2) is required. Install with: python3 -m pip install --user opencv-python", file=sys.stderr)
    print(f"Details: {exc}", file=sys.stderr)
    sys.exit(2)

try:
    from PIL import Image, ImageDraw, ImageFont
except Exception as exc:  # pragma: no cover
    print("ERROR: Pillow is required. Install with: python3 -m pip install --user pillow", file=sys.stderr)
    print(f"Details: {exc}", file=sys.stderr)
    sys.exit(2)

PANEL_W = 1872
PANEL_H = 1404
MAGIC = b"G16P"
VERSION = 1
HEADER_STRUCT = struct.Struct("<4sBBHHII")
HEADER_SIZE = HEADER_STRUCT.size
PAYLOAD_SIZE = PANEL_W * PANEL_H // 2

# ---------------------------------------------------------------------------
# ArUco configuration
# ---------------------------------------------------------------------------
ARUCO_DICT_NAME = "DICT_4X4_50"
MARKER_SIZE = 160  # px on panel; larger markers survive partial glare better

# (id, top-left x, top-left y) of each marker. Pushed toward the corners
# (64px edge margin) so the wedge can claim the whole interior. A wide marker
# baseline also improves homography conditioning.
MARKER_MARGIN = 64
MARKERS = [
    (0, MARKER_MARGIN, MARKER_MARGIN),                                  # top-left
    (1, PANEL_W - MARKER_MARGIN - MARKER_SIZE, MARKER_MARGIN),          # top-right
    (2, MARKER_MARGIN, PANEL_H - MARKER_MARGIN - MARKER_SIZE),          # bottom-left
    (3, PANEL_W - MARKER_MARGIN - MARKER_SIZE,
        PANEL_H - MARKER_MARGIN - MARKER_SIZE),                         # bottom-right
]

# ---------------------------------------------------------------------------
# Content layout
# ---------------------------------------------------------------------------
# Reference B/W pairs live in the top and bottom bands, near the four interior
# corners, giving good 2D coverage for the bilinear flat-field fit.
REF_SIZE = 104
REF_GAP = 14  # gap between the black and white patch of a pair
# (location, pair-left x, pair-top y) -- 4 corner B/W pairs (8 patches total).
REF_PAIRS = [
    ("top_left", 260, 84),
    ("top_right", 1390, 84),
    ("bottom_left", 260, 1196),
    ("bottom_right", 1390, 1196),
]

# Registration checkerboard sits in the centre of the bottom band.
CHECKER_ORIGIN = (880, 1200)
CHECKER_CELLS = 4
CHECKER_CELL = 28  # => 112x112 total

# The wedge claims the full interior between the markers. Each 108px slot is
# split into a gray bar + guard gap + white rail (v3.2 co-located white ref).
DEFAULT_WEDGE = (72, 336, 1728, 744)    # 16 slots x 108px wide, ~744px tall
RAIL_W = 26      # white rail width beside each gray bar (co-located white ref)
RAIL_GAP = 6     # guard gap between gray bar and rail (PSF bleed buffer)
# Diagnostics live in the top band centre (between the corner reference pairs).
DEFAULT_UNIFORM = (1140, 96, 200, 80)
DEFAULT_DITHER = (540, 96, 560, 80)


def clamp_u8(value: int) -> int:
    return max(0, min(255, value))


def nearest_gray16(gray: int) -> int:
    """Same linear quantizer as feature/e1003 _nearest_gray16 (0->0, 255->15)."""
    return max(0, min(15, (gray + 8) // 17))


def rect_obj(x: int, y: int, w: int, h: int) -> dict[str, int]:
    return {"x": x, "y": y, "w": w, "h": h}


def inset_rect(x: int, y: int, w: int, h: int, fraction: float = 0.20) -> dict[str, int]:
    dx = round(w * fraction)
    dy = round(h * fraction)
    return rect_obj(x + dx, y + dy, w - 2 * dx, h - 2 * dy)


def fill_rect(levels: array, x: int, y: int, w: int, h: int, level: int) -> None:
    level = max(0, min(15, level))
    for yy in range(y, y + h):
        row = yy * PANEL_W
        levels[row + x : row + x + w] = array("B", [level]) * w


def aruco_dictionary() -> "cv2.aruco.Dictionary":
    return cv2.aruco.getPredefinedDictionary(getattr(cv2.aruco, ARUCO_DICT_NAME))


def aruco_detector() -> "cv2.aruco.ArucoDetector":
    return cv2.aruco.ArucoDetector(aruco_dictionary())


def marker_corners(mx: int, my: int) -> list[list[int]]:
    """Outer corner coordinates (TL, TR, BR, BL) of a marker placed at (mx,my).

    Matches the corner order returned by cv2.aruco.detectMarkers (clockwise
    from top-left). The outer extent is the last pixel index, MARKER_SIZE-1.
    """
    last = MARKER_SIZE - 1
    return [
        [mx, my],
        [mx + last, my],
        [mx + last, my + last],
        [mx, my + last],
    ]


def blit_aruco(levels: array, marker_id: int, mx: int, my: int) -> dict:
    """Render an ArUco marker into the level array via nearest_gray16.

    The background is already level 15 (white), supplying the required quiet
    zone around the marker (>=30px on all sides given the 150px edge margin).
    """
    img = cv2.aruco.generateImageMarker(aruco_dictionary(), marker_id, MARKER_SIZE)
    for yy in range(MARKER_SIZE):
        row = (my + yy) * PANEL_W
        for xx in range(MARKER_SIZE):
            levels[row + mx + xx] = nearest_gray16(int(img[yy, xx]))
    return {
        "id": marker_id,
        "dictionary": ARUCO_DICT_NAME,
        "rect": rect_obj(mx, my, MARKER_SIZE, MARKER_SIZE),
        # Corner order matches cv2.aruco.detectMarkers output: TL, TR, BR, BL.
        "corners": marker_corners(mx, my),
    }


def draw_label(levels: array, x: int, y: int, w: int, bar_level: int, label: str) -> dict[str, int]:
    sw = min(54, w - 12)
    sh = 38
    sx = x + (w - sw) // 2
    sy = y
    swatch_level = 15 if bar_level < 8 else 0
    text_level = 0 if swatch_level == 15 else 15
    fill_rect(levels, sx, sy, sw, sh, swatch_level)

    img = Image.new("L", (sw, sh), swatch_level * 17)
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("DejaVuSansMono-Bold.ttf", 24)
    except Exception:
        font = ImageFont.load_default()
    bbox = draw.textbbox((0, 0), label, font=font)
    tx = (sw - (bbox[2] - bbox[0])) // 2 - bbox[0]
    ty = (sh - (bbox[3] - bbox[1])) // 2 - bbox[1]
    draw.text((tx, ty), label, fill=text_level * 17, font=font)
    pix = img.load()
    for yy in range(sh):
        row = (sy + yy) * PANEL_W
        for xx in range(sw):
            levels[row + sx + xx] = nearest_gray16(pix[xx, yy])
    return rect_obj(sx, sy, sw, sh)


def draw_checkerboard(levels: array, ox: int, oy: int, cells: int, cell: int) -> dict:
    """4x4 alternating level 0 / 15 checkerboard, top-left cell = level 0.

    Returns the manifest entry including expected interior edge coordinates so
    the sampler can verify homography alignment after warping.
    """
    for cy in range(cells):
        for cx in range(cells):
            level = 0 if ((cx + cy) % 2 == 0) else 15
            fill_rect(levels, ox + cx * cell, oy + cy * cell, cell, cell, level)
    total = cells * cell
    v_edges = [ox + i * cell for i in range(cells + 1)]
    h_edges = [oy + i * cell for i in range(cells + 1)]
    return {
        "id": "registration_checkerboard",
        "rect": rect_obj(ox, oy, total, total),
        "cells": cells,
        "cell_size": cell,
        "top_left_level": 0,
        "vertical_edges_x": v_edges,
        "horizontal_edges_y": h_edges,
    }


def floyd_steinberg_ramp(levels: array, x: int, y: int, w: int, h: int) -> None:
    gray = array("h", [0]) * (w * h)
    for yy in range(h):
        for xx in range(w):
            gray[yy * w + xx] = round(255 * xx / max(1, w - 1))

    for yy in range(h):
        for xx in range(w):
            idx = yy * w + xx
            old = clamp_u8(gray[idx])
            q = nearest_gray16(old)
            levels[(y + yy) * PANEL_W + (x + xx)] = q
            err = old - q * 17
            if xx + 1 < w:
                gray[idx + 1] = clamp_u8(gray[idx + 1] + err * 7 // 16)
            if yy + 1 < h:
                if xx > 0:
                    gray[idx + w - 1] = clamp_u8(gray[idx + w - 1] + err * 3 // 16)
                gray[idx + w] = clamp_u8(gray[idx + w] + err * 5 // 16)
                if xx + 1 < w:
                    gray[idx + w + 1] = clamp_u8(gray[idx + w + 1] + err // 16)


def build_pattern(args: argparse.Namespace) -> tuple[array, dict]:
    levels = array("B", [15]) * (PANEL_W * PANEL_H)
    manifest: dict = {
        "pattern": "e1003-bench-measurement-3-v3.2",
        "dimensions": {"width": PANEL_W, "height": PANEL_H},
        "gray16_levels": {"0": "panel black", "15": "panel white"},
        "fiducial_type": "aruco",
        "aruco": {
            "dictionary": ARUCO_DICT_NAME,
            "marker_size_px": MARKER_SIZE,
            "ids": [m[0] for m in MARKERS],
            "corner_order": "TL,TR,BR,BL (matches cv2.aruco.detectMarkers)",
        },
        "sampling_note": "Sample sample_rect regions only; avoid labels, gutters, patch edges and marker quiet zones.",
    }

    # ArUco markers: inset 150px, defining the canonical coordinate frame.
    manifest["markers"] = []
    for marker_id, mx, my in MARKERS:
        manifest["markers"].append(blit_aruco(levels, marker_id, mx, my))

    # Reference B/W pairs: 5 positions, paired black/white for local lighting
    # field + endpoint estimation (same role as bench-2).
    refs = []
    for loc, x, y in REF_PAIRS:
        for role, level, ox in (("black", 0, 0), ("white", 15, REF_SIZE + REF_GAP)):
            fill_rect(levels, x + ox, y, REF_SIZE, REF_SIZE, level)
            refs.append(
                {
                    "id": f"{loc}_{role}",
                    "location": loc,
                    "role": role,
                    "level": level,
                    "rect": rect_obj(x + ox, y, REF_SIZE, REF_SIZE),
                    "sample_rect": inset_rect(x + ox, y, REF_SIZE, REF_SIZE, 0.20),
                }
            )
    manifest["reference_patches"] = refs

    # Registration checkerboard (homography quality self-test).
    cox, coy = CHECKER_ORIGIN
    manifest["checkerboard"] = draw_checkerboard(levels, cox, coy, CHECKER_CELLS, CHECKER_CELL)

    # Main LUT wedge: 16 gray bars, each paired with a full-height white rail.
    # Every 108px slot is split into [gray bar | guard gap | white rail] so a
    # true white reference sits at each bar's own y (centre illumination band).
    wx, wy, ww, wh = args.wedge
    slot_w = ww // 16
    gray_w = slot_w - RAIL_W - RAIL_GAP
    bars = []
    white_rails = []
    label_rects = []
    for level in range(16):
        sx = wx + level * slot_w
        slot = slot_w if level < 15 else wx + ww - sx
        gw = (gray_w if level < 15 else slot - RAIL_W - RAIL_GAP)
        # Gray bar (left part of the slot).
        fill_rect(levels, sx, wy, gw, wh, level)
        # Label swatch sits just inside the top of the bar, within the
        # sample_rect's top inset so it is never measured.
        label_rects.append(draw_label(levels, sx, wy + 10, gw, level, f"{level:X}"))
        bars.append(
            {
                "id": f"bar_{level:02d}",
                "label": f"{level:X}",
                "level": level,
                "rect": rect_obj(sx, wy, gw, wh),
                "sample_rect": inset_rect(sx, wy, gw, wh, 0.12),
            }
        )
        # White rail (right part of the slot). Background is already level 15,
        # but fill explicitly so verify() can assert it. Sample_rect is inset
        # hard (0.20) so the PSF bleed from the adjacent gray edge is excluded.
        rx = sx + gw + RAIL_GAP
        fill_rect(levels, rx, wy, RAIL_W, wh, 15)
        white_rails.append(
            {
                "id": f"white_rail_{level:02d}",
                "level": 15,
                "rect": rect_obj(rx, wy, RAIL_W, wh),
                "sample_rect": inset_rect(rx, wy, RAIL_W, wh, 0.20),
            }
        )
    manifest["wedge"] = {
        "orientation": "vertical_bars_left_to_right_0_to_F",
        "rect": rect_obj(wx, wy, ww, wh),
        "bars": bars,
        "white_rails": white_rails,
        "label_rects": label_rects,
    }

    ux, uy, uw, uh = args.uniformity_patch
    fill_rect(levels, ux, uy, uw, uh, 8)
    manifest["uniformity_patch"] = {
        "id": "mid_gray_8_uniformity",
        "level": 8,
        "rect": rect_obj(ux, uy, uw, uh),
        "sample_rect": inset_rect(ux, uy, uw, uh, 0.15),
    }

    dx, dy, dw, dh = args.dither_ramp
    floyd_steinberg_ramp(levels, dx, dy, dw, dh)
    manifest["dither_ramp"] = {
        "id": "fs_dithered_0_to_15_ramp",
        "rect": rect_obj(dx, dy, dw, dh),
        "method": "Floyd-Steinberg using the existing linear nearest_gray16 quantizer",
        "comparison_regions": [
            {
                "id": f"dither_bin_{i:02d}",
                "nominal_level": i,
                "sample_rect": rect_obj(dx + round(i * dw / 16) + 8, dy + 24, max(1, round(dw / 16) - 16), dh - 48),
            }
            for i in range(16)
        ],
    }

    manifest["layout_rationale"] = [
        "Four ArUco markers (DICT_4X4_50) pushed to a 64px corner margin define the coordinate frame with a wide, well-conditioned baseline.",
        "The 16-bar wedge claims the entire interior between the markers (full width, ~744px tall) so each bar has a large pixel population for glare-robust estimation.",
        "v3.2: each gray bar is narrowed and paired with a full-height white rail (level 15) separated by a guard gap, giving a co-located white reference at every bar's own y so the sampler can fit a quadratic-in-y white field that models the centre illumination hump (fixes highlight clamp).",
        "Black/white reference pairs sit near the four interior corners (top and bottom bands) and feed the residual black bilinear lighting field.",
        "Diagnostics (mid-gray uniformity patch, FS dither ramp) live in the top band; the registration checkerboard sits in the bottom band centre.",
        "Bar labels are drawn inside the top inset of each bar, never within the sampled region.",
    ]
    return levels, manifest


def pack_levels(levels: array) -> bytes:
    payload = bytearray(PAYLOAD_SIZE)
    for i in range(0, len(levels), 2):
        payload[i // 2] = ((levels[i] & 0x0F) << 4) | (levels[i + 1] & 0x0F)
    return bytes(payload)


def unpack_payload(payload: bytes) -> array:
    levels = array("B", [0]) * (PANEL_W * PANEL_H)
    out = 0
    for byte in payload:
        levels[out] = (byte >> 4) & 0x0F
        levels[out + 1] = byte & 0x0F
        out += 2
    return levels


def write_g16p(path: Path, payload: bytes) -> None:
    header = HEADER_STRUCT.pack(MAGIC, VERSION, 0, PANEL_W, PANEL_H, len(payload), zlib.crc32(payload) & 0xFFFFFFFF)
    path.write_bytes(header + payload)


def read_g16p(path: Path) -> tuple[bytes, array, dict]:
    data = path.read_bytes()
    magic, version, flags, width, height, payload_len, crc = HEADER_STRUCT.unpack(data[:HEADER_SIZE])
    if magic != MAGIC or version != VERSION or width != PANEL_W or height != PANEL_H:
        raise ValueError(f"Unexpected G16P header: magic={magic!r} version={version} size={width}x{height}")
    payload = data[HEADER_SIZE:]
    if len(payload) != payload_len or len(payload) != PAYLOAD_SIZE:
        raise ValueError(f"Unexpected payload length: header={payload_len} actual={len(payload)}")
    actual_crc = zlib.crc32(payload) & 0xFFFFFFFF
    if actual_crc != crc:
        raise ValueError(f"CRC mismatch: header={crc:08x} actual={actual_crc:08x}")
    return payload, unpack_payload(payload), {"flags": flags, "crc32": f"{crc:08x}"}


def levels_to_uint8(levels: array) -> "np.ndarray":
    arr = np.frombuffer(bytes(bytearray(levels)), dtype=np.uint8).astype(np.uint8)
    return (arr.reshape(PANEL_H, PANEL_W) * 17).astype(np.uint8)


def save_preview(path: Path, levels: array) -> None:
    Image.fromarray(levels_to_uint8(levels)).save(str(path), format="PNG")


def verify(g16p_path: Path, source_payload: bytes, source_levels: array, manifest: dict) -> str:
    decoded_payload, decoded_levels, header_info = read_g16p(g16p_path)
    lines = []
    lines.append("E1003 bench-measurement-3 (Pattern v3 / ArUco) verification")
    lines.append(f"file: {g16p_path}")
    lines.append(f"dimensions: {PANEL_W}x{PANEL_H}")
    lines.append(f"payload_bytes: {len(decoded_payload)}")
    lines.append(f"crc32: {header_info['crc32']}")
    lines.append(f"payload_roundtrip_exact: {decoded_payload == source_payload}")
    lines.append(f"nibble_roundtrip_exact: {decoded_levels == source_levels}")

    # Wedge sample-rect exactness: each rect must contain exactly its level.
    all_bars_ok = True
    distinct_levels = set()
    for bar in manifest["wedge"]["bars"]:
        level = bar["level"]
        r = bar["sample_rect"]
        ok = True
        for yy in range(r["y"], r["y"] + r["h"]):
            row = yy * PANEL_W
            for xx in range(r["x"], r["x"] + r["w"]):
                if decoded_levels[row + xx] != level:
                    ok = False
                    break
            if not ok:
                break
        if ok:
            distinct_levels.add(level)
        all_bars_ok = all_bars_ok and ok
        lines.append(f"wedge_bar_{level:02d}_{level:X}_sample_exact: {ok}")
    lines.append(f"wedge_all_sample_rects_exact: {all_bars_ok}")
    lines.append(f"wedge_distinct_levels: {len(distinct_levels)} (expected 16)")

    # White-rail sample-rect exactness: each rail rect must be solid level 15.
    all_rails_ok = True
    for rail in manifest["wedge"].get("white_rails", []):
        r = rail["sample_rect"]
        ok = True
        for yy in range(r["y"], r["y"] + r["h"]):
            row = yy * PANEL_W
            for xx in range(r["x"], r["x"] + r["w"]):
                if decoded_levels[row + xx] != 15:
                    ok = False
                    break
            if not ok:
                break
        all_rails_ok = all_rails_ok and ok
        lines.append(f"{rail['id']}_sample_exact_white: {ok}")
    lines.append(f"wedge_all_white_rails_exact: {all_rails_ok}")

    # ArUco re-detection on the rendered 8-bit preview.
    preview = levels_to_uint8(decoded_levels)
    detector = aruco_detector()
    corners, ids, _ = detector.detectMarkers(preview)
    found_ids = sorted(int(i) for i in ids.ravel()) if ids is not None else []
    expected_ids = sorted(m["id"] for m in manifest["markers"])
    lines.append(f"aruco_ids_detected: {found_ids} (expected {expected_ids})")
    aruco_ids_ok = found_ids == expected_ids
    aruco_corners_ok = True
    max_corner_err = 0.0
    if ids is not None:
        det = {int(i): c.reshape(4, 2) for i, c in zip(ids.ravel(), corners)}
        for m in manifest["markers"]:
            mid = m["id"]
            if mid not in det:
                aruco_corners_ok = False
                continue
            expected = np.array(m["corners"], dtype=np.float64)
            got = det[mid]
            err = float(np.max(np.linalg.norm(got - expected, axis=1)))
            max_corner_err = max(max_corner_err, err)
            ok = err <= 1.0
            aruco_corners_ok = aruco_corners_ok and ok
            lines.append(f"aruco_marker_{mid}_corner_err_px: {err:.3f} (<=1.0: {ok})")
    lines.append(f"aruco_ids_ok: {aruco_ids_ok}")
    lines.append(f"aruco_corners_ok: {aruco_corners_ok} (max_err={max_corner_err:.3f}px)")

    overall = (
        decoded_payload == source_payload
        and all_bars_ok
        and all_rails_ok
        and len(distinct_levels) == 16
        and aruco_ids_ok
        and aruco_corners_ok
    )
    lines.append("result: PASS" if overall else "result: FAIL")
    return "\n".join(lines) + "\n"


def parse_rect(value: str) -> tuple[int, int, int, int]:
    parts = [int(p) for p in value.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("rect must be x,y,w,h")
    return tuple(parts)  # type: ignore[return-value]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate E1003 Gray16 bench-measurement-3 (Pattern v3) test pattern")
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--g16p-name", default="test-pattern-v3.2.g16p")
    parser.add_argument("--preview-name", default="test-pattern-v3.2-preview.png")
    parser.add_argument("--manifest-name", default="sample-regions-v3.2.json")
    parser.add_argument("--verify-name", default="verify-v3.2.log")
    parser.add_argument("--wedge", type=parse_rect, default=DEFAULT_WEDGE, help="x,y,w,h for 16-bar wedge")
    parser.add_argument("--uniformity-patch", type=parse_rect, default=DEFAULT_UNIFORM, help="x,y,w,h for gray-8 flat patch")
    parser.add_argument("--dither-ramp", type=parse_rect, default=DEFAULT_DITHER, help="x,y,w,h for FS dither ramp")
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    levels, manifest = build_pattern(args)
    payload = pack_levels(levels)

    g16p_path = args.output_dir / args.g16p_name
    preview_path = args.output_dir / args.preview_name
    manifest_path = args.output_dir / args.manifest_name
    verify_path = args.output_dir / args.verify_name

    write_g16p(g16p_path, payload)
    save_preview(preview_path, levels)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    report = verify(g16p_path, payload, levels, manifest)
    verify_path.write_text(report, encoding="utf-8")
    print(report, end="")
    return 0 if "result: PASS" in report else 1


if __name__ == "__main__":
    raise SystemExit(main())
