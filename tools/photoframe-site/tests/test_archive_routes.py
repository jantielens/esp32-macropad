"""Authentication and confirmation gates for archive HTTP surfaces."""

from __future__ import annotations

import json
import os
import re
import sys
import tempfile
from pathlib import Path

from site_client import TestClient

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import app as application  # noqa: E402
import archive  # noqa: E402
import config  # noqa: E402


OWNER = "owner@example.com"
PASSWORD = "a-secure-password"
TOKEN = "0123456789abcdef0123456789abcdef"


def _site() -> Path:
    root = Path(tempfile.mkdtemp())
    (root / "config").mkdir()
    (root / "config/frames.json").write_text(json.dumps({"frames": {
        "device-one": {
            "token": TOKEN,
            "profile": {"width": 8, "height": 4, "format_codes": [2]},
        }
    }}))
    (root / "config/users.json").write_text(json.dumps({"users": {
        OWNER: {"password_hash": config.hash_password(PASSWORD), "frames": ["device-one"]}
    }}))
    (root / "config/session-secret").write_text("s" * 64 + "\n")
    (root / "devices/device-one/images").mkdir(parents=True)
    (root / "devices/device-one/state").mkdir()
    return root


def _nonce(html: str, name: str) -> str:
    match = re.search(rf'name="{name}" value="([^"]+)"', html)
    assert match
    return match.group(1)


def test_import_routes_require_control_stack() -> None:
    root = _site()
    device_bundle = archive.export_device(root, "device-one", OWNER)
    site_bundle = archive.export_site(root)
    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        client.post("/login", data={"email": OWNER, "password": PASSWORD})
        missing_device_nonce = client.post("/devices/import", data={
            "password": PASSWORD, "confirm": "IMPORT", "expected_device_id": "device-one",
        }, files={"file": ("device.zip", device_bundle, "application/zip")})
        assert missing_device_nonce.status_code == 403

        add_page = client.get("/devices/add")
        assert "Create new device" in add_page.text
        assert "Import existing device" in add_page.text
        wrong_password = client.post("/devices/import", data={
            "password": "wrong", "confirm": "IMPORT",
            "import_nonce": _nonce(add_page.text, "import_nonce"),
        }, files={"file": ("device.zip", device_bundle, "application/zip")})
        assert wrong_password.status_code == 403

        settings = client.get("/settings?device_id=device-one")
        device_export_denied = client.post("/devices/export", data={
            "device_id": "device-one", "password": "wrong",
            "export_nonce": _nonce(settings.text, "export_nonce"),
        })
        assert device_export_denied.status_code == 403
        settings = client.get("/settings?device_id=device-one")
        device_export = client.post("/devices/export", data={
            "device_id": "device-one", "password": PASSWORD,
            "export_nonce": _nonce(settings.text, "export_nonce"),
        })
        assert device_export.status_code == 200
        assert device_export.headers["content-type"] == "application/zip"

        admin = client.get("/site-admin")
        assert "Export site" in admin.text
        assert "Import site" in admin.text
        assert "Import device" not in admin.text
        site_export_denied = client.post("/site/export", data={
            "password": "wrong",
            "site_export_nonce": _nonce(admin.text, "site_export_nonce"),
        })
        assert site_export_denied.status_code == 403
        assert "Invalid password" in site_export_denied.text

        admin = client.get("/site-admin")
        site_export = client.post("/site/export", data={
            "password": PASSWORD,
            "site_export_nonce": _nonce(admin.text, "site_export_nonce"),
        })
        assert site_export.status_code == 200
        assert site_export.headers["content-type"] == "application/zip"

        admin = client.get("/site-admin")
        failed_confirm_nonce = _nonce(admin.text, "site_import_nonce")
        missing_confirm = client.post("/site/import", data={
            "password": PASSWORD,
            "site_import_nonce": failed_confirm_nonce,
        }, files={"file": ("site.zip", site_bundle, "application/zip")})
        assert missing_confirm.status_code == 400
        assert "Enter REPLACE SITE" in missing_confirm.text
        assert 'name="site_import_nonce"' in missing_confirm.text
        reused_nonce = client.post("/site/import", data={
            "password": PASSWORD, "confirm": "REPLACE SITE",
            "site_import_nonce": failed_confirm_nonce,
        }, files={"file": ("site.zip", site_bundle, "application/zip")})
        assert reused_nonce.status_code == 403

        admin = client.get("/site-admin")
        missing_site_nonce = client.post("/site/import", data={
            "password": PASSWORD, "confirm": "REPLACE SITE",
        }, files={"file": ("site.zip", site_bundle, "application/zip")})
        assert missing_site_nonce.status_code == 403


def test_limited_user_cannot_access_site_transfer() -> None:
    root = _site()
    users = json.loads((root / "config/users.json").read_text())
    users["users"]["limited@example.com"] = {
        "password_hash": config.hash_password(PASSWORD), "frames": []
    }
    (root / "config/users.json").write_text(json.dumps(users))
    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        client.post("/login", data={"email": "limited@example.com", "password": PASSWORD})
        assert "Site export &amp; import" not in client.get("/").text
        assert client.get("/site-admin").status_code == 403
        assert client.post("/site/export", data={
            "password": PASSWORD, "site_export_nonce": "invalid",
        }).status_code == 403


def test_existing_device_import_requires_exact_device_id() -> None:
    root = _site()
    device_bundle = archive.export_device(root, "device-one", OWNER)
    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        client.post("/login", data={"email": OWNER, "password": PASSWORD})
        add_page = client.get("/devices/add")
        generic = client.post("/devices/import", data={
            "password": PASSWORD,
            "confirm": "IMPORT",
            "import_nonce": _nonce(add_page.text, "import_nonce"),
        }, files={"file": ("device.zip", device_bundle, "application/zip")})
        assert generic.status_code == 400
        assert "Enter its exact device ID" in generic.text

        add_page = client.get("/devices/add")
        exact = client.post("/devices/import", data={
            "password": PASSWORD,
            "confirm": "device-one",
            "import_nonce": _nonce(add_page.text, "import_nonce"),
        }, files={"file": ("device.zip", device_bundle, "application/zip")},
           follow_redirects=False)
        assert exact.status_code == 303
        assert exact.headers["location"] == "/settings?device_id=device-one"


def test_successful_site_import_requires_restart() -> None:
    root = _site()
    site_bundle = archive.export_site(root)
    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        client.post("/login", data={"email": OWNER, "password": PASSWORD})
        admin = client.get("/site-admin")
        imported = client.post("/site/import", data={
            "password": PASSWORD,
            "confirm": "REPLACE SITE",
            "site_import_nonce": _nonce(admin.text, "site_import_nonce"),
        }, files={"file": ("site.zip", site_bundle, "application/zip")})
        assert imported.status_code == 200
        assert client.get("/healthz").status_code == 200
        assert client.get("/").status_code == 503


if __name__ == "__main__":
    tests = [value for name, value in sorted(globals().items())
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
