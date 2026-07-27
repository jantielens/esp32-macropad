"""Upload through the human UI and retrieve the exact generated transport."""

from __future__ import annotations

import io
import json
import os
import tempfile
import zlib
from pathlib import Path

from PIL import Image
from site_client import TestClient

import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import app as application  # noqa: E402
import config  # noqa: E402


def test_upload_to_next_exact_crc():
    root = Path(tempfile.mkdtemp())
    (root / "config").mkdir()
    token = "0123456789abcdef0123456789abcdef"
    (root / "config/frames.json").write_text(json.dumps({"frames": {
        "test": {"token": token, "profile": {"width": 8, "height": 4, "format_codes": [2]}}
    }}))
    (root / "config/users.json").write_text(json.dumps({"users": {
        "owner@example.com": {"password_hash": config.hash_password("secret"), "frames": ["test"]}
    }}))
    source = Image.new("RGB", (16, 8), (120, 80, 40))
    image_bytes = io.BytesIO()
    source.save(image_bytes, format="JPEG")

    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        login = client.post("/login", data={"email": "owner@example.com", "password": "secret"},
                            follow_redirects=False)
        assert login.status_code == 303
        upload = client.post(
            "/upload",
            data={"device_id": "test", "permanent": "on", "caption": "Integration"},
            files={"file": ("source.jpg", image_bytes.getvalue(), "image/jpeg")},
            follow_redirects=False,
        )
        assert upload.status_code == 303
        response = client.get("/api/v1/next", headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 200
        assert response.headers["content-type"] == "application/vnd.photoframe.g16p"
        assert response.headers["photoframe-content-crc32"] == f"{zlib.crc32(response.content) & 0xffffffff:08x}"
        image_key = response.headers["photoframe-image-key"]
        image_root = root / "devices" / "test" / "images" / image_key
        assert (image_root / "source.jpg").exists()
        sidecar = json.loads((image_root / "sidecar.json").read_text())
        assert sidecar["variants"][0]["content_length"] == len(response.content)
        transport_path = image_root / sidecar["variants"][0]["blob_name"]
        transport_path.write_bytes(b"corrupt")
        corrupt = client.get("/api/v1/next", headers={"Authorization": f"Bearer {token}"})
        assert corrupt.status_code == 500
        assert "photoframe-image-key" not in corrupt.headers
        assert "photoframe-content-crc32" not in corrupt.headers
        unchanged = json.loads((image_root / "sidecar.json").read_text())
        assert unchanged["last_shown_at"] == sidecar["last_shown_at"]


def test_same_profile_devices_are_isolated_both_directions():
    root = Path(tempfile.mkdtemp())
    (root / "config").mkdir()
    token_a = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    token_b = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    profile = {"width": 8, "height": 4, "format_codes": [2]}
    (root / "config/frames.json").write_text(json.dumps({"frames": {
        "device-a": {"token": token_a, "profile": profile},
        "device-b": {"token": token_b, "profile": profile},
    }}))
    (root / "config/users.json").write_text(json.dumps({"users": {
        "owner@example.com": {
            "password_hash": config.hash_password("secret"),
            "frames": ["device-a", "device-b"],
        }
    }}))

    def source_bytes(color: tuple[int, int, int]) -> bytes:
        output = io.BytesIO()
        Image.new("RGB", (16, 8), color).save(output, format="JPEG")
        return output.getvalue()

    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        client.post("/login", data={"email": "owner@example.com", "password": "secret"})
        upload_a = client.post(
            "/upload",
            data={"device_id": "device-a", "permanent": "on", "caption": "A"},
            files={"file": ("a.jpg", source_bytes((180, 20, 20)), "image/jpeg")},
            follow_redirects=False,
        )
        assert upload_a.status_code == 303
        served_a = client.get("/api/v1/next", headers={"Authorization": f"Bearer {token_a}"})
        assert served_a.status_code == 200
        assert client.get("/api/v1/next", headers={"Authorization": f"Bearer {token_b}"}).status_code == 204

        upload_b = client.post(
            "/upload",
            data={"device_id": "device-b", "permanent": "on", "caption": "B"},
            files={"file": ("b.jpg", source_bytes((20, 20, 180)), "image/jpeg")},
            follow_redirects=False,
        )
        assert upload_b.status_code == 303
        served_b = client.get("/api/v1/next", headers={"Authorization": f"Bearer {token_b}"})
        assert served_b.status_code == 200
        assert served_a.headers["photoframe-image-key"] != served_b.headers["photoframe-image-key"]
        served_a_again = client.get("/api/v1/next", headers={"Authorization": f"Bearer {token_a}"})
        assert served_a_again.status_code == 200
        assert served_a_again.headers["photoframe-image-key"] == served_a.headers["photoframe-image-key"]
        assert served_a_again.headers["photoframe-image-key"] != served_b.headers["photoframe-image-key"]
        assert client.get("/photos?device_id=device-a").text.count("A") > 0
        assert "B</p>" not in client.get("/photos?device_id=device-a").text
        assert client.get("/photos?device_id=device-b").text.count("B") > 0
        assert "A</p>" not in client.get("/photos?device_id=device-b").text


if __name__ == "__main__":
    tests = [value for name, value in sorted(globals().items())
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")