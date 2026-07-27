"""Unit tests for the normalized crop step (gray16.apply_crop).

Run standalone (no pytest needed):

    python3 tests/test_crop.py
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from PIL import Image  # noqa: E402

import gray16  # noqa: E402


def _solid(w, h):
    return Image.new("RGB", (w, h), (10, 20, 30))


def test_full_frame_is_noop() -> None:
    img = _solid(200, 100)
    for crop in (None, {}, {"x": 0, "y": 0, "w": 1, "h": 1}):
        out = gray16.apply_crop(img, crop)
        assert out.size == (200, 100), crop


def test_centered_quarter_box() -> None:
    img = _solid(400, 200)
    out = gray16.apply_crop(img, {"x": 0.25, "y": 0.25, "w": 0.5, "h": 0.5})
    assert out.size == (200, 100)


def test_out_of_bounds_pads_white() -> None:
    # Window extends past the right edge: the region beyond the image becomes a
    # white letterbox/pillarbox bar rather than being clamped away.
    img = _solid(100, 100)
    out = gray16.apply_crop(img, {"x": 0.6, "y": 0.0, "w": 0.9, "h": 1.0})
    assert out.size == (90, 100)
    assert out.getpixel((10, 50)) == (10, 20, 30)   # still inside the image
    assert out.getpixel((80, 50)) == (255, 255, 255)  # white bar past the edge


def test_window_larger_than_image_letterboxes_all_sides() -> None:
    img = _solid(100, 100)
    out = gray16.apply_crop(img, {"x": -0.25, "y": -0.25, "w": 1.5, "h": 1.5})
    assert out.size == (150, 150)
    assert out.getpixel((75, 75)) == (10, 20, 30)     # centered source
    assert out.getpixel((5, 5)) == (255, 255, 255)    # white border
    assert out.getpixel((145, 145)) == (255, 255, 255)


def test_extreme_crop_geometry_is_rejected_before_allocation() -> None:
    img = _solid(100, 100)
    for crop in (
        {"x": -1000000, "y": 0, "w": 1, "h": 1},
        {"x": 0, "y": 0, "w": 1000000, "h": 1},
    ):
        try:
            gray16.apply_crop(img, crop)
        except ValueError as exc:
            assert "Crop" in str(exc)
        else:
            raise AssertionError(f"unsafe crop was accepted: {crop}")


def test_invalid_falls_back_to_noop() -> None:
    img = _solid(80, 60)
    for crop in ({"x": "nan"}, {"w": None}, {"x": 2.0, "y": 2.0, "w": 0.0, "h": 0.0}):
        out = gray16.apply_crop(img, crop)
        assert out.size == (80, 60), crop


def test_crop_changes_encode_output_region() -> None:
    # A textured (asymmetric, non-uniform) image so per-crop auto-stretch cannot
    # normalize two different regions into the same preview. Cropping to the left
    # half vs the right half must yield different encoded preview pixels.
    img = Image.new("RGB", (200, 150))
    for x in range(200):
        for y in range(150):
            v = (x * 131 + y * 17) % 256
            img.putpixel((x, y), (v, v, v))
    _, left = gray16.encode_g16p(img, width=64, height=48,
                                 crop={"x": 0.0, "y": 0.0, "w": 0.5, "h": 1.0})
    _, right = gray16.encode_g16p(img, width=64, height=48,
                                  crop={"x": 0.5, "y": 0.0, "w": 0.5, "h": 1.0})
    assert left.size == right.size
    assert bytes(left.tobytes()) != bytes(right.tobytes())


def _run() -> int:
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as exc:
                failures += 1
                print(f"FAIL {name}: {exc}")
    print(f"\n{'OK' if failures == 0 else 'FAILED'} ({failures} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(_run())
