"""Device CRUD, token lifecycle, profile backfill, and telemetry regressions."""

from __future__ import annotations

import io
import json
import os
import re
import sys
import tempfile
import threading
from pathlib import Path

from PIL import Image
from site_client import TestClient

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import app as application  # noqa: E402
import config  # noqa: E402
import transport  # noqa: E402


PASSWORD = "a-secure-password"
OWNER = "owner@example.com"
ORIGINAL_TOKEN = "0123456789abcdef0123456789abcdef"


def _site() -> Path:
    root = Path(tempfile.mkdtemp())
    (root / "config").mkdir()
    (root / "config/frames.json").write_text(json.dumps({"frames": {
        "original": {
            "display_name": "Original",
            "token": ORIGINAL_TOKEN,
            "profile": {"width": 8, "height": 4, "format_codes": [2]},
        }
    }}))
    (root / "config/users.json").write_text(json.dumps({"users": {
        OWNER: {"password_hash": config.hash_password(PASSWORD), "frames": ["original"]}
    }}))
    return root


def _nonce(html: str, name: str) -> str:
    match = re.search(rf'name="{name}" value="([^"]+)"', html)
    assert match
    return match.group(1)


def _login(client: TestClient) -> None:
    response = client.post("/login", data={"email": OWNER, "password": PASSWORD},
                           follow_redirects=False)
    assert response.status_code == 303


def _image_bytes() -> bytes:
    output = io.BytesIO()
    Image.linear_gradient("L").resize((16, 8)).convert("RGB").save(output, format="JPEG")
    return output.getvalue()


def test_add_edit_rotate_telemetry_and_hard_delete() -> None:
    root = _site()
    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        _login(client)
        add_page = client.get("/devices/add")
        assert "Create new device" in add_page.text
        assert "Import existing device" in add_page.text
        add = client.post("/devices/add", data={
            "display_name": "Kitchen Wall",
            "width": "8",
            "height": "4",
            "format_codes": "3,2",
            "control_nonce": _nonce(add_page.text, "control_nonce"),
        }, follow_redirects=False)
        assert add.status_code == 303
        frames = json.loads((root / "config/frames.json").read_text())["frames"]
        device_id = next(item for item in frames if item != "original")
        assert re.fullmatch(r"kitchen-wall-[0-9a-f]{6}", device_id)
        assert (root / "devices" / device_id / "images").is_dir()
        assert frames[device_id]["profile"]["format_codes"] == [3, 2]

        invalid_page = client.get("/devices/add")
        invalid = client.post("/devices/add", data={
            "display_name": "Invalid", "width": "7", "height": "4",
            "format_codes": "3,2",
            "control_nonce": _nonce(invalid_page.text, "control_nonce"),
        })
        assert invalid.status_code == 400

        upload = client.post(
            "/upload",
            data={"device_id": device_id, "permanent": "on"},
            files={"file": ("source.jpg", _image_bytes(), "image/jpeg")},
            follow_redirects=False,
        )
        assert upload.status_code == 303
        original_encode = transport.encode_variant
        transport.encode_variant = lambda *_args, **_kwargs: (_ for _ in ()).throw(
            RuntimeError("simulated conversion failure")
        )
        failed_edit_page = client.get(f"/settings?device_id={device_id}")
        try:
            failed_edit = client.post(f"/devices/{device_id}/edit", data={
                "display_name": "Broken Update",
                "width": "10",
                "height": "4",
                "format_codes": "1",
                "control_nonce": _nonce(failed_edit_page.text, "control_nonce"),
            })
        finally:
            transport.encode_variant = original_encode
        assert failed_edit.status_code == 500
        assert "The previous profile is still active" in failed_edit.text
        unchanged = json.loads((root / "config/frames.json").read_text())["frames"][device_id]
        assert unchanged["profile"] == {"width": 8, "height": 4, "format_codes": [3, 2]}

        edit_page = client.get(f"/devices/{device_id}/edit")
        assert "Kitchen Wall · Settings" in edit_page.text
        assert "Device details" in edit_page.text
        assert "Export device" in edit_page.text
        assert "Replace from bundle" not in edit_page.text
        edit = client.post(f"/devices/{device_id}/edit", data={
            "display_name": "Kitchen Display",
            "width": "10",
            "height": "4",
            "format_codes": "1",
            "control_nonce": _nonce(edit_page.text, "control_nonce"),
        }, follow_redirects=False)
        assert edit.status_code == 303
        updated = json.loads((root / "config/frames.json").read_text())["frames"][device_id]
        assert updated["display_name"] == "Kitchen Display"
        assert updated["profile"] == {"width": 10, "height": 4, "format_codes": [1]}
        sidecar_path = next((root / "devices" / device_id / "images").glob("*/sidecar.json"))
        sidecar = json.loads(sidecar_path.read_text())
        assert [(item["width"], item["format_code"]) for item in sidecar["variants"]] == [(10, 1)]
        assert (sidecar_path.parent / sidecar["variants"][0]["blob_name"]).read_bytes()

        old_token = updated["token"]
        settings = client.get(f"/settings?device_id={device_id}")
        rotate = client.post("/settings/token/rotate", data={
            "device_id": device_id,
            "password": PASSWORD,
            "management_nonce": _nonce(settings.text, "management_nonce"),
        })
        assert rotate.status_code == 200
        rotated = json.loads((root / "config/frames.json").read_text())["frames"][device_id]["token"]
        assert rotated != old_token
        assert rotated in rotate.text
        assert client.get("/api/v1/next", headers={"Authorization": f"Bearer {old_token}"}).status_code == 401
        shown = client.get("/api/v1/next", headers={"Authorization": f"Bearer {rotated}"})
        assert shown.status_code == 200
        telemetry = json.loads((root / "devices" / device_id / "state/telemetry.json").read_text())
        assert telemetry["last_request_at"]
        assert telemetry["image_key"] == shown.headers["photoframe-image-key"]

        settings = client.get(f"/settings?device_id={device_id}")
        wrong_remove = client.post("/devices/remove", data={
            "device_id": device_id,
            "password": PASSWORD,
            "confirm": "wrong-id",
            "management_nonce": _nonce(settings.text, "management_nonce"),
        })
        assert wrong_remove.status_code == 400
        assert wrong_remove.text.index("Remove device") < wrong_remove.text.index(
            "Enter the device ID to confirm removal."
        )

        settings = client.get(f"/settings?device_id={device_id}")
        removed = client.post("/devices/remove", data={
            "device_id": device_id,
            "password": PASSWORD,
            "confirm": device_id,
            "management_nonce": _nonce(settings.text, "management_nonce"),
        }, follow_redirects=False)
        assert removed.status_code == 303
        assert not (root / "devices" / device_id).exists()
        assert device_id not in json.loads((root / "config/frames.json").read_text())["frames"]
        owner = json.loads((root / "config/users.json").read_text())["users"][OWNER]
        assert device_id not in owner["frames"]
        assert client.get("/api/v1/next", headers={"Authorization": f"Bearer {rotated}"}).status_code == 401


def test_settings_nonces_remain_valid_across_tabs() -> None:
    root = _site()
    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        _login(client)
        first = client.get("/settings?device_id=original")
        second = client.get("/settings?device_id=original")
        values = {
            "display_name": "Original",
            "width": "8",
            "height": "4",
            "format_codes": "2",
        }
        first_save = client.post("/devices/original/edit", data={
            **values, "control_nonce": _nonce(first.text, "control_nonce"),
        }, follow_redirects=False)
        second_save = client.post("/devices/original/edit", data={
            **values, "control_nonce": _nonce(second.text, "control_nonce"),
        }, follow_redirects=False)
        assert first_save.status_code == 303
        assert second_save.status_code == 303


def test_interrupted_namespace_remove_is_recovered() -> None:
    root = _site()
    namespace = root / "devices/original"
    namespace.mkdir(parents=True)
    (namespace / "marker").write_text("old")
    frames = {"frames": {}}
    users = {"users": {
        OWNER: {"password_hash": config.hash_password(PASSWORD), "frames": []}
    }}
    transaction = {
        "frame_data": frames,
        "user_data": users,
        "namespace": {
            "operation": "remove",
            "target": "devices/original",
            "backup": "devices/.original.remove-test",
        },
    }
    config._atomic_write_json(root / "config/config.pending.json", transaction)
    recovered = config.load_config(root)
    assert "original" not in recovered.frames
    assert not namespace.exists()
    assert not (root / "devices/.original.remove-test").exists()
    assert not (root / "config/config.pending.json").exists()


def test_interrupted_namespace_replace_is_recovered() -> None:
    root = _site()
    live = root / "devices/original"
    staged = root / "devices/.original.import-test"
    live.mkdir(parents=True)
    staged.mkdir(parents=True)
    (live / "marker").write_text("old")
    (staged / "marker").write_text("new")
    frames = json.loads((root / "config/frames.json").read_text())
    frames["frames"]["original"]["display_name"] = "Recovered"
    users = json.loads((root / "config/users.json").read_text())
    transaction = {
        "frame_data": frames,
        "user_data": users,
        "namespace": {
            "operation": "replace",
            "staged": "devices/.original.import-test",
            "target": "devices/original",
            "backup": "devices/.original.backup-test",
        },
    }
    config._atomic_write_json(root / "config/config.pending.json", transaction)
    recovered = config.load_config(root)
    assert recovered.frames["original"].display_name == "Recovered"
    assert (live / "marker").read_text() == "new"
    assert not (root / "devices/.original.backup-test").exists()
    assert not (root / "config/config.pending.json").exists()


def test_concurrent_config_mutations_preserve_every_device() -> None:
    root = _site()
    count = 8
    barrier = threading.Barrier(count)
    errors = []

    def add(number: int) -> None:
        try:
            barrier.wait()
            config.add_frame(
                root,
                owner_email=OWNER,
                display_name=f"Concurrent {number}",
                width=8,
                height=4,
                format_codes=(2,),
            )
        except Exception as exc:
            errors.append(exc)

    threads = [threading.Thread(target=add, args=(number,)) for number in range(count)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert not errors
    configured = config.load_config(root)
    assert len(configured.frames) == count + 1
    assert len(configured.users[OWNER].devices) == count + 1


if __name__ == "__main__":
    tests = [value for name, value in sorted(globals().items())
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
