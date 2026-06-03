"""Unit tests for the G16Z transport wrapper (gray16.wrap_g16z).

Run standalone (no pytest needed):

    python3 tests/test_g16z.py
"""

import os
import sys
import zlib

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import gray16  # noqa: E402


def _inflate_raw(body: bytes) -> bytes:
    """Mirror the firmware: raw DEFLATE (no zlib/gzip header) decode."""
    return zlib.decompress(body, -15)


def test_roundtrip_restores_g16p():
    # A compressible G16P-like blob (large run of zeros packs well).
    g16p = gray16.MAGIC + b"\x01\x00" + bytes(64) + bytes(4096)
    wrapped = gray16.wrap_g16z(g16p)
    assert wrapped[:4] == gray16.G16Z_MAGIC
    assert len(wrapped) < len(g16p)
    assert _inflate_raw(wrapped[4:]) == g16p


def test_incompressible_returns_raw_g16p():
    # Random bytes do not compress; wrapper must not grow the wire.
    g16p = gray16.MAGIC + os.urandom(2048)
    wrapped = gray16.wrap_g16z(g16p)
    assert wrapped == g16p
    assert wrapped[:4] == gray16.MAGIC


def test_realistic_panel_payload_shrinks():
    # Smooth gradient nibble payload (typical dithered photo) must shrink.
    width, height = gray16.PANEL_W, gray16.PANEL_H
    nibbles = bytes((i // 256) & 0xFF for i in range(width * height // 2))
    g16p = gray16.pack_g16p(nibbles, width, height)
    wrapped = gray16.wrap_g16z(g16p)
    assert wrapped[:4] == gray16.G16Z_MAGIC
    assert len(wrapped) < len(g16p)
    assert _inflate_raw(wrapped[4:]) == g16p


if __name__ == "__main__":
    test_roundtrip_restores_g16p()
    test_incompressible_returns_raw_g16p()
    test_realistic_panel_payload_shrinks()
    print("ok")
