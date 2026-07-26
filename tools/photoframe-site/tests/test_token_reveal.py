#!/usr/bin/env python3
"""Authenticated device-token reveal regression tests."""

from __future__ import annotations

import json
import os
import re
import sys
import tempfile
from pathlib import Path

from starlette.testclient import TestClient

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import app as application  # noqa: E402
import config  # noqa: E402
import ui  # noqa: E402


def _site() -> tuple[Path, str]:
    root = Path(tempfile.mkdtemp())
    token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    config_root = root / "config"
    config_root.mkdir()
    (config_root / "frames.json").write_text(json.dumps({"frames": {
        "living-room": {"token": token, "profile": {"width": 8, "height": 4, "format_codes": [3, 2]}},
        "private-frame": {"token": "abcdef0123456789abcdef0123456789",
                          "profile": {"width": 8, "height": 4, "format_codes": [2]}},
    }}), encoding="utf-8")
    (config_root / "users.json").write_text(json.dumps({"users": {
        "owner@example.com": {"password_hash": config.hash_password("a-secure-password"),
                              "frames": ["living-room"]}
    }}), encoding="utf-8")
    return root, token


def _nonce(html: str) -> str:
    match = re.search(r'name="token_nonce" value="([^"]+)"', html)
    assert match
    return match.group(1)


def test_token_reveal_requires_password_and_nonce() -> None:
    ui._login_throttle = ui._LoginThrottle()
    root, token = _site()
    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        unauthenticated = client.get("/settings?device_id=living-room", follow_redirects=False)
        assert unauthenticated.status_code == 303
        client.post("/login", data={"email": "owner@example.com", "password": "a-secure-password"})

        settings = client.get("/settings?device_id=living-room")
        assert settings.status_code == 200
        assert token not in settings.text
        nonce = _nonce(settings.text)
        assert client.get("/settings?device_id=private-frame").status_code == 403

        missing_nonce = client.post("/settings/token/reveal", data={
            "device_id": "living-room", "password": "a-secure-password"
        })
        assert missing_nonce.status_code == 403

        wrong_password = client.post("/settings/token/reveal", data={
            "device_id": "living-room", "password": "wrong-password", "token_nonce": nonce
        })
        assert wrong_password.status_code == 403
        assert token not in wrong_password.text

        reveal = client.post("/settings/token/reveal", data={
            "device_id": "living-room",
            "password": "a-secure-password",
            "token_nonce": _nonce(wrong_password.text),
        })
        assert reveal.status_code == 200
        assert token in reveal.text
        assert reveal.headers["cache-control"] == "no-store"
        assert reveal.headers["pragma"] == "no-cache"

        reused_nonce = client.post("/settings/token/reveal", data={
            "device_id": "living-room", "password": "a-secure-password", "token_nonce": nonce
        })
        assert reused_nonce.status_code == 403


def test_forwarded_headers_cannot_bypass_reveal_throttle() -> None:
    ui._login_throttle = ui._LoginThrottle()
    root, _token = _site()
    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        client.post("/login", data={"email": "owner@example.com", "password": "a-secure-password"})
        response = client.get("/settings?device_id=living-room")
        for attempt in range(10):
            response = client.post(
                "/settings/token/reveal",
                data={"device_id": "living-room", "password": "wrong-password",
                      "token_nonce": _nonce(response.text)},
                headers={"X-Forwarded-For": f"203.0.113.{attempt}"},
            )
            if response.status_code == 429:
                break
        assert response.status_code == 429


if __name__ == "__main__":
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")