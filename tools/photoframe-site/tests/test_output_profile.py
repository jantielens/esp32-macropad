"""Tests for the per-device output profile (config) and the resize-only JPEG
encoder (gray16.encode_jpeg), plus the format-aware blob naming in store.

Run standalone (no pytest needed):

    python3 tests/test_output_profile.py
"""

import io
import json
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from PIL import Image  # noqa: E402

import config as cfg  # noqa: E402
import gray16  # noqa: E402
import store  # noqa: E402


def _load(devices: dict):
    os.environ["CONFIG_JSON"] = json.dumps({"devices": devices, "users": {}})
    return cfg.load_config()


_BASE_DEVICE = {
    "container_sas_url": "https://example.blob.core.windows.net/c?sig=x",
    "api_key": "k",
}


# --- config: output profile ---------------------------------------------------


def test_default_profile_is_g16z():
    config = _load({"E1003-1": dict(_BASE_DEVICE)})
    device = config.device("E1003-1")
    assert device.image_format == cfg.FORMAT_G16Z == "g16z"
    assert device.jpeg_quality == cfg.DEFAULT_JPEG_QUALITY == 90


def test_jpeg_profile_parsed():
    config = _load({
        "ink-1": dict(_BASE_DEVICE, output={"format": "jpeg", "jpeg_quality": 75}),
    })
    device = config.device("ink-1")
    assert device.image_format == "jpeg"
    assert device.jpeg_quality == 75


def test_format_is_case_insensitive():
    config = _load({"ink-1": dict(_BASE_DEVICE, output={"format": "JPEG"})})
    assert config.device("ink-1").image_format == "jpeg"


def test_unknown_format_rejected():
    try:
        _load({"x": dict(_BASE_DEVICE, output={"format": "png"})})
    except cfg.ConfigError:
        return
    raise AssertionError("expected ConfigError for unknown format")


def test_bad_quality_rejected():
    for bad in (0, 101, "abc"):
        try:
            _load({"x": dict(_BASE_DEVICE, output={"format": "jpeg", "jpeg_quality": bad})})
        except cfg.ConfigError:
            continue
        raise AssertionError(f"expected ConfigError for jpeg_quality={bad!r}")


# --- config: serve_mode -------------------------------------------------------


def test_default_serve_mode_is_redirect():
    config = _load({"E1003-1": dict(_BASE_DEVICE)})
    assert config.device("E1003-1").serve_mode == cfg.SERVE_REDIRECT == "redirect"


def test_serve_mode_inline_parsed():
    config = _load({"ink-1": dict(_BASE_DEVICE, serve_mode="inline")})
    assert config.device("ink-1").serve_mode == cfg.SERVE_INLINE == "inline"


def test_serve_mode_is_case_insensitive():
    config = _load({"ink-1": dict(_BASE_DEVICE, serve_mode="INLINE")})
    assert config.device("ink-1").serve_mode == "inline"


def test_unknown_serve_mode_rejected():
    try:
        _load({"x": dict(_BASE_DEVICE, serve_mode="proxy")})
    except cfg.ConfigError:
        return
    raise AssertionError("expected ConfigError for unknown serve_mode")


# --- gray16.encode_jpeg -------------------------------------------------------


def test_encode_jpeg_is_baseline_3component_at_device_size():
    src = Image.new("RGB", (200, 150), (180, 90, 40))
    data, preview = gray16.encode_jpeg(src, width=320, height=240, quality=80)
    assert preview.mode == "L"  # gallery thumbnail stays grayscale
    out = Image.open(io.BytesIO(data))
    assert out.format == "JPEG"
    assert out.size == (320, 240)
    # Encoded as 3-component RGB (YCbCr) so Inkplate's TJpgDec can decode it; a
    # single-component grayscale JPEG is rejected by that decoder.
    assert out.mode == "RGB"
    # Baseline, not progressive (E1003 firmware and simple loaders need baseline).
    assert "progression" not in out.info


def test_encode_jpeg_colour_when_grayscale_false():
    src = Image.new("RGB", (64, 64), (10, 200, 60))
    data, _ = gray16.encode_jpeg(src, width=64, height=64, grayscale=False)
    out = Image.open(io.BytesIO(data))
    assert out.mode == "RGB"


def test_encode_jpeg_quality_affects_size():
    src = Image.effect_noise((256, 256), 80).convert("RGB")
    small, _ = gray16.encode_jpeg(src, width=256, height=256, quality=20)
    large, _ = gray16.encode_jpeg(src, width=256, height=256, quality=95)
    assert len(small) < len(large)


def _mean(img: Image.Image) -> float:
    data = img.convert("L").getdata()
    return sum(data) / len(data)


def test_encode_jpeg_applies_tone_curve():
    # A midtone-grey gradient so auto-stretch + brightness have something to act on.
    src = Image.linear_gradient("L").convert("RGB")
    _, dark = gray16.encode_jpeg(src, width=128, height=128, brightness=-0.2)
    _, bright = gray16.encode_jpeg(src, width=128, height=128, brightness=0.2)
    # Brightness knob is reused on the jpeg path: +0.2 must lift the mean level.
    assert _mean(bright) > _mean(dark)


def test_encode_jpeg_no_panel_calibration():
    # The jpeg path must NOT apply the E1003 panel inverse LUT: its identity-tone
    # output should match calibrated_gray_levels with panel_calibration=0.
    src = Image.linear_gradient("L").convert("RGB")
    _, preview = gray16.encode_jpeg(src, width=64, height=64)
    rgb = gray16.fit_rgb_to_panel(src, 64, 64, resample=gray16.resolve_resampler(None))
    expected = gray16.gray_levels_to_preview(
        gray16.calibrated_gray_levels(rgb, width=64, height=64, panel_calibration=0.0),
        64, 64,
    )
    assert list(preview.getdata()) == list(expected.getdata())


# --- store: format-aware blob naming ------------------------------------------


def test_format_ext_mapping():
    assert store.format_ext("g16z") == ".g16p"
    assert store.format_ext("jpeg") == ".jpg"
    assert store.format_ext(None) == ".g16p"      # legacy default
    assert store.format_ext("bogus") == ".g16p"   # safe fallback


def test_meta_image_ext():
    assert store.meta_image_ext({"format": "jpeg"}) == ".jpg"
    assert store.meta_image_ext({}) == ".g16p"     # pre-format meta -> legacy


def test_image_name_and_split_roundtrip():
    assert store.image_name("abc", ".jpg") == "images/abc.jpg"
    assert store.image_name("abc") == "images/abc.g16p"
    assert store._split_image_name("images/abc.g16p") == ("abc", ".g16p")
    assert store._split_image_name("images/abc.jpg") == ("abc", ".jpg")
    # Non-image blobs are ignored.
    assert store._split_image_name("images/abc__thumb.png") is None
    assert store._split_image_name("images/abc.json") is None
    assert store._split_image_name("state/queue.json") is None


if __name__ == "__main__":
    test_default_profile_is_g16z()
    test_jpeg_profile_parsed()
    test_format_is_case_insensitive()
    test_unknown_format_rejected()
    test_bad_quality_rejected()
    test_encode_jpeg_is_baseline_3component_at_device_size()
    test_encode_jpeg_colour_when_grayscale_false()
    test_encode_jpeg_quality_affects_size()
    test_encode_jpeg_applies_tone_curve()
    test_encode_jpeg_no_panel_calibration()
    test_format_ext_mapping()
    test_meta_image_ext()
    test_image_name_and_split_roundtrip()
    print("ok")
