#!/usr/bin/env python3
"""First-boot setup and persistent configuration regression tests."""

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
import config  # noqa: E402


def test_first_boot_setup_persists() -> None:
    root = Path(tempfile.mkdtemp())
    application.DATA_ROOT = root

    with TestClient(application.app) as client:
        assert client.get("/api/v1/next").status_code == 401
        index = client.get("/", follow_redirects=False)
        assert index.status_code == 303
        assert index.headers["location"] == "/setup"
        setup_page = client.get("/setup")
        assert setup_page.status_code == 200
        assert setup_page.headers["cache-control"] == "no-store"
        assert 'pattern="[a-z0-9][a-z0-9\\-]{0,63}"' in setup_page.text
        nonce_match = re.search(r'name="setup_nonce" value="([^"]+)"', setup_page.text)
        assert nonce_match

        response = client.post("/setup", data={
            "email": "owner@example.com",
            "password": "a-secure-password",
            "password_confirm": "a-secure-password",
            "frame_id": "living-room",
            "width": "8",
            "height": "4",
            "output_profile": "gray16",
            "setup_nonce": nonce_match.group(1),
        })
        assert response.status_code == 200
        assert response.headers["cache-control"] == "no-store"
        assert "reveal it later from device settings" in response.text
        assert client.get("/setup").status_code == 404

        frame_data = json.loads((root / "config" / "frames.json").read_text(encoding="utf-8"))
        user_data = json.loads((root / "config" / "users.json").read_text(encoding="utf-8"))
        token = frame_data["frames"]["living-room"]["token"]
        password_hash = user_data["users"]["owner@example.com"]["password_hash"]
        assert len(token) == 64
        assert token in response.text
        assert "a-secure-password" not in password_hash
        assert config.verify_password("a-secure-password", password_hash)


def test_configured_site_with_no_devices_does_not_return_to_setup() -> None:
    root = Path(tempfile.mkdtemp())
    (root / "config").mkdir()
    (root / "config" / "frames.json").write_text('{"frames":{}}', encoding="utf-8")
    (root / "config" / "users.json").write_text(json.dumps({"users": {
        "owner@example.com": {
            "password_hash": config.hash_password("a-secure-password"),
            "frames": [],
        },
    }}), encoding="utf-8")
    application.DATA_ROOT = root

    with TestClient(application.app) as client:
        login = client.post("/login", data={
            "email": "owner@example.com",
            "password": "a-secure-password",
        }, follow_redirects=False)
        assert login.status_code == 303
        index = client.get("/", follow_redirects=False)
        assert index.status_code == 200
        assert "Add device" in index.text
        assert client.get("/setup").status_code == 404

        duplicate = client.post("/setup", data={
            "email": "other@example.com",
            "password": "another-password",
            "password_confirm": "another-password",
            "frame_id": "other-frame",
            "width": "8",
            "height": "4",
            "output_profile": "gray16",
        })
        assert duplicate.status_code == 404

    before = (root / "config" / "frames.json").read_bytes()
    with TestClient(application.app) as client:
        assert client.get("/login").status_code == 200
        login = client.post(
            "/login",
            data={"email": "owner@example.com", "password": "a-secure-password"},
            follow_redirects=False,
        )
        assert login.status_code == 303
        assert login.headers["location"] == "/"
    assert (root / "config" / "frames.json").read_bytes() == before


def test_session_secret_is_created_once() -> None:
    root = Path(tempfile.mkdtemp())
    first = config.session_secret(root)
    second = config.session_secret(root)
    assert first == second
    assert len(first) == 64
    assert (root / "config" / "session-secret").stat().st_mode & 0o777 == 0o600


def test_interrupted_setup_is_recovered() -> None:
    root = Path(tempfile.mkdtemp())
    original_write = config._atomic_write_json

    def interrupt_users(path: Path, value: dict) -> None:
        if path.name == "users.json":
            raise RuntimeError("simulated interruption")
        original_write(path, value)

    config._atomic_write_json = interrupt_users
    try:
        try:
            config.initialize_site(
                root,
                email="owner@example.com",
                password="a-secure-password",
                frame_id="living-room",
                width=8,
                height=4,
                format_codes=(config.FORMAT_G16Z, config.FORMAT_G16P),
            )
            raise AssertionError("setup should have been interrupted")
        except RuntimeError as exc:
            assert str(exc) == "simulated interruption"
    finally:
        config._atomic_write_json = original_write

    assert config.setup_pending(root)
    recovered, token = config.initialize_site(
        root,
        email="owner@example.com",
        password="a-secure-password",
        frame_id="living-room",
        width=8,
        height=4,
        format_codes=(config.FORMAT_G16Z, config.FORMAT_G16P),
    )
    assert config.is_configured(recovered)
    assert recovered.authenticate_frame(token) is not None
    assert not config.setup_pending(root)


if __name__ == "__main__":
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
    print(f"\n{len(tests)}/{len(tests)} passed")
