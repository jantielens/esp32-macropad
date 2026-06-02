#!/usr/bin/env python3
"""Prepare an image for the reTerminal E1003 Gray16 e-paper path.

Outputs are written next to the source image. Three variants are produced so a
spike can compare them on the panel:

* ``.g16p`` / ``.e1003.jpg`` (linear): raw BT.709 luma, no tone shaping.
* ``.pre.g16p`` / ``.pre.e1003.jpg`` (preprocessed): the gamma + highlights tone
  curve over a fixed 0-255 range (no auto-stretch), then the same mapping as
  linear.
* ``.cal.g16p`` / ``.cal.e1003.jpg`` (calibrated): a Lightroom-style tone curve
  (auto black/white points + gamma + highlights) so image content spans all 16
  panel levels.

IT8951 Gray16 is monotonic by design (nibble 0x0 = black, 0xF = white), so every
path maps tone-curved luma straight to nibble value with Floyd-Steinberg error
diffusion. ``--gamma`` and ``--highlights`` tune the ``.pre`` and ``.cal``
outputs; ``--bars`` emits a 16-level gradient for calibration. When a container
SAS URL is provided, all generated files are uploaded.
"""

from __future__ import annotations

import os
import struct
import sys
import urllib.error
import urllib.parse
import urllib.request
import zlib
from array import array
from math import ceil

try:
    from PIL import Image
except Exception as exc:  # pragma: no cover
    print(
        "ERROR: Python dependency 'Pillow' is required for image conversion.\n"
        "\n"
        "Install it with:\n"
        "  python3 -m pip install --user pillow\n"
        "\n"
        f"Details: {exc}",
        file=sys.stderr,
    )
    sys.exit(2)


PANEL_W = 1872
PANEL_H = 1404
MAGIC = b"G16P"
VERSION = 1

# IT8951 Gray16 is monotonic by design: nibble 0x0 = black, 0xF = white, with
# brightness increasing across the range. The calibrated path therefore maps
# tone-curved luma straight to nibble value (no level reordering) and relies on
# the tone curve below for contrast and midtone shaping.

# Lightroom-style tone curve applied before quantization. Black/white points
# are taken from source-luma percentiles (auto-stretch) so image content spans
# the full level range; gamma adjusts midtone distribution. CAL_GAMMA and
# CAL_HIGHLIGHTS are the defaults when --gamma / --highlights are not given.
#
# Highlights uses an anchored bump curve (zero at both endpoints, peak in the
# upper tones) so negative values pull the lights down while keeping black at 0
# and white pinned at pure white. HIGHLIGHT_BUMP_EXP sets where the bump peaks
# (peak at exp/(exp+1); 6.0 -> ~0.86 of the range) and how tightly it stays in
# the lights without disturbing the midtones.
CAL_BLACK_PCT = 0.5
CAL_WHITE_PCT = 99.5
CAL_GAMMA = 0.8
CAL_HIGHLIGHTS = -0.1
HIGHLIGHT_BUMP_EXP = 6.0
_HIGHLIGHT_BUMP_PEAK = (
    (HIGHLIGHT_BUMP_EXP / (HIGHLIGHT_BUMP_EXP + 1.0)) ** HIGHLIGHT_BUMP_EXP
) / (HIGHLIGHT_BUMP_EXP + 1.0)


def _usage() -> int:
    print(
        f"Usage: {os.path.basename(sys.argv[0])} [--gamma N] [--highlights N] <source-image> [container-sas-url]\n"
        f"       {os.path.basename(sys.argv[0])} [--gamma N] [--highlights N] --bars [container-sas-url]\n"
        f"\n"
        f"  --gamma N        Midtone tone-curve gamma for the calibrated (.cal) output.\n"
        f"                   >1.0 brightens midtones, <1.0 darkens. Default {CAL_GAMMA}.\n"
        f"  --highlights N   Shift the lights (-1.0..1.0) while keeping black and pure\n"
        f"                   white pinned. Negative darkens the highlights, positive\n"
        f"                   brightens. Default {CAL_HIGHLIGHTS}.",
        file=sys.stderr,
    )
    return 2


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


def _load_rgb_panel(path: str) -> Image.Image:
    with Image.open(path) as img:
        if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
            rgba = img.convert("RGBA")
            background = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
            img = Image.alpha_composite(background, rgba)
        else:
            img = img.convert("RGB")

        if img.size != (PANEL_W, PANEL_H):
            scale = max(PANEL_W / img.width, PANEL_H / img.height)
            resized_w = max(PANEL_W, ceil(img.width * scale))
            resized_h = max(PANEL_H, ceil(img.height * scale))
            img = img.resize((resized_w, resized_h), Image.Resampling.NEAREST)

            left = (resized_w - PANEL_W) // 2
            top = (resized_h - PANEL_H) // 2
            img = img.crop((left, top, left + PANEL_W, top + PANEL_H))

        return img.convert("RGB")


def _generate_bars_rgb() -> Image.Image:
    img = Image.new("RGB", (PANEL_W, PANEL_H), (255, 255, 255))
    pixels = img.load()
    bar_w = PANEL_W // 16
    for y in range(PANEL_H):
        for x in range(PANEL_W):
            gray = min(x // bar_w, 15) * 17
            pixels[x, y] = (gray, gray, gray)
    return img


def _dither_gray16(rgb: Image.Image) -> bytearray:
    pixels = rgb.tobytes()
    pixel_count = PANEL_W * PANEL_H
    gray = array("h", [0]) * pixel_count

    for pixel_index in range(pixel_count):
        source_index = pixel_index * 3
        gray[pixel_index] = _luma_bt709(
            pixels[source_index],
            pixels[source_index + 1],
            pixels[source_index + 2],
        )

    packed = bytearray(pixel_count // 2)
    for y in range(PANEL_H):
        row = y * PANEL_W
        for x in range(PANEL_W):
            idx = row + x
            old = _clamp_u8(gray[idx])
            q = _nearest_gray16(old)
            packed_index = idx >> 1
            if x & 1:
                packed[packed_index] |= q
            else:
                packed[packed_index] = q << 4

            err = old - (q * 17)
            if x + 1 < PANEL_W:
                gray[idx + 1] = _clamp_u8(gray[idx + 1] + err * 7 // 16)
            if y + 1 < PANEL_H:
                if x > 0:
                    gray[idx + PANEL_W - 1] = _clamp_u8(gray[idx + PANEL_W - 1] + err * 3 // 16)
                gray[idx + PANEL_W] = _clamp_u8(gray[idx + PANEL_W] + err * 5 // 16)
                if x + 1 < PANEL_W:
                    gray[idx + PANEL_W + 1] = _clamp_u8(gray[idx + PANEL_W + 1] + err // 16)

    return packed


def _dither_gray16_calibrated(rgb: Image.Image, gamma: float = CAL_GAMMA, highlights: float = CAL_HIGHLIGHTS, auto_stretch: bool = True) -> bytearray:
    pixels = rgb.tobytes()
    pixel_count = PANEL_W * PANEL_H
    gray = array("h", [0]) * pixel_count

    for pixel_index in range(pixel_count):
        source_index = pixel_index * 3
        gray[pixel_index] = _luma_bt709(
            pixels[source_index],
            pixels[source_index + 1],
            pixels[source_index + 2],
        )

    if auto_stretch:
        black, white = _percentile_bounds(gray, CAL_BLACK_PCT, CAL_WHITE_PCT)
    else:
        black, white = 0, 255
    tone_lut = _build_tone_lut(black, white, gamma, highlights)
    for pixel_index in range(pixel_count):
        gray[pixel_index] = tone_lut[_clamp_u8(gray[pixel_index])]

    packed = bytearray(pixel_count // 2)
    for y in range(PANEL_H):
        row = y * PANEL_W
        for x in range(PANEL_W):
            idx = row + x
            old = _clamp_u8(gray[idx])
            q = _nearest_gray16(old)
            packed_index = idx >> 1
            if x & 1:
                packed[packed_index] |= q
            else:
                packed[packed_index] = q << 4

            err = old - (q * 17)
            if x + 1 < PANEL_W:
                gray[idx + 1] = _clamp_u8(gray[idx + 1] + err * 7 // 16)
            if y + 1 < PANEL_H:
                if x > 0:
                    gray[idx + PANEL_W - 1] = _clamp_u8(gray[idx + PANEL_W - 1] + err * 3 // 16)
                gray[idx + PANEL_W] = _clamp_u8(gray[idx + PANEL_W] + err * 5 // 16)
                if x + 1 < PANEL_W:
                    gray[idx + PANEL_W + 1] = _clamp_u8(gray[idx + PANEL_W + 1] + err // 16)

    return packed


def _g16p_output_path(source_path: str, suffix: str = "") -> str:
    root, _ = os.path.splitext(source_path)
    return f"{root}{suffix}.g16p"


def _jpg_output_path(source_path: str, suffix: str = "") -> str:
    root, _ = os.path.splitext(source_path)
    return f"{root}{suffix}.e1003.jpg"


def _write_g16p(path: str, payload: bytes) -> None:
    header = struct.pack(
        "<4sBBHHII",
        MAGIC,
        VERSION,
        0,
        PANEL_W,
        PANEL_H,
        len(payload),
        zlib.crc32(payload) & 0xFFFFFFFF,
    )
    with open(path, "wb") as output:
        output.write(header)
        output.write(payload)


def _write_baseline_jpeg(path: str, rgb: Image.Image) -> None:
    gray = rgb.convert("L")
    gray.save(path, format="JPEG", quality=90, optimize=True, progressive=False)


def _write_payload_preview_jpeg(path: str, payload: bytes) -> None:
    img = Image.new("L", (PANEL_W, PANEL_H), 255)
    pixels = img.load()
    for y in range(PANEL_H):
        row = y * (PANEL_W // 2)
        for x in range(0, PANEL_W, 2):
            byte = payload[row + (x // 2)]
            pixels[x, y] = (byte >> 4) * 17
            pixels[x + 1, y] = (byte & 0x0F) * 17
    img.save(path, format="JPEG", quality=90, optimize=True, progressive=False)


def _blob_url(container_sas_url: str, blob_name: str) -> str:
    parsed = urllib.parse.urlsplit(container_sas_url)
    if not parsed.scheme or not parsed.netloc or not parsed.query:
        raise ValueError("container SAS URL must include scheme, host, container path, and query token")

    base_path = parsed.path.rstrip("/")
    blob_path = f"{base_path}/{urllib.parse.quote(blob_name)}"
    return urllib.parse.urlunsplit((parsed.scheme, parsed.netloc, blob_path, parsed.query, ""))


def _upload_blob(container_sas_url: str, file_path: str, content_type: str) -> str:
    blob_name = os.path.basename(file_path)
    url = _blob_url(container_sas_url, blob_name)
    with open(file_path, "rb") as input_file:
        body = input_file.read()

    request = urllib.request.Request(
        url,
        data=body,
        method="PUT",
        headers={
            "Content-Length": str(len(body)),
            "Content-Type": content_type,
            "x-ms-blob-type": "BlockBlob",
            "x-ms-version": "2020-10-02",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            if response.status not in (200, 201):
                raise RuntimeError(f"upload returned HTTP {response.status}")
    except urllib.error.HTTPError as exc:
        details = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"upload failed for {blob_name}: HTTP {exc.code}: {details}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"upload failed for {blob_name}: {exc.reason}") from exc

    return url


def generate_outputs(source_path: str, rgb: Image.Image, container_sas_url: str | None, gamma: float = CAL_GAMMA, highlights: float = CAL_HIGHLIGHTS) -> int:
    gamma_note = "" if gamma == CAL_GAMMA else " (overridden)"
    highlights_note = "" if highlights == CAL_HIGHLIGHTS else " (overridden)"
    print(f"TONE_GAMMA={gamma}{gamma_note}")
    print(f"TONE_HIGHLIGHTS={highlights}{highlights_note}")

    linear_payload = _dither_gray16(rgb)
    preprocessed_payload = _dither_gray16_calibrated(rgb, gamma, highlights, auto_stretch=False)
    calibrated_payload = _dither_gray16_calibrated(rgb, gamma, highlights, auto_stretch=True)

    outputs = [
        ("G16P_LINEAR", _g16p_output_path(source_path), "application/octet-stream"),
        ("JPG_LINEAR", _jpg_output_path(source_path), "image/jpeg"),
        ("G16P_PRE", _g16p_output_path(source_path, ".pre"), "application/octet-stream"),
        ("JPG_PRE", _jpg_output_path(source_path, ".pre"), "image/jpeg"),
        ("G16P_CAL", _g16p_output_path(source_path, ".cal"), "application/octet-stream"),
        ("JPG_CAL", _jpg_output_path(source_path, ".cal"), "image/jpeg"),
    ]

    _write_g16p(outputs[0][1], linear_payload)
    _write_baseline_jpeg(outputs[1][1], rgb)
    _write_g16p(outputs[2][1], preprocessed_payload)
    _write_payload_preview_jpeg(outputs[3][1], preprocessed_payload)
    _write_g16p(outputs[4][1], calibrated_payload)
    _write_payload_preview_jpeg(outputs[5][1], calibrated_payload)

    for label, path, _ in outputs:
        print(f"{label}_FILE={path}")
    if container_sas_url:
        try:
            uploaded = [
                (label, _upload_blob(container_sas_url, path, content_type))
                for label, path, content_type in outputs
            ]
        except (RuntimeError, ValueError) as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 1

        for label, url in uploaded:
            print(f"{label}_SAS_URL={url}")
    return 0


def generate_bars(container_sas_url: str | None = None, gamma: float = CAL_GAMMA, highlights: float = CAL_HIGHLIGHTS) -> int:
    output_dir = os.path.join("sample", "img")
    os.makedirs(output_dir, exist_ok=True)
    source_path = os.path.join(output_dir, "e1003-gray16-bars.png")
    return generate_outputs(source_path, _generate_bars_rgb(), container_sas_url, gamma, highlights)


def _parse_options(args: list[str]) -> tuple[float, float, list[str]]:
    gamma = CAL_GAMMA
    highlights = CAL_HIGHLIGHTS
    remaining: list[str] = []
    index = 0
    while index < len(args):
        token = args[index]
        if token == "--gamma":
            if index + 1 >= len(args):
                raise ValueError("--gamma requires a numeric value")
            gamma = float(args[index + 1])
            index += 2
            continue
        if token.startswith("--gamma="):
            gamma = float(token.split("=", 1)[1])
            index += 1
            continue
        if token == "--highlights":
            if index + 1 >= len(args):
                raise ValueError("--highlights requires a numeric value")
            highlights = float(args[index + 1])
            index += 2
            continue
        if token.startswith("--highlights="):
            highlights = float(token.split("=", 1)[1])
            index += 1
            continue
        remaining.append(token)
        index += 1
    if gamma <= 0:
        raise ValueError("--gamma must be greater than 0")
    if not -1.0 <= highlights <= 1.0:
        raise ValueError("--highlights must be between -1.0 and 1.0")
    return gamma, highlights, remaining


def main() -> int:
    try:
        gamma, highlights, args = _parse_options(sys.argv[1:])
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return _usage()

    if len(args) not in (1, 2):
        return _usage()

    if args[0] == "--bars":
        container_sas_url = args[1] if len(args) == 2 else None
        return generate_bars(container_sas_url, gamma, highlights)

    source_path = args[0]
    container_sas_url = args[1] if len(args) == 2 else None
    if not os.path.isfile(source_path):
        print(f"ERROR: Source image not found: {source_path}", file=sys.stderr)
        return 1

    return generate_outputs(source_path, _load_rgb_panel(source_path), container_sas_url, gamma, highlights)


if __name__ == "__main__":
    sys.exit(main())