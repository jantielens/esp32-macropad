#!/usr/bin/env python3
"""Bench-measurement-3 sampler — ArUco-aligned, perspective-corrected, flat-fielded.

Pattern v3 replaces v2's brittle blob-based fiducial search with four OpenCV
ArUco markers (DICT_4X4_50, IDs 0-3). Detection is automatic and unambiguous,
so the entire manual-coordinate / dual-orientation-guessing path from
bench-measurement-2 is gone.

Pipeline (per frame):
  1. Decode DNG with rawpy in linear / no-WB / no-tone-curve mode (same
     settings as bench-2): gamma=(1,1), no_auto_bright, raw color space,
     AHD demosaic -> Rec.709 luminance proxy, float32 in [0,1].
  2. Detect ArUco markers (cv2.aruco.ArucoDetector, DICT_4X4_50).
  3. GATE: require all four IDs (0,1,2,3). Missing any -> reject frame.
  4. Build 16 point correspondences (4 markers x 4 corners) photo<->canonical.
  5. Homography via cv2.findHomography(..., cv2.RANSAC).
  6. GATE: RMS reprojection error over the 16 points must be < 2.0 px.
  7. Warp full-res luminance to canonical 1872x1404 (cv2.warpPerspective,
     INTER_LINEAR).
  8. Registration self-test: measure checkerboard interior-edge positions and
     compare to expected. Warn (do NOT reject) if off by > 2px.
  9. Sample 4 black + 4 white reference patches with a glare-robust estimator;
     fit bilinear surfaces B(x,y), W(x,y) (least squares).
 10. Per wedge bar: r = (Y - B) / (W - B); take the glare-robust good-area
     value (low-percentile anchor + dark-side inlier window). Pixels above the
     window are glare; below are shadow / edge bleed / dust.
 11. Glare overlay: paint bright (glare) pixels red and dark outliers blue onto
     the aligned canonical image for visual verification.
 12. Combine surviving frames (mean per bar, excluding coverage-rejected bars).
 13. GATE: per-bar inter-frame std > 0.08 on bars 0-9 (after outlier
     exclusion) -> abort with diagnosis.
 14. Build forward LUT (16, monotonic envelope + endpoint clamp) and inverse
     LUT (256).
 15. GATE: forward LUT must be monotonic; otherwise abort.
 16. Sample uniformity patch and dither ramp for the diagnostic CSV.

Outputs (next to this file):
  - aligned-img{N}.png           8-bit warped canonical frames
  - raw-samples-frame{N}.csv     per-bar per-frame measurements
  - raw-samples-averaged.csv     cross-frame combined
  - lut-forward-16.json, lut-inverse-256.json
  - response-curve.png           plot with bench-1 / bench-2 overlay
  - verify.log                   full run journal with timestamps

Author: Marge / coding agent — bench-measurement-3.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

try:
    import cv2
except Exception as exc:  # pragma: no cover
    print("ERROR: opencv-python (cv2) is required. Install with: python3 -m pip install --user opencv-python", file=sys.stderr)
    print(f"Details: {exc}", file=sys.stderr)
    sys.exit(2)

try:
    import rawpy
except Exception as exc:  # pragma: no cover
    print("ERROR: rawpy is required. Install with: python3 -m pip install --user rawpy", file=sys.stderr)
    print(f"Details: {exc}", file=sys.stderr)
    sys.exit(2)

from PIL import Image
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
DEFAULT_TEMP = REPO_ROOT / "temp"
REGIONS_PATH = HERE / "sample-regions-v3.2.json"

CANONICAL_W = 1872
CANONICAL_H = 1404

ARUCO_DICT_NAME = "DICT_4X4_50"
REQUIRED_IDS = [0, 1, 2, 3]

# Gates
REPROJ_ERR_MAX_PX = 2.0
INTER_FRAME_STD_MAX = 0.08      # bars 0-9 hard gate
CHECKER_WARN_PX = 2.0          # registration self-test soft warning

# Glare-robust good-area estimation.
# Each bar / reference patch is uniform by construction. Two things sit on top
# of the diffuse value:
#   * e-paper HALFTONE texture — high spatial frequency, zero-mean speckle of
#     fully-light / fully-dark particles. This must be AVERAGED IN, not masked.
#   * specular GLARE — low spatial frequency, a large contiguous bright blob
#     that only ever ADDS light. This must be MASKED.
# We separate them by low-pass filtering the patch: a blur kernel large vs the
# halftone pitch but small vs the bar smooths the speckle to the local mean
# while leaving a glare blob elevated. Glare/shadow are then detected on the
# blurred field; the reported value averages the ORIGINAL pixels (texture
# intact) over the non-glare region.
GLARE_BLUR_KERNEL = 25         # low-pass kernel (px) — kills halftone, keeps glare
ANCHOR_PCTL = 25.0             # robust diffuse anchor on the blurred field
LOW_PCTL = 5.0                 # dark-side reference for window scaling
SPREAD_K = 3.0                 # dark-side half-width = K * (ANCHOR - LOW)
GLARE_FRAC = 0.40              # bright cut = anchor + FRAC*(1 - anchor): specular
                               # glare drives pixels a large fraction toward white,
                               # so this ignores mild within-bar sheen gradients
                               # but catches the strong reflection.
TOL_FLOOR = 0.012             # absolute floor for either window half-width
MIN_COVERAGE = 0.30            # min non-glare fraction, else the bar is rejected

# Pattern v3.2: each gray bar is paired with a full-height white rail giving a
# co-located white reference at the bar's own y. The rail sample_rect is
# subdivided into this many vertical bands; each band contributes one (x,y,
# white) control point so the white field can model the centre illumination
# hump (quadratic in y) instead of a bilinear plane.
RAIL_Y_BANDS = 5

LOG_LINES: List[str] = []


def log(msg: str) -> None:
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    line = f"[{stamp}] {msg}"
    print(line, flush=True)
    LOG_LINES.append(line)


def flush_log(path: Path) -> None:
    path.write_text("\n".join(LOG_LINES) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# RAW decode (identical settings to bench-measurement-2)
# ---------------------------------------------------------------------------
def decode_dng_to_luminance(path: Path) -> np.ndarray:
    """Decode a DNG to linear-light Rec.709 luminance, float32 in [0,1]."""
    with rawpy.imread(str(path)) as raw:
        rgb16 = raw.postprocess(
            no_auto_bright=True,
            output_bps=16,
            gamma=(1, 1),
            use_camera_wb=False,
            use_auto_wb=False,
            user_wb=[1.0, 1.0, 1.0, 1.0],
            output_color=rawpy.ColorSpace.raw,
            demosaic_algorithm=rawpy.DemosaicAlgorithm.AHD,
        )
    rgb = rgb16.astype(np.float32) / 65535.0
    Y = 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]
    return Y


# ---------------------------------------------------------------------------
# ArUco detection + homography
# ---------------------------------------------------------------------------
def aruco_detector() -> "cv2.aruco.ArucoDetector":
    d = cv2.aruco.getPredefinedDictionary(getattr(cv2.aruco, ARUCO_DICT_NAME))
    return cv2.aruco.ArucoDetector(d)


def luminance_to_uint8(Y: np.ndarray) -> np.ndarray:
    """8-bit detection image. ArUco's adaptive threshold handles gradients;
    a simple robust normalization to the 99.5th percentile is plenty."""
    hi = float(np.percentile(Y, 99.5))
    hi = max(hi, 1e-6)
    return (np.clip(Y / hi, 0.0, 1.0) * 255.0).astype(np.uint8)


def canonical_marker_corners(regions: dict) -> Dict[int, np.ndarray]:
    """Map marker id -> 4x2 canonical corners (TL,TR,BR,BL)."""
    out: Dict[int, np.ndarray] = {}
    for m in regions["markers"]:
        out[int(m["id"])] = np.array(m["corners"], dtype=np.float64)
    return out


def detect_markers(Y: np.ndarray) -> Dict[int, np.ndarray]:
    """Return {id: 4x2 photo-space corners} for every detected marker."""
    detector = aruco_detector()
    img8 = luminance_to_uint8(Y)
    corners, ids, _ = detector.detectMarkers(img8)
    found: Dict[int, np.ndarray] = {}
    if ids is None:
        return found
    for i, c in zip(ids.ravel(), corners):
        found[int(i)] = c.reshape(4, 2).astype(np.float64)
    return found


def solve_and_check_homography(
    photo: Dict[int, np.ndarray],
    canonical: Dict[int, np.ndarray],
) -> Tuple[Optional[np.ndarray], float, int]:
    """Build 16 correspondences and solve photo->canonical homography.

    Returns (H, rms_reprojection_error_px, n_points). H maps photo pixels into
    canonical 1872x1404 space.
    """
    src = []  # photo
    dst = []  # canonical
    for mid in REQUIRED_IDS:
        src.append(photo[mid])
        dst.append(canonical[mid])
    src_pts = np.vstack(src).astype(np.float64)
    dst_pts = np.vstack(dst).astype(np.float64)
    H, _ = cv2.findHomography(src_pts, dst_pts, cv2.RANSAC, 3.0)
    if H is None:
        return None, float("inf"), len(src_pts)
    proj = cv2.perspectiveTransform(src_pts.reshape(-1, 1, 2), H).reshape(-1, 2)
    err = np.linalg.norm(proj - dst_pts, axis=1)
    rms = float(np.sqrt(np.mean(err ** 2)))
    return H, rms, len(src_pts)


def warp_to_canonical(Y: np.ndarray, H: np.ndarray) -> np.ndarray:
    return cv2.warpPerspective(
        Y.astype(np.float32), H, (CANONICAL_W, CANONICAL_H), flags=cv2.INTER_LINEAR
    )


# ---------------------------------------------------------------------------
# Registration self-test
# ---------------------------------------------------------------------------
def checkerboard_self_test(canonical: np.ndarray, regions: dict) -> Tuple[float, str]:
    """Measure interior-edge positions of the warped checkerboard.

    Returns (max_edge_delta_px, summary). Soft diagnostic: a large delta means
    the homography is imperfect but we do not reject the frame here.
    """
    cb = regions.get("checkerboard")
    if not cb:
        return 0.0, "no checkerboard region in manifest"
    r = cb["rect"]
    x, y, w, h = r["x"], r["y"], r["w"], r["h"]
    patch = canonical[y:y + h, x:x + w]
    if patch.size == 0:
        return float("inf"), "checkerboard region empty after warp"

    # Mid level (between black and white) for edge detection.
    lo, hi = float(patch.min()), float(patch.max())
    mid = 0.5 * (lo + hi)

    # Interior vertical edges: scan column profile of the top row band.
    cell = cb["cell_size"]
    expected_v = [vx - x for vx in cb["vertical_edges_x"][1:-1]]  # interior only
    expected_h = [hy - y for hy in cb["horizontal_edges_y"][1:-1]]

    def edge_positions(profile: np.ndarray) -> List[float]:
        crossings = []
        for i in range(1, len(profile)):
            a, b = profile[i - 1], profile[i]
            if (a - mid) * (b - mid) < 0:
                # linear interpolation of zero crossing
                t = (mid - a) / (b - a) if (b - a) != 0 else 0.0
                crossings.append((i - 1) + t)
        return crossings

    # Vertical edges from a horizontal scanline through the first row of cells.
    row_band = patch[cell // 2: cell // 2 + 1, :].mean(axis=0)
    col_band = patch[:, cell // 2: cell // 2 + 1].mean(axis=1)
    v_cross = edge_positions(row_band)
    h_cross = edge_positions(col_band)

    def match_delta(expected: List[float], crossings: List[float]) -> float:
        if not crossings:
            return float("inf")
        d = 0.0
        for e in expected:
            nearest = min(crossings, key=lambda c: abs(c - e))
            d = max(d, abs(nearest - e))
        return d

    dv = match_delta(expected_v, v_cross)
    dh = match_delta(expected_h, h_cross)
    delta = max(dv, dh)
    return delta, f"v_delta={dv:.2f}px h_delta={dh:.2f}px"


# ---------------------------------------------------------------------------
# Flat-fielding from reference patches
# ---------------------------------------------------------------------------
def _odd_clamp(kernel: int, dim: int) -> int:
    """Largest odd value <= min(kernel, dim) (Gaussian kernels must be odd)."""
    k = min(kernel, dim)
    return k if k % 2 == 1 else k - 1


def robust_uniform_value(
    patch: np.ndarray,
) -> Tuple[float, np.ndarray, np.ndarray, np.ndarray, float]:
    """Glare-robust diffuse value of a uniform patch.

    The patch is a single nominal level by construction, carrying two additive
    components on top of the diffuse value: high-frequency e-paper halftone
    texture (zero-mean speckle — keep) and low-frequency specular glare (a big
    contiguous bright blob — mask).

    We low-pass the patch so the halftone averages out but a glare blob stays
    elevated, then detect glare/shadow on the BLURRED field. The bright cut is
    a large fraction of the headroom to white (specular glare reflects the
    light source, driving pixels toward white regardless of the bar level), so
    mild within-bar sheen gradients are kept while the strong reflection is
    masked. The dark side uses a modest window around the anchor.
    The returned value averages the ORIGINAL pixels over the non-glare region,
    preserving the halftone average.

    Returns (value, inlier_mask, bright_mask, dark_mask, coverage_fraction).
    bright_mask flags glare (above the window); dark_mask flags shadow / edge
    bleed / dust (below the window).
    """
    if patch.size == 0:
        empty = np.zeros_like(patch, dtype=bool)
        return 0.0, empty, empty, empty, 0.0

    # Low-pass to separate low-frequency glare from high-frequency halftone.
    patch32 = patch.astype(np.float32)
    ky = max(3, _odd_clamp(GLARE_BLUR_KERNEL, patch.shape[0]))
    kx = max(3, _odd_clamp(GLARE_BLUR_KERNEL, patch.shape[1]))
    blurred = cv2.GaussianBlur(patch32, (kx, ky), 0).astype(np.float64)

    anchor = float(np.percentile(blurred, ANCHOR_PCTL))
    low = float(np.percentile(blurred, LOW_PCTL))
    spread = max(TOL_FLOOR, SPREAD_K * (anchor - low))
    lo = anchor - spread                                   # dark side
    hi = anchor + max(TOL_FLOOR, GLARE_FRAC * (1.0 - anchor))  # specular cut
    inlier = (blurred >= lo) & (blurred <= hi)
    bright = blurred > hi
    dark = blurred < lo
    coverage = float(inlier.mean())
    # Average the ORIGINAL pixels (texture intact) over the non-glare region.
    value = float(patch[inlier].mean()) if inlier.any() else anchor
    return value, inlier, bright, dark, coverage


def robust_patch_value(canonical: np.ndarray, rect: dict) -> float:
    """Glare-robust raw value for a reference patch (used for the B/W fit)."""
    x, y, w, h = rect["x"], rect["y"], rect["w"], rect["h"]
    value, *_ = robust_uniform_value(canonical[y:y + h, x:x + w])
    return value


def fit_bilinear_surface(points: List[Tuple[float, float, float]], W: int, H: int) -> np.ndarray:
    """Fit z = a*x + b*y + c*x*y + d to (x,y,z) samples; return WxH grid."""
    A = np.array([[x, y, x * y, 1.0] for x, y, _ in points], dtype=np.float64)
    z = np.array([p[2] for p in points], dtype=np.float64)
    coeffs, *_ = np.linalg.lstsq(A, z, rcond=None)
    a, b, c, d = coeffs
    us, vs = np.meshgrid(np.arange(W), np.arange(H))
    return (a * us + b * vs + c * us * vs + d).astype(np.float32)


def fit_quadratic_y_surface(points: List[Tuple[float, float, float]], W: int, H: int) -> np.ndarray:
    """Fit z = a + b*x + c*y + d*x*y + e*y^2 to (x,y,z) samples; return WxH grid.

    The extra y^2 term lets the surface model a centre-peaked vertical
    illumination hump that a plain bilinear plane cannot represent.

    DEPRECATED for the white field: a global low-order surface that is only
    linear in x cannot follow the *horizontal* illumination hump revealed by
    the level-15 rails (which vary ~46% across the wedge, peaking near centre
    and falling at both ends). That over-estimated white at the wedge ends and
    produced a spurious non-monotonic "rollover" at bars D-F. Use
    fit_rail_white_field instead, which interpolates the actual rail values so
    every bar is normalised against the true-white reference beside it. Kept
    only as the no-rail fallback path.
    """
    A = np.array([[1.0, x, y, x * y, y * y] for x, y, _ in points], dtype=np.float64)
    z = np.array([p[2] for p in points], dtype=np.float64)
    coeffs, *_ = np.linalg.lstsq(A, z, rcond=None)
    a, b, c, d, e = coeffs
    us, vs = np.meshgrid(np.arange(W), np.arange(H))
    return (a + b * us + c * vs + d * us * vs + e * vs * vs).astype(np.float32)


def fit_rail_white_field(rail_xc: np.ndarray, rail_yc: np.ndarray,
                         rail_grid: np.ndarray, W: int, H: int) -> np.ndarray:
    """White field by separable linear interpolation through the rail samples.

    rail_xc : (K,) ascending x-centres of the K white rails.
    rail_yc : (M,) ascending y-centres of the M vertical bands per rail.
    rail_grid : (M, K) robust white value at each (band, rail).

    The field passes through every measured rail value and is endpoint-clamped
    outside the rail grid, so each bar is normalised against the true-white
    reference physically beside it (bar 0 -> rail 0 on its left edge; bar F
    bracketed by rails E and F). This models BOTH the horizontal and vertical
    illumination hump directly from data instead of fitting a low-order surface,
    removing the spurious highlight rollover that the linear-in-x fit produced.
    """
    xs = np.arange(W)
    # Interpolate across x within each band row -> (M, W), clamped at the ends.
    rows = np.empty((rail_yc.shape[0], W), dtype=np.float64)
    for m in range(rail_yc.shape[0]):
        rows[m] = np.interp(xs, rail_xc, rail_grid[m])
    # Interpolate across y for every column at once via a fractional band index.
    ys = np.arange(H)
    fidx = np.interp(ys, rail_yc, np.arange(rail_yc.shape[0]))  # (H,), clamped
    lo = np.floor(fidx).astype(int)
    hi = np.minimum(lo + 1, rail_yc.shape[0] - 1)
    wgt = (fidx - lo)[:, None]
    return (rows[lo] * (1.0 - wgt) + rows[hi] * wgt).astype(np.float32)


# ---------------------------------------------------------------------------
# Per-frame measurement
# ---------------------------------------------------------------------------
def measure_frame(canonical: np.ndarray, regions: dict, frame_idx: int) -> dict:
    refs = regions["reference_patches"]
    black_pts, white_pts, ref_records = [], [], []
    # Glare overlay accumulators (canonical-sized boolean masks).
    bright_canvas = np.zeros((CANONICAL_H, CANONICAL_W), dtype=bool)
    dark_canvas = np.zeros((CANONICAL_H, CANONICAL_W), dtype=bool)

    for rp in refs:
        sr = rp["sample_rect"]
        x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
        cx = x + w / 2.0
        cy = y + h / 2.0
        y_val, _inl, r_bright, r_dark, _cov = robust_uniform_value(canonical[y:y + h, x:x + w])
        bright_canvas[y:y + h, x:x + w] |= r_bright
        dark_canvas[y:y + h, x:x + w] |= r_dark
        ref_records.append({"id": rp["id"], "role": rp["role"], "y_mean": y_val})
        (black_pts if rp["role"] == "black" else white_pts).append((cx, cy, y_val))

    # Pattern v3.2 white rails: co-located white references at every bar's y.
    # Subdivide each rail vertically into bands and build a (band x rail) grid of
    # robust white values, then interpolate it (fit_rail_white_field) so every
    # bar is normalised against the true-white rail beside it. This follows both
    # the vertical AND horizontal illumination hump directly from the rails.
    # Without rails (v3.1 regions) fall back to the corner-ref bilinear plane.
    rails = regions.get("wedge", {}).get("white_rails", [])
    n_rails = len(rails)
    if rails:
        rail_xc = np.zeros(n_rails)
        rail_yc = np.zeros(RAIL_Y_BANDS)
        rail_grid = np.zeros((RAIL_Y_BANDS, n_rails))
    for k, rail in enumerate(rails):
        sr = rail["sample_rect"]
        x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
        rail_xc[k] = x + w / 2.0
        bh = max(1, h // RAIL_Y_BANDS)
        for bi in range(RAIL_Y_BANDS):
            by = y + bi * bh
            bhh = (h - bi * bh) if bi == RAIL_Y_BANDS - 1 else bh
            if bhh <= 0:
                break
            w_val, _i, r_bright, r_dark, _c = robust_uniform_value(canonical[by:by + bhh, x:x + w])
            bright_canvas[by:by + bhh, x:x + w] |= r_bright
            dark_canvas[by:by + bhh, x:x + w] |= r_dark
            rail_grid[bi, k] = w_val
            if k == 0:
                rail_yc[bi] = by + bhh / 2.0

    B_field = fit_bilinear_surface(black_pts, CANONICAL_W, CANONICAL_H)
    if rails:
        W_field = fit_rail_white_field(rail_xc, rail_yc, rail_grid, CANONICAL_W, CANONICAL_H)
    else:
        W_field = fit_bilinear_surface(white_pts, CANONICAL_W, CANONICAL_H)
    span_field = W_field - B_field
    refl = (canonical - B_field) / np.maximum(span_field, 1e-6)

    bars_out, quality_rows = [], []
    for bar in regions["wedge"]["bars"]:
        sr = bar["sample_rect"]
        x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
        patch_refl = refl[y:y + h, x:x + w]

        refl_good, inlier, bright, dark, coverage = robust_uniform_value(patch_refl)
        bright_canvas[y:y + h, x:x + w] |= bright
        dark_canvas[y:y + h, x:x + w] |= dark

        rejected = coverage < MIN_COVERAGE
        reason = "" if not rejected else f"low_coverage({coverage:.2f}<{MIN_COVERAGE:.2f})"

        bars_out.append({
            "bar_index": bar["level"],
            "label": bar["label"],
            "y_raw_mean": float(canonical[y:y + h, x:x + w].mean()),
            "y_raw_median": float(np.median(canonical[y:y + h, x:x + w])),
            "refl_good": refl_good,
            "refl_mean": float(patch_refl.mean()),
            "refl_median": float(np.median(patch_refl)),
            "refl_std": float(patch_refl.std()),
            "coverage": coverage,
            "rejected": rejected,
            "reason": reason,
        })
        quality_rows.append({
            "frame": frame_idx,
            "region": bar["id"],
            "refl_good": round(refl_good, 6),
            "refl_mean": round(float(patch_refl.mean()), 6),
            "refl_std": round(float(patch_refl.std()), 4),
            "coverage": round(coverage, 4),
            "bright_px": int(bright.sum()),
            "dark_px": int(dark.sum()),
            "rejected": rejected,
            "reason": reason,
        })

    up = regions["uniformity_patch"]["sample_rect"]
    up_refl = refl[up["y"]:up["y"] + up["h"], up["x"]:up["x"] + up["w"]]
    uniformity = {
        "refl_mean": float(up_refl.mean()),
        "refl_median": float(np.median(up_refl)),
        "refl_std": float(up_refl.std()),
        "refl_min": float(up_refl.min()),
        "refl_max": float(up_refl.max()),
    }

    dither = []
    for bin_def in regions["dither_ramp"]["comparison_regions"]:
        sr = bin_def["sample_rect"]
        bin_refl = refl[sr["y"]:sr["y"] + sr["h"], sr["x"]:sr["x"] + sr["w"]]
        dither.append({
            "id": bin_def["id"],
            "nominal_level": bin_def["nominal_level"],
            "refl_mean": float(bin_refl.mean()),
            "refl_std": float(bin_refl.std()),
        })

    return {
        "frame_idx": frame_idx,
        "bars": bars_out,
        "ref_patches": ref_records,
        "uniformity": uniformity,
        "dither": dither,
        "quality_rows": quality_rows,
        "bright_canvas": bright_canvas,
        "dark_canvas": dark_canvas,
        "B_field_mean": float(B_field.mean()),
        "W_field_mean": float(W_field.mean()),
    }


# ---------------------------------------------------------------------------
# Output writers
# ---------------------------------------------------------------------------
def write_frame_csv(path: Path, frame: dict) -> None:
    cols = ["bar_index", "label", "y_raw_mean", "y_raw_median",
            "refl_good", "refl_mean", "refl_median", "refl_std",
            "coverage", "rejected", "reason"]
    with path.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(cols)
        for b in frame["bars"]:
            w.writerow([b[c] for c in cols])


def average_frames(frames: List[dict]) -> List[dict]:
    avg = []
    for i in range(16):
        vals, used, rejected = [], [], []
        for f in frames:
            b = f["bars"][i]
            if b["rejected"]:
                rejected.append(f["frame_idx"])
            else:
                vals.append(b["refl_good"])
                used.append(f["frame_idx"])
        if not vals:
            vals = [f["bars"][i]["refl_good"] for f in frames]
            used = [f["frame_idx"] for f in frames]
            rejected = []
            note = "ALL_FRAMES_REJECTED_FALLBACK"
        else:
            note = ""
        avg.append({
            "bar_index": i,
            "refl_good_avg": float(np.mean(vals)),
            "refl_std_across_frames": float(np.std(vals)) if len(vals) > 1 else 0.0,
            "n_frames_used": len(used),
            "frames_used": ",".join(str(x) for x in used),
            "frames_rejected": ",".join(str(x) for x in rejected),
            "note": note,
        })
    return avg


def write_avg_csv(path: Path, avg: List[dict]) -> None:
    cols = ["bar_index", "refl_good_avg", "refl_std_across_frames",
            "n_frames_used", "frames_used", "frames_rejected", "note"]
    with path.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(cols)
        for r in avg:
            w.writerow([r[c] for c in cols])


def build_luts(avg: List[dict]) -> Tuple[List[dict], List[int], dict]:
    """Forward (16) + inverse (256) LUT with monotonic envelope + endpoint clamp."""
    refl = np.array([r["refl_good_avg"] for r in avg], dtype=np.float64)
    r0, r15 = float(refl[0]), float(refl[-1])
    norm = (refl - r0) / max(r15 - r0, 1e-9)
    norm = np.clip(norm, 0.0, 1.0)

    # Monotonic envelope (running max) to remove tiny non-monotonic wobble.
    env = np.maximum.accumulate(norm)
    env[0] = 0.0
    env[-1] = 1.0

    nominal = np.arange(16) / 15.0
    fwd = []
    for i, n in enumerate(env):
        fwd.append({
            "bar_index": i,
            "source_gray_8bit": i * 17,
            "normalized_Y_measured": round(float(norm[i]), 6),
            "normalized_Y_envelope": round(float(n), 6),
            "nominal_linear": round(float(nominal[i]), 6),
            "delta": round(float(n - nominal[i]), 6),
        })

    diffs = np.diff(env)
    monotonic = bool(np.all(diffs >= -1e-9))
    raw_diffs = np.diff(norm)
    sanity = {
        "monotonic": monotonic,
        "max_negative_step_envelope": float(diffs.min()) if len(diffs) else 0.0,
        "max_negative_step_raw": float(raw_diffs.min()) if len(raw_diffs) else 0.0,
        "endpoints": [float(env[0]), float(env[-1])],
    }

    src_vals = np.arange(16) * 17
    inverse: List[int] = []
    for g in range(256):
        target = g / 255.0
        idx = int(np.argmin(np.abs(env - target)))
        inverse.append(int(src_vals[idx]))

    return fwd, inverse, sanity


def write_response_curve(path: Path, fwd: List[dict]) -> None:
    x = np.arange(16)
    nominal = x / 15.0
    v3 = [e["normalized_Y_envelope"] for e in fwd]
    plt.figure(figsize=(8, 6))
    plt.plot(x, nominal, "k--", label="nominal linear", linewidth=1)
    plt.plot(x, v3, "D-", color="#cc4444", label="measured response", linewidth=2, markersize=5)
    plt.xlabel("bar index (source gray level 0-15)")
    plt.ylabel("normalized measured luminance")
    plt.title("Panel tone response — measured vs linear")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(str(path), dpi=110)
    plt.close()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def run(dng_files: List[Path], regions_path: Path = REGIONS_PATH) -> int:
    log("bench-measurement-3 sampler starting (ArUco-aligned)")
    log(f"cv2 version: {cv2.__version__}  rawpy version: {rawpy.__version__}")

    regions = json.loads(regions_path.read_text())
    assert regions["dimensions"]["width"] == CANONICAL_W
    assert regions["dimensions"]["height"] == CANONICAL_H
    canon_corners = canonical_marker_corners(regions)
    rails = regions.get("wedge", {}).get("white_rails", [])
    log(f"regions: {regions_path.name}  white_field={'quadratic-y (rails)' if rails else 'bilinear (corners)'}")

    frames: List[dict] = []

    for idx, dng in enumerate(dng_files, start=1):
        log(f"Frame {idx}: decoding {dng.name}")
        if not dng.exists():
            log(f"  REJECT frame {idx}: file not found ({dng})")
            continue
        Y = decode_dng_to_luminance(dng)
        log(f"  decoded shape={Y.shape} range=[{Y.min():.4f},{Y.max():.4f}]")

        # ArUco detection + gate on all 4 IDs.
        photo = detect_markers(Y)
        found = sorted(photo.keys())
        log(f"  aruco ids detected: {found}")
        missing = [i for i in REQUIRED_IDS if i not in photo]
        if missing:
            log(f"  REJECT frame {idx}: missing marker IDs {missing}")
            continue

        H, rms, npts = solve_and_check_homography(photo, canon_corners)
        log(f"  homography: {npts} pts, reprojection RMS = {rms:.3f}px")
        if H is None or rms > REPROJ_ERR_MAX_PX:
            log(f"  REJECT frame {idx}: reprojection error {rms:.3f}px > {REPROJ_ERR_MAX_PX}px")
            continue

        canonical = warp_to_canonical(Y, H)

        # Registration self-test (soft).
        delta, cb_summary = checkerboard_self_test(canonical, regions)
        if delta > CHECKER_WARN_PX:
            log(f"  WARNING: registration checkerboard off by {delta:.2f}px ({cb_summary}) — alignment may be imperfect")
        else:
            log(f"  registration self-test OK: {cb_summary}")

        # Measure first so the glare overlay can use the per-pixel masks.
        frame = measure_frame(canonical, regions, idx)
        log(f"  black ref Y mean = {frame['B_field_mean']:.4f}  white ref Y mean = {frame['W_field_mean']:.4f}")
        for b in frame["bars"]:
            tag = "REJECTED " + b["reason"] if b["rejected"] else "ok"
            log(f"  bar {b['bar_index']:>2} ({b['label']}): raw={b['y_raw_mean']:.4f} "
                f"refl_good={b['refl_good']:+.4f} cov={b['coverage']:.2f} "
                f"std={b['refl_std']:.4f} [{tag}]")
        frames.append(frame)
        write_frame_csv(HERE / f"raw-samples-frame{idx}.csv", frame)

        # Aligned preview with glare overlay: bright (glare) -> red,
        # dark (shadow/edge/dust) -> blue, sample-rect outlines kept.
        aligned_8 = (np.clip(canonical / max(float(canonical.max()), 1e-6), 0, 1) * 255).astype(np.uint8)
        overlay = np.stack([aligned_8, aligned_8, aligned_8], axis=-1)
        bright = frame["bright_canvas"]
        dark = frame["dark_canvas"]
        overlay[bright] = [255, 40, 40]
        overlay[dark] = [40, 80, 255]
        for bar in regions["wedge"]["bars"]:
            sr = bar["sample_rect"]
            x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
            overlay[y, x:x + w] = [255, 255, 0]
            overlay[y + h - 1, x:x + w] = [255, 255, 0]
            overlay[y:y + h, x] = [255, 255, 0]
            overlay[y:y + h, x + w - 1] = [255, 255, 0]
        for rp in regions["reference_patches"]:
            sr = rp["sample_rect"]
            x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
            overlay[y, x:x + w] = [0, 255, 0]
            overlay[y + h - 1, x:x + w] = [0, 255, 0]
            overlay[y:y + h, x] = [0, 255, 0]
            overlay[y:y + h, x + w - 1] = [0, 255, 0]
        for rail in regions.get("wedge", {}).get("white_rails", []):
            sr = rail["sample_rect"]
            x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
            overlay[y, x:x + w] = [0, 220, 220]
            overlay[y + h - 1, x:x + w] = [0, 220, 220]
            overlay[y:y + h, x] = [0, 220, 220]
            overlay[y:y + h, x + w - 1] = [0, 220, 220]
        Image.fromarray(overlay).save(str(HERE / f"aligned-img{idx}.png"))

    if not frames:
        log("ABORT: no frames survived the marker/reprojection gates — no LUT produced.")
        flush_log(HERE / "verify.log")
        return 1

    log(f"{len(frames)} frame(s) survived gates.")

    avg = average_frames(frames)
    write_avg_csv(HERE / "raw-samples-averaged.csv", avg)
    log("Averaged per bar across frames (using glare-rejection list):")
    for r in avg:
        log(f"  bar {r['bar_index']:>2}: refl={r['refl_good_avg']:+.4f} "
            f"std_across={r['refl_std_across_frames']:.4f} "
            f"used={r['frames_used']} rejected={r['frames_rejected']} {r['note']}")

    # GATE: inter-frame std on bars 0-9.
    noisy = [(r["bar_index"], r["refl_std_across_frames"]) for r in avg
             if r["bar_index"] <= 9 and r["refl_std_across_frames"] > INTER_FRAME_STD_MAX]
    if noisy:
        for bi, s in noisy:
            log(f"  bar {bi}: inter-frame std {s:.4f} > {INTER_FRAME_STD_MAX}")
        log("ABORT: inter-frame std exceeds gate on shadow/midtone bars — diagnosis needed, no LUT produced.")
        flush_log(HERE / "verify.log")
        return 1

    fwd, inverse, sanity = build_luts(avg)
    log(f"Forward LUT: monotonic={sanity['monotonic']} "
        f"max_neg_step_raw={sanity['max_negative_step_raw']:+.5f} "
        f"endpoints={sanity['endpoints']}")

    # GATE: monotonicity.
    if not sanity["monotonic"]:
        log("ABORT: forward LUT is not monotonic after envelope correction — no LUT produced.")
        flush_log(HERE / "verify.log")
        return 1

    method = (
        "rawpy linear demosaic (no WB, no tone curve, gamma=(1,1)) -> Rec.709 "
        "luminance proxy in raw color space -> ArUco DICT_4X4_50 detection "
        "(IDs 0-3) -> cv2.findHomography RANSAC from 16 marker corners to "
        "1872x1404 canonical -> reprojection-error gate (<2px) -> "
        "cv2.warpPerspective -> per-pixel reflectance r=(Y-B)/(W-B) with "
        "B,W bilinear surfaces fit to 4 in-frame black/white reference pairs "
        "-> glare-robust good-area mean per bar sample rect (low-percentile "
        "anchor + dark-side window, bright glare and dark outliers masked) -> "
        "coverage-rejected cross-frame mean -> "
        "monotonic envelope, renormalized so bar_00=0, bar_15=1."
    )
    forward_doc = {
        "panel": "reTerminal E1003 (IT8951, 4bpp, GC16)",
        "measurement": "bench-measurement-3 (Pattern v3, ArUco-aligned)",
        "method": method,
        "source_frames": [p.name for p in dng_files],
        "frames_used": len(frames),
        "sanity": sanity,
        "entries": fwd,
    }
    (HERE / "lut-forward-16.json").write_text(json.dumps(forward_doc, indent=2) + "\n")

    inverse_doc = {
        "panel": "reTerminal E1003 (IT8951, 4bpp, GC16)",
        "measurement": "bench-measurement-3",
        "description": (
            "256-entry LUT. Index = desired perceptual gray in 0..255 "
            "(target normalized luminance = idx/255). Value = source 8-bit "
            "gray (multiple of 17) whose measured luminance is closest."
        ),
        "lut": inverse,
    }
    (HERE / "lut-inverse-256.json").write_text(json.dumps(inverse_doc, indent=2) + "\n")

    write_response_curve(HERE / "response-curve.png", fwd)
    log("Wrote lut-forward-16.json, lut-inverse-256.json, response-curve.png")
    log("bench-measurement-3 sampler complete: LUT produced.")
    flush_log(HERE / "verify.log")
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="bench-measurement-3 ArUco sampler")
    parser.add_argument("dng", nargs="*", type=Path,
                        help="DNG frame paths (default: all *.dng / *.DNG in ../../temp/)")
    parser.add_argument("--regions", type=Path, default=REGIONS_PATH,
                        help="sample-regions JSON (default: sample-regions-v3.2.json, "
                             "the rail-based white-field manifest)")
    args = parser.parse_args(argv)

    if args.dng:
        dng_files = list(args.dng)
    else:
        dng_files = sorted(DEFAULT_TEMP.glob("*.dng")) + sorted(DEFAULT_TEMP.glob("*.DNG"))
        if not dng_files:
            log(f"No DNG files supplied and none found in {DEFAULT_TEMP}.")
            log("Usage: python3 sample-v3.py frame1.dng frame2.dng frame3.dng ...")
            return 2
    log(f"Input frames ({len(dng_files)}): " + ", ".join(p.name for p in dng_files))
    return run(dng_files, args.regions)


if __name__ == "__main__":
    raise SystemExit(main())
