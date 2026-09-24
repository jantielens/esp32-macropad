#!/usr/bin/env python3
"""Standalone API tests for ordered batch selection and exact-byte retrieval."""

from __future__ import annotations

import json
import os
import shutil
import sys
import zlib
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit

from site_client import TestClient

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import app as application  # noqa: E402
import config  # noqa: E402

TOKEN_A = "a" * 32
TOKEN_B = "b" * 32
AUTH_A = {"Authorization": "Bear" + "er " + TOKEN_A}
AUTH_B = {"Authorization": "Bear" + "er " + TOKEN_B}
TEST_ROOT = Path(__file__).with_name(".next-batch-test-data")


def _frame(token: str) -> dict:
    return {"token": token, "profile": {"width": 8, "height": 4, "format_codes": [2]}}


@contextmanager
def _client(name: str, *, two_frames: bool = False):
    root = TEST_ROOT / name
    shutil.rmtree(root, ignore_errors=True)
    (root / "config").mkdir(parents=True)
    frames = {"frame-a": _frame(TOKEN_A)}
    if two_frames:
        frames["frame-b"] = _frame(TOKEN_B)
    (root / "config/frames.json").write_text(json.dumps({"frames": frames}), encoding="utf-8")
    (root / "config/users.json").write_text('{"users":{}}\n', encoding="utf-8")
    application.DATA_ROOT = root
    try:
        with TestClient(application.app) as client:
            yield client, root
    finally:
        shutil.rmtree(root, ignore_errors=True)


def _add_image(
    root: Path,
    device_id: str,
    image_key: str,
    payload: bytes,
    *,
    stored_payload: bytes | None = None,
) -> Path:
    frame = config.load_config(root).frames[device_id]
    blob_name = f"transport-{image_key}.g16p"
    image_root = root / "devices" / device_id / "images" / image_key
    image_root.mkdir(parents=True)
    (image_root / blob_name).write_bytes(payload if stored_payload is None else stored_payload)
    sidecar = {
        "id": image_key,
        "permanent": True,
        "uploaded_at": "2020-01-01T00:00:00+00:00",
        "expires_at": None,
        "variants": [{
            "format_code": 2,
            "width": 8,
            "height": 4,
            "profile_key": frame.variant_key(2),
            "blob_name": blob_name,
            "content_length": len(payload),
            "content_crc32": f"{zlib.crc32(payload) & 0xffffffff:08x}",
        }],
    }
    (image_root / "sidecar.json").write_text(json.dumps(sidecar), encoding="utf-8")
    return image_root


def test_authentication_and_count_limits() -> None:
    with _client("auth-count") as (client, root):
        _add_image(root, "frame-a", "only", b"only-payload")
        client.app.state.index.rebuild()
        assert client.get("/api/v1/next-batch?count=1").status_code == 401
        assert client.get("/api/v1/next-batch?count=bad").status_code == 401
        for query in (
            "", "?count=0", "?count=-1", "?count=18", "?count=99999999999999999999",
            "?count=1&count=2",
        ):
            response = client.get(f"/api/v1/next-batch{query}", headers=AUTH_A)
            assert response.status_code == 400
        maximum = client.get("/api/v1/next-batch?count=17", headers=AUTH_A)
        assert maximum.status_code == 200
        assert len(maximum.json()["images"]) == 1
        entry = maximum.json()["images"][0]
        excluded_only = client.get(
            "/api/v1/next-batch?count=1",
            headers={
                **AUTH_A,
                "Photoframe-Current-Image-Key": entry["image_key"],
                "Photoframe-Current-Content-CRC32": entry["content_crc32"],
            },
        )
        assert excluded_only.status_code == 204


def test_short_batch_excludes_current_duplicates_and_invalid_blobs() -> None:
    with _client("selection") as (client, root):
        payloads = {
            "a-current": b"current",
            "b-first": b"first",
            "c-corrupt": b"expected",
            "d-third": b"third",
        }
        roots = {
            key: _add_image(
                root,
                "frame-a",
                key,
                payload,
                stored_payload=b"corrupt" if key == "c-corrupt" else None,
            )
            for key, payload in payloads.items()
        }
        client.app.state.index.rebuild()
        current_crc = f"{zlib.crc32(payloads['a-current']) & 0xffffffff:08x}"
        response = client.get(
            "/api/v1/next-batch?count=5",
            headers={
                **AUTH_A,
                "Photoframe-Current-Image-Key": "a-current",
                "Photoframe-Current-Content-CRC32": current_crc,
            },
        )
        assert response.status_code == 200
        entries = response.json()["images"]
        assert [entry["image_key"] for entry in entries] == ["b-first", "d-third"]
        assert len({(entry["image_key"], entry["content_crc32"]) for entry in entries}) == 2
        for key in ("b-first", "d-third"):
            assert json.loads((roots[key] / "sidecar.json").read_text())["last_shown_at"]
        for key in ("a-current", "c-corrupt"):
            assert "last_shown_at" not in json.loads((roots[key] / "sidecar.json").read_text())

        only_current = client.get(
            "/api/v1/next-batch?count=1",
            headers={
                **AUTH_A,
                "Photoframe-Current-Image-Key": "a-current",
                "Photoframe-Current-Content-CRC32": current_crc,
            },
        )
        assert only_current.status_code == 200
        assert only_current.json()["images"][0]["image_key"] != "a-current"


def test_exact_content_is_authenticated_isolated_and_never_substituted() -> None:
    with _client("content", two_frames=True) as (client, root):
        image_a = _add_image(root, "frame-a", "shared-key", b"frame-a-bytes")
        _add_image(root, "frame-a", "unselected", b"unselected-bytes")
        _add_image(root, "frame-b", "shared-key", b"frame-b-bytes")
        client.app.state.index.rebuild()
        batch = client.get("/api/v1/next-batch?count=1", headers=AUTH_A)
        entry = batch.json()["images"][0]
        content_url = entry["content_url"]
        assert len(content_url) < 384

        guessed = (
            "/api/v1/content/unselected/2/16/"
            f"{zlib.crc32(b'unselected-bytes') & 0xffffffff:08x}"
        )
        assert client.get(guessed, headers=AUTH_A).status_code == 404
        parsed = urlsplit(content_url)
        query = parse_qs(parsed.query)
        query["blob"] = ["transport-unselected.g16p"]
        tampered = urlunsplit((parsed.scheme, parsed.netloc, parsed.path,
                             urlencode(query, doseq=True), parsed.fragment))
        assert client.get(tampered, headers=AUTH_A).status_code == 404

        assert client.get(content_url).status_code == 401
        assert client.get(content_url, headers=AUTH_B).status_code == 404
        content = client.get(content_url, headers=AUTH_A)
        assert content.status_code == 200
        assert content.content == b"frame-a-bytes"
        assert content.headers["content-type"] == "application/vnd.photoframe.g16p"
        assert content.headers["photoframe-image-key"] == entry["image_key"]
        assert content.headers["photoframe-content-crc32"] == entry["content_crc32"]

        blob_name = json.loads((image_a / "sidecar.json").read_text())["variants"][0]["blob_name"]
        (image_a / "sidecar.json").write_text('{"variants": []}', encoding="utf-8")
        client.app.state.index.rebuild()
        assert client.get(content_url, headers=AUTH_A).content == b"frame-a-bytes"
        (image_a / blob_name).write_bytes(b"changed-after-selection")
        changed = client.get(content_url, headers=AUTH_A)
        assert changed.status_code == 500
        assert "photoframe-image-key" not in changed.headers
        assert b"frame-b-bytes" not in changed.content


def test_competing_batches_cannot_interleave_selection_history() -> None:
    with _client("concurrency") as (client, root):
        for key in ("a", "b", "c", "d"):
            _add_image(root, "frame-a", key, key.encode("ascii"))
        client.app.state.index.rebuild()

        def fetch() -> set[str]:
            response = client.get("/api/v1/next-batch?count=2", headers=AUTH_A)
            assert response.status_code == 200
            return {entry["image_key"] for entry in response.json()["images"]}

        with ThreadPoolExecutor(max_workers=2) as executor:
            first, second = [future.result() for future in (executor.submit(fetch), executor.submit(fetch))]
        assert len(first) == len(second) == 2
        assert first.isdisjoint(second)


def test_existing_next_endpoint_still_returns_inline_bytes() -> None:
    with _client("next") as (client, root):
        _add_image(root, "frame-a", "inline", b"inline-bytes")
        client.app.state.index.rebuild()
        response = client.get("/api/v1/next", headers=AUTH_A)
        assert response.status_code == 200
        assert response.content == b"inline-bytes"


if __name__ == "__main__":
    try:
        tests = [value for name, value in sorted(globals().items())
                 if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
            print(f"PASS {test.__name__}")
    finally:
        shutil.rmtree(TEST_ROOT, ignore_errors=True)
