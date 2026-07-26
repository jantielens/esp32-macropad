"""File configuration and exact transport encoder regressions."""

from __future__ import annotations

import io
import json
import os
import tempfile
import zlib
from pathlib import Path

from PIL import Image

import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import config as cfg  # noqa: E402
import gray16  # noqa: E402
import transport  # noqa: E402


def _load(frames: dict, users: dict | None = None) -> cfg.Config:
    root = Path(tempfile.mkdtemp())
    (root / "config").mkdir()
    (root / "config" / "frames.json").write_text(json.dumps({"frames": frames}))
    (root / "config" / "users.json").write_text(json.dumps({"users": users or {}}))
    return cfg.load_config(root)


def _frame(token: str = "0123456789abcdef", **overrides) -> dict:
    value = {"token": token, "profile": {"width": 8, "height": 4, "format_codes": [3, 2]}}
    value.update(overrides)
    return value


def test_capability_preference_and_union():
    config = _load({
        "first": _frame(),
        "second": _frame("fedcba9876543210", profile={"width": 6, "height": 4, "format_codes": [1]}),
        "revoked": _frame("0011223344556677", revoked=True),
    })
    assert config.device("first").format_codes == (3, 2)
    assert config.required_variants() == {(8, 4, 2), (8, 4, 3), (6, 4, 1)}
    assert config.authenticate_frame("0011223344556677") is None


def test_distinct_processing_profiles_get_distinct_variants():
    config = _load({
        "normal": _frame(),
        "rotated": _frame("fedcba9876543210", image_transform={"rotate_deg": 180}),
    })
    assert len(config.required_variants()) == 2
    assert len(config.variant_requirements()) == 4


def test_duplicate_token_rejected():
    try:
        _load({"one": _frame(), "two": _frame()})
    except cfg.ConfigError:
        return
    raise AssertionError("duplicate frame token accepted")


def test_invalid_profile_values_rejected():
    invalid = (
        _frame(profile={"width": 7, "height": 4, "format_codes": [2]}),
        _frame(profile={"width": 8, "height": 4, "format_codes": [0]}),
        _frame(profile={"width": 8, "height": 4, "format_codes": [2, 2]}),
        _frame(jpeg_quality=101),
    )
    for entry in invalid:
        try:
            _load({"bad": entry})
        except cfg.ConfigError:
            continue
        raise AssertionError(f"invalid frame accepted: {entry}")


def test_encode_jpeg_is_baseline_at_exact_size():
    source = Image.new("RGB", (20, 10), (180, 90, 40))
    payload = transport.encode_variant(source, width=8, height=4, format_code=1, jpeg_quality=80)
    with Image.open(io.BytesIO(payload)) as encoded:
        assert encoded.format == "JPEG"
        assert encoded.size == (8, 4)
        assert encoded.mode == "RGB"
        assert "progression" not in encoded.info


def test_encode_g16_variants_are_exact_formats():
    source = Image.linear_gradient("L").convert("RGB")
    g16p = transport.encode_variant(source, width=8, height=4, format_code=2)
    g16z = transport.encode_variant(source, width=8, height=4, format_code=3)
    assert g16p.startswith(b"G16P")
    assert g16z.startswith(gray16.G16Z_MAGIC)
    assert zlib.decompress(g16z[4:], wbits=-15) == g16p


if __name__ == "__main__":
    tests = sorted((name, value) for name, value in globals().items() if name.startswith("test_") and callable(value))
    for name, test in tests:
        test()
        print(f"PASS {name}")
    print(f"\n{len(tests)}/{len(tests)} passed")