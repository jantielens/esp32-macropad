"""Upload through the human UI and retrieve the exact generated transport."""

from __future__ import annotations

import io
import json
import os
import tempfile
import zlib
from pathlib import Path

from PIL import Image
from starlette.testclient import TestClient

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
        assert (root / "photos" / image_key / "source.jpg").exists()
        sidecar = json.loads((root / "photos" / image_key / "sidecar.json").read_text())
        assert sidecar["variants"][0]["content_length"] == len(response.content)
        transport_path = root / "photos" / sidecar["variants"][0]["blob_name"]
        transport_path.write_bytes(b"corrupt")
        corrupt = client.get("/api/v1/next", headers={"Authorization": f"Bearer {token}"})
        assert corrupt.status_code == 500
        assert "photoframe-image-key" not in corrupt.headers
        assert "photoframe-content-crc32" not in corrupt.headers
        unchanged = json.loads((root / "photos" / image_key / "sidecar.json").read_text())
        assert unchanged["last_shown_at"] == sidecar["last_shown_at"]


if __name__ == "__main__":
    test_upload_to_next_exact_crc()
    print("PASS test_upload_to_next_exact_crc")