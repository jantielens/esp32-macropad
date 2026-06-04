#!/usr/bin/env python3
"""Ground-truth validation of the auto-glare detector using a hand-drawn
blue rectangle that marks a known glare-free region across all 16 bars.

For each capture we have a DNG (linear measurement source) and a TIFF of the
same shot with a blue rectangle OUTLINE drawn on it. The interior of that
rectangle is, by human inspection, free of glare/reflection. We:

  1. Decode the DNG -> luminance, detect ArUco markers, warp to the canonical
     1872x1404 frame (identical to sample-v3.py).
  2. Fit the same B/W bilinear flat-field and compute reflectance.
  3. Detect ArUco markers in the TIFF, warp the filled blue rectangle into the
     SAME canonical frame (independent homography -> robust to TIFF resolution
     / crop differences).
  4. Per bar, the GROUND TRUTH reflectance = mean of canonical reflectance over
     (bar sample_rect  AND  blue interior).
  5. Compare against the auto-glare estimate (robust_uniform_value -> refl_good)
     bar-by-bar and as a built LUT.

Output: bluebox-validation.log, bluebox-overlay-img{N}.png (blue interior +
bar rects), and a printed scorecard.

Usage:
  python3 validate-bluebox.py DNG1 DNG2 DNG3   (matching .tiff next to each DNG)
"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
from PIL import Image

import sample_v3_shim as S  # imports sample-v3.py under a safe module name


def fill_blue_rectangle(tiff_path: Path) -> Tuple[np.ndarray, np.ndarray]:
    """Return (interior_mask, rgb) for the TIFF. interior_mask is True inside
    the blue rectangle outline (border pixels themselves excluded)."""
    import cv2

    rgb = np.asarray(Image.open(str(tiff_path)).convert("RGB"))
    R = rgb[..., 0].astype(int)
    G = rgb[..., 1].astype(int)
    B = rgb[..., 2].astype(int)
    blue = (B > 150) & (R < 80) & (G < 80)

    blue_u8 = (blue.astype(np.uint8)) * 255
    # Close small gaps in the hand-drawn outline so it forms a sealed loop.
    k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))
    closed = cv2.morphologyEx(blue_u8, cv2.MORPH_CLOSE, k)

    contours, _ = cv2.findContours(closed, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        raise RuntimeError(f"no blue outline found in {tiff_path.name}")
    biggest = max(contours, key=cv2.contourArea)

    filled = np.zeros(blue.shape, dtype=np.uint8)
    cv2.drawContours(filled, [biggest], -1, 1, thickness=cv2.FILLED)
    interior = filled.astype(bool) & ~blue  # drop the painted border itself
    return interior, rgb


def tiff_luminance(rgb: np.ndarray) -> np.ndarray:
    """Rec.709 luma from a gamma-encoded TIFF (structure only; ArUco-friendly)."""
    r = rgb[..., 0].astype(np.float32) / 255.0
    g = rgb[..., 1].astype(np.float32) / 255.0
    b = rgb[..., 2].astype(np.float32) / 255.0
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def warp_mask_to_canonical(mask: np.ndarray, H: np.ndarray) -> np.ndarray:
    import cv2

    warped = cv2.warpPerspective(
        mask.astype(np.uint8), H, (S.CANONICAL_W, S.CANONICAL_H),
        flags=cv2.INTER_NEAREST,
    )
    return warped.astype(bool)


def reflectance_field(canonical: np.ndarray, regions: dict) -> np.ndarray:
    """Reproduce measure_frame's B/W flat-field -> reflectance exactly.

    Mirrors sample-v3.measure_frame: black field is bilinear from the 4 corner
    refs; white field interpolates the v3.2 white rails (fit_rail_white_field)
    so every bar is normalised against the true-white rail beside it. Falls back
    to a bilinear corner-ref white plane when no rails exist.
    """
    refs = regions["reference_patches"]
    black_pts, white_pts = [], []
    for rp in refs:
        sr = rp["sample_rect"]
        x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
        val = S.robust_patch_value(canonical, sr)
        cx, cy = x + w / 2.0, y + h / 2.0
        (black_pts if rp["role"] == "black" else white_pts).append((cx, cy, val))

    rails = regions.get("wedge", {}).get("white_rails", [])
    n_rails = len(rails)
    if rails:
        rail_xc = np.zeros(n_rails)
        rail_yc = np.zeros(S.RAIL_Y_BANDS)
        rail_grid = np.zeros((S.RAIL_Y_BANDS, n_rails))
    for k, rail in enumerate(rails):
        sr = rail["sample_rect"]
        x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
        rail_xc[k] = x + w / 2.0
        bh = max(1, h // S.RAIL_Y_BANDS)
        for bi in range(S.RAIL_Y_BANDS):
            by = y + bi * bh
            bhh = (h - bi * bh) if bi == S.RAIL_Y_BANDS - 1 else bh
            if bhh <= 0:
                break
            val, *_ = S.robust_uniform_value(canonical[by:by + bhh, x:x + w])
            rail_grid[bi, k] = val
            if k == 0:
                rail_yc[bi] = by + bhh / 2.0

    B = S.fit_bilinear_surface(black_pts, S.CANONICAL_W, S.CANONICAL_H)
    if rails:
        W = S.fit_rail_white_field(rail_xc, rail_yc, rail_grid, S.CANONICAL_W, S.CANONICAL_H)
    else:
        W = S.fit_bilinear_surface(white_pts, S.CANONICAL_W, S.CANONICAL_H)
    return (canonical - B) / np.maximum(W - B, 1e-6)


def main(argv: List[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    args = list(argv)
    regions_path = S.REGIONS_PATH
    if "--regions" in args:
        i = args.index("--regions")
        regions_path = Path(args[i + 1]).resolve()
        del args[i:i + 2]
    dngs = [Path(a).resolve() for a in args]
    regions = S.json.loads(regions_path.read_text())
    canonical_corners = S.canonical_marker_corners(regions)
    bars = regions["wedge"]["bars"]

    log: List[str] = []

    def emit(msg: str) -> None:
        print(msg)
        log.append(msg)

    # Per-frame ground-truth and auto-glare reflectance per bar.
    gt_per_frame: List[List[float]] = []
    gt_cov_per_frame: List[List[float]] = []
    auto_bars_per_frame: List[List[dict]] = []

    for fi, dng in enumerate(dngs, start=1):
        tiff = dng.with_suffix(".tiff")
        if not tiff.exists():
            emit(f"Frame {fi}: MISSING tiff {tiff.name}")
            return 3
        emit(f"Frame {fi}: {dng.name}  +  {tiff.name}")

        # --- DNG: canonical reflectance + auto-glare bars ---
        Y = S.decode_dng_to_luminance(dng)
        markers = S.detect_markers(Y)
        if not all(mid in markers for mid in S.REQUIRED_IDS):
            emit(f"  DNG markers missing: have {sorted(markers)}")
            return 4
        Hd, rms_d, _ = S.solve_and_check_homography(markers, canonical_corners)
        emit(f"  DNG homography reproj rms={rms_d:.2f}px")
        canon = S.warp_to_canonical(Y, Hd)
        refl = reflectance_field(canon, regions)
        frame_meas = S.measure_frame(canon, regions, fi)
        auto_bars_per_frame.append(frame_meas["bars"])

        # --- TIFF: blue rectangle -> canonical mask ---
        interior, rgb = fill_blue_rectangle(tiff)
        Yt = tiff_luminance(rgb)
        tmarkers = S.detect_markers(Yt)
        if not all(mid in tmarkers for mid in S.REQUIRED_IDS):
            emit(f"  TIFF markers missing: have {sorted(tmarkers)}")
            return 5
        Ht, rms_t, _ = S.solve_and_check_homography(tmarkers, canonical_corners)
        emit(f"  TIFF homography reproj rms={rms_t:.2f}px  blue_interior_px={int(interior.sum())}")
        blue_canon = warp_mask_to_canonical(interior, Ht)

        # --- per-bar ground truth from the blue region ---
        gt_vals, gt_cov = [], []
        for bar in bars:
            sr = bar["sample_rect"]
            x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
            bar_mask = np.zeros_like(blue_canon)
            bar_mask[y:y + h, x:x + w] = True
            sel = bar_mask & blue_canon
            n = int(sel.sum())
            total = w * h
            cov = n / total if total else 0.0
            val = float(refl[sel].mean()) if n > 0 else float("nan")
            gt_vals.append(val)
            gt_cov.append(cov)
        gt_per_frame.append(gt_vals)
        gt_cov_per_frame.append(gt_cov)

        # --- per-bar table for this frame ---
        emit("  bar | label |  GT_refl  cov% |  auto_refl  cov% | delta")
        for bi, bar in enumerate(bars):
            a = frame_meas["bars"][bi]
            gtv = gt_vals[bi]
            d = (a["refl_good"] - gtv) if np.isfinite(gtv) else float("nan")
            emit(f"   {bi:2d} |   {bar['label']:>2} | {gtv:+8.4f} {gt_cov[bi]*100:4.0f} "
                 f"| {a['refl_good']:+8.4f} {a['coverage']*100:4.0f} | {d:+.4f}"
                 + ("  REJ" if a["rejected"] else ""))

        # --- overlay: blue interior (cyan) + bar rects (yellow) on canonical ---
        vis = np.clip(canon / max(np.percentile(canon, 99.5), 1e-6), 0, 1)
        vis = (vis * 255).astype(np.uint8)
        ov = np.dstack([vis, vis, vis])
        ov[blue_canon] = (0.5 * ov[blue_canon] + 0.5 * np.array([0, 200, 255])).astype(np.uint8)
        for bar in bars:
            sr = bar["sample_rect"]
            x, y, w, h = sr["x"], sr["y"], sr["w"], sr["h"]
            ov[y:y + 2, x:x + w] = [255, 255, 0]
            ov[y + h - 2:y + h, x:x + w] = [255, 255, 0]
            ov[y:y + h, x:x + 2] = [255, 255, 0]
            ov[y:y + h, x + w - 2:x + w] = [255, 255, 0]
        out_png = S.HERE / f"bluebox-overlay-img{fi}.png"
        Image.fromarray(ov).save(out_png)
        emit(f"  wrote {out_png.name}")

    # --- average across frames (ground truth: simple mean over finite vals) ---
    emit("")
    emit("=== Cross-frame comparison (averaged) ===")
    gt_arr = np.array(gt_per_frame, dtype=np.float64)          # [frames, 16]
    gt_avg = np.nanmean(gt_arr, axis=0)

    # auto-glare averaged identically to sample-v3 (respects rejection).
    auto_avg_list = S.average_frames([
        {"frame_idx": i + 1, "bars": auto_bars_per_frame[i]}
        for i in range(len(auto_bars_per_frame))
    ])
    auto_avg = np.array([r["refl_good_avg"] for r in auto_avg_list], dtype=np.float64)

    emit("bar | label |  GT_avg  | auto_avg | delta   | |delta|")
    for bi, bar in enumerate(bars):
        d = auto_avg[bi] - gt_avg[bi]
        emit(f" {bi:2d} |   {bar['label']:>2} | {gt_avg[bi]:+8.4f} | {auto_avg[bi]:+8.4f} "
             f"| {d:+7.4f} | {abs(d):.4f}")

    # --- LUTs from each source, compared in normalized space ---
    gt_lut, gt_inv, gt_sanity = S.build_luts(
        [{"refl_good_avg": float(v)} for v in gt_avg])
    auto_lut, auto_inv, auto_sanity = S.build_luts(
        [{"refl_good_avg": float(v)} for v in auto_avg])

    # Persist both LUTs as usable JSON files.
    def dump_lut(stem: str, fwd: list, inv: list, avg: np.ndarray, sanity: dict, method: str) -> None:
        fwd_path = S.HERE / f"lut-forward-16-{stem}.json"
        inv_path = S.HERE / f"lut-inverse-256-{stem}.json"
        fwd_path.write_text(S.json.dumps({
            "method": method,
            "source_frames": [d.name for d in dngs],
            "refl_avg": [round(float(v), 6) for v in avg],
            "monotonic": sanity["monotonic"],
            "table": fwd,
        }, indent=2) + "\n")
        inv_path.write_text(S.json.dumps({
            "method": method,
            "comment": "index = target 8-bit gray (0..255); value = source 8-bit gray to send",
            "table": inv,
        }, indent=2) + "\n")
        emit(f"  wrote {fwd_path.name} + {inv_path.name}")

    emit("")
    emit("=== Writing LUT files ===")
    dump_lut("bluebox", gt_lut, gt_inv, gt_avg, gt_sanity,
             "blue-rectangle ground truth (clean pixels only)")
    dump_lut("auto", auto_lut, auto_inv, auto_avg, auto_sanity,
             "auto-glare detector (no blue rectangle)")

    gt_norm = np.array([r["normalized_Y_envelope"] for r in gt_lut])
    auto_norm = np.array([r["normalized_Y_envelope"] for r in auto_lut])
    lut_delta = auto_norm - gt_norm

    emit("")
    emit("=== LUT comparison (normalized 0..1, monotone envelope) ===")
    emit("bar | source8 | GT_norm | auto_norm | delta")
    for bi in range(16):
        emit(f" {bi:2d} |   {bi*17:3d}   | {gt_norm[bi]:.4f}  | {auto_norm[bi]:.4f}   | {lut_delta[bi]:+.4f}")

    def rms(a: np.ndarray) -> float:
        return float(np.sqrt(np.mean(a ** 2)))

    refl_delta = auto_avg - gt_avg
    emit("")
    emit("=== Scorecard: auto-glare vs blue-rectangle ground truth ===")
    emit(f"  reflectance  RMS all bars   : {rms(refl_delta):.4f}")
    emit(f"  reflectance  RMS bars 0..8   : {rms(refl_delta[:9]):.4f}")
    emit(f"  reflectance  max|delta| all  : {np.nanmax(np.abs(refl_delta)):.4f} (bar {int(np.nanargmax(np.abs(refl_delta)))})")
    emit(f"  LUT-norm     RMS all bars    : {rms(lut_delta):.4f}")
    emit(f"  LUT-norm     RMS bars 0..8   : {rms(lut_delta[:9]):.4f}")
    emit(f"  LUT-norm     max|delta| all  : {np.max(np.abs(lut_delta)):.4f} (bar {int(np.argmax(np.abs(lut_delta)))})")
    emit(f"  GT LUT monotonic             : {gt_sanity['monotonic']}")
    emit(f"  auto LUT monotonic           : {auto_sanity['monotonic']}")

    log_path = S.HERE / "bluebox-validation.log"
    log_path.write_text("\n".join(log) + "\n")
    emit(f"\nwrote {log_path.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
