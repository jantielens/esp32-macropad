"""Upload-time generation of exact contract transport variants."""

from __future__ import annotations

import zlib

from PIL import Image

import gray16


def _encode_options(
    *,
    width: int,
    height: int,
    transform: dict | None,
    crop: dict | None,
    knobs: dict | None,
    resampler: str | None,
) -> tuple[dict, dict]:
    values = knobs or {}
    common = {
        "width": width,
        "height": height,
        "transform": transform or {},
        "crop": crop or {},
        "resampler": resampler,
        "gamma": values.get("gamma", gray16.CAL_GAMMA),
        "highlights": values.get("highlights", gray16.CAL_HIGHLIGHTS),
        "brightness": values.get("brightness", gray16.CAL_BRIGHTNESS),
        "contrast": values.get("contrast", gray16.CAL_CONTRAST),
        "midtone": values.get("midtone", gray16.CAL_MIDTONE),
    }
    return values, common


def _encode_g16z(g16p: bytes) -> bytes:
    compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
    return gray16.G16Z_MAGIC + compressor.compress(g16p) + compressor.flush()


def encode_g16_pair(
    image: Image.Image,
    *,
    width: int,
    height: int,
    transform: dict | None = None,
    crop: dict | None = None,
    knobs: dict | None = None,
    resampler: str | None = None,
) -> tuple[bytes, bytes, Image.Image]:
    """Encode G16P and G16Z once and return the unchanged upload preview."""
    values, common = _encode_options(
        width=width,
        height=height,
        transform=transform,
        crop=crop,
        knobs=knobs,
        resampler=resampler,
    )
    g16p, preview = gray16.encode_g16p(
        image,
        panel_calibration=values.get("panel_calibration", gray16.CAL_PANEL_STRENGTH),
        preview_before_panel_calibration=True,
        **common,
    )
    return g16p, _encode_g16z(g16p), preview


def encode_variant(
    image: Image.Image,
    *,
    width: int,
    height: int,
    format_code: int,
    transform: dict | None = None,
    crop: dict | None = None,
    knobs: dict | None = None,
    resampler: str | None = None,
    jpeg_quality: int = 90,
) -> bytes:
    values, common = _encode_options(
        width=width,
        height=height,
        transform=transform,
        crop=crop,
        knobs=knobs,
        resampler=resampler,
    )
    if format_code == 1:
        encoded, _preview = gray16.encode_jpeg(image, quality=jpeg_quality, **common)
        return encoded
    g16p, _preview = gray16.encode_g16p(
        image,
        panel_calibration=values.get("panel_calibration", gray16.CAL_PANEL_STRENGTH),
        **common,
    )
    if format_code == 2:
        return g16p
    if format_code == 3:
        return _encode_g16z(g16p)
    raise ValueError(f"unsupported format code: {format_code}")