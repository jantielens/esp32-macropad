#!/usr/bin/env python3
"""Run Version 1 service assertions and explicitly skip non-service roles."""

from __future__ import annotations

import json
import tempfile
import zlib
from contextlib import contextmanager
from pathlib import Path

from starlette.testclient import TestClient

import app as application
from config import Frame

ROOT = Path(__file__).resolve().parents[2]
KIT = ROOT / "docs/dev/photoframe-next-image/conformance/photoframe-next-image-v1"
ASSERTIONS = KIT / "assertions.json"
PROFILE = KIT / "profiles/e1003-landscape"
VALID_G16Z = PROFILE / "vectors/valid/frame.g16z"
TOKEN = "0123456789abcdef0123456789abcdef"
REVOKED_TOKEN = "fedcba9876543210fedcba9876543210"
PROFILE_KEY = Frame("fixture", TOKEN, 1872, 1404, (3,), image_transform={}).variant_key(3)


def _sidecar(image_id: str, payload: bytes) -> dict:
    return {
        "id": image_id,
        "permanent": True,
        "uploaded_at": "2026-01-01T00:00:00+00:00",
        "last_shown_at": None,
        "served_at": None,
        "expires_at": None,
        "variants": [{
            "width": 1872,
            "height": 1404,
            "format_code": 3,
            "profile_key": PROFILE_KEY,
            "blob_name": "transport-1872x1404-3.g16z",
            "content_length": len(payload),
            "content_crc32": f"{zlib.crc32(payload) & 0xffffffff:08x}",
        }],
    }


@contextmanager
def service(image_ids: tuple[str, ...] = ("image_a",)):
    root = Path(tempfile.mkdtemp())
    (root / "config").mkdir(parents=True)
    frames = {
        "active": {"token": TOKEN, "profile": {"width": 1872, "height": 1404, "format_codes": [3]}},
        "revoked": {"token": REVOKED_TOKEN, "revoked": True,
                    "profile": {"width": 1872, "height": 1404, "format_codes": [3]}},
    }
    (root / "config/frames.json").write_text(json.dumps({"frames": frames}))
    payload = VALID_G16Z.read_bytes()
    for image_id in image_ids:
        folder = root / "devices" / "active" / "images" / image_id
        folder.mkdir(parents=True)
        (folder / "transport-1872x1404-3.g16z").write_bytes(payload)
        (folder / "sidecar.json").write_text(json.dumps(_sidecar(image_id, payload)))
    application.DATA_ROOT = root
    with TestClient(application.app) as client:
        yield client, payload


def _get(client: TestClient, token: str = TOKEN, **headers):
    return client.get("/api/v1/next", headers={"Authorization": f"Bearer {token}", **headers})


def inline_show() -> None:
    with service() as (client, payload):
        response = _get(client)
        assert response.status_code == 200 and response.content == payload
        assert response.headers["content-type"] == "application/vnd.photoframe.g16z"
        assert response.headers["photoframe-image-key"] == "image_a"
        assert response.headers["photoframe-content-crc32"] == f"{zlib.crc32(payload) & 0xffffffff:08x}"
        assert response.headers["cache-control"] == "private, no-cache"
        assert response.headers["vary"] == "Authorization"
        assert "content-encoding" not in response.headers


def authentication_equivalence() -> None:
    with service() as (client, _payload):
        responses = [client.get("/api/v1/next"), _get(client, "unknown"), _get(client, REVOKED_TOKEN)]
        signatures = {(r.status_code, r.content, r.headers.get("www-authenticate")) for r in responses}
        assert signatures == {(401, b"Unauthorized", "Bearer")}
        assert all("photoframe-image-key" not in response.headers for response in responses)


def request_precedence() -> None:
    with service() as (client, _payload):
        assert client.post("/api/v2/next").status_code == 404
        wrong = client.post("/api/v1/next")
        assert wrong.status_code == 405 and wrong.headers["allow"] == "GET"
        malformed = client.get("/api/v1/next", headers={
            "Authorization": "Bearer unknown",
            "Photoframe-Current-Image-Key": "bad key",
            "Photoframe-Current-Content-CRC32": "BAD",
        })
        assert malformed.status_code == 401


def method_not_allowed() -> None:
    with service() as (client, _payload):
        response = client.post("/api/v1/next")
        assert response.status_code == 405 and response.headers["allow"] == "GET"


def ignore_unknown_request_header() -> None:
    with service() as (client, _payload):
        assert _get(client, **{"X-Future-Hint": "ignored"}).status_code == 200


def malformed_fails_open() -> None:
    with service() as (client, _payload):
        assert _get(client, **{"Photoframe-Current-Image-Key": "image_a"}).status_code == 200
    with service() as (client, _payload):
        response = _get(client, **{
            "Photoframe-Current-Image-Key": "bad key",
            "Photoframe-Current-Content-CRC32": "1234ABCD",
        })
        assert response.status_code == 200


def avoid_reported_pair() -> None:
    with service(("image_a", "image_b")) as (client, payload):
        response = _get(client, **{
            "Photoframe-Current-Image-Key": "image_a",
            "Photoframe-Current-Content-CRC32": f"{zlib.crc32(payload) & 0xffffffff:08x}",
        })
        assert response.status_code == 200
        assert response.headers["photoframe-image-key"] == "image_b"


def best_effort_repeat() -> None:
    with service() as (client, payload):
        response = _get(client, **{
            "Photoframe-Current-Image-Key": "image_a",
            "Photoframe-Current-Content-CRC32": f"{zlib.crc32(payload) & 0xffffffff:08x}",
        })
        assert response.status_code in (200, 204)


SERVICE_SCENARIOS = {
    "http.inline-show": inline_show,
    "http.authentication-equivalence": authentication_equivalence,
    "http.request-precedence": request_precedence,
    "http.method-not-allowed": method_not_allowed,
    "versioning.ignore-unknown-request-header": ignore_unknown_request_header,
    "fingerprint.malformed-fails-open": malformed_fails_open,
    "fingerprint.avoid-reported-pair": avoid_reported_pair,
    "fingerprint.best-effort-repeat": best_effort_repeat,
}


def main() -> int:
    scenarios = json.loads(ASSERTIONS.read_text())["scenarios"]
    failures = 0
    for scenario in scenarios:
        scenario_id = scenario["id"]
        if scenario["role"] != "service":
            print(f"SKIP {scenario_id}: requires a {scenario['role']} adapter")
            continue
        test = SERVICE_SCENARIOS.get(scenario_id)
        if test is None:
            print(f"FAIL {scenario_id}: service scenario is not mapped")
            failures += 1
            continue
        try:
            test()
            print(f"PASS {scenario_id}")
        except Exception as exc:
            print(f"FAIL {scenario_id}: {exc}")
            failures += 1
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())