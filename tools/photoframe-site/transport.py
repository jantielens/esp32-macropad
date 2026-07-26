"""Upload-time generation of exact contract transport variants."""

from __future__ import annotations

import zlib

from PIL import Image

import gray16


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
        compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
        return gray16.G16Z_MAGIC + compressor.compress(g16p) + compressor.flush()
    raise ValueError(f"unsupported format code: {format_code}")