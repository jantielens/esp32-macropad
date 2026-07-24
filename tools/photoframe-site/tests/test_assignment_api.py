#!/usr/bin/env python3
"""HTTP contract tests for the photoframe assignment endpoints."""

from __future__ import annotations

import os
import sys
import copy

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
os.environ["COOKIE_SECURE"] = "0"
os.environ.pop("SECRET_KEY", None)

import app  # noqa: E402
import store  # noqa: E402
from fastapi import Response  # noqa: E402
from starlette.testclient import TestClient  # noqa: E402


DEVICE = type(
    "Device",
    (),
    {
        "device_id": "frame-a",
        "container_sas_url": "https://example.invalid/frame-a?sas=1",
        "serve_mode": "inline",
    },
)()

DEVICE_B = type(
    "Device",
    (),
    {
        "device_id": "frame-b",
        "container_sas_url": "https://example.invalid/frame-b?sas=2",
        "serve_mode": "inline",
    },
)()


def _record(revision: int) -> dict:
    return {
        "schema": 1,
        "device_id": "frame-a",
        "revision": revision,
        "image_id": "photo-1",
        "image_key": store.image_key("photo-1"),
        "content_crc32": 0x89ABCDEF,
        "format": "g16z",
        "ext": store.G16P_EXT,
        "created_at": "2026-07-01T12:00:00+00:00",
        "state": "pending",
    }


def test_current_honors_etag_and_changes_after_supersede():
    state = {"current": _record(1), "etag": '"etag-1"'}
    originals = app._assignment_device, app._assignment_current_locked
    app._assignment_device = lambda _device_id, _key: DEVICE
    app._assignment_current_locked = lambda _device: (
        {}, state["current"], state["etag"]
    )
    try:
        with TestClient(app.app) as client:
            first = client.get("/api/assignment/current?device_id=frame-a&key=secret")
            unchanged = client.get(
                "/api/assignment/current?device_id=frame-a&key=secret",
                headers={"If-None-Match": '"etag-1"'},
            )
            state.update(current=_record(2), etag='"etag-2"')
            changed = client.get(
                "/api/assignment/current?device_id=frame-a&key=secret",
                headers={"If-None-Match": '"etag-1"'},
            )
    finally:
        app._assignment_device, app._assignment_current_locked = originals
    assert first.status_code == 200 and first.headers["etag"] == '"etag-1"'
    assert unchanged.status_code == 304 and unchanged.content == b""
    assert unchanged.headers["etag"] == '"etag-1"'
    assert changed.status_code == 200 and changed.json()["revision"] == 2
    assert changed.headers["etag"] == '"etag-2"'


def test_image_rejects_unknown_revision_and_maps_wire_headers():
    record = _record(9)
    document = {"current": record, "journal": []}
    delivered: list[tuple[str, str]] = []
    originals = (
        app._assignment_device,
        app.store.read_assignment,
        app.bs.list_blobs_with_metadata,
        app._deliver_image,
    )
    app._assignment_device = lambda _device_id, _key: DEVICE
    app.store.read_assignment = lambda _sas, _device_id: (document, '"etag"')
    app.bs.list_blobs_with_metadata = lambda _sas, _prefix: {
        store.image_name("photo-1", store.G16P_EXT): {"permanent": "true"}
    }

    def _deliver(_device, blob_name, image_format, *, proxy, headers=None, **_kwargs):
        delivered.append((blob_name, image_format))
        response_headers = dict(headers or {})
        response_headers["X-Image-Format"] = "g16p" if image_format == "g16z" else "jpeg"
        return Response(b"image", media_type="application/octet-stream", headers=response_headers)

    app._deliver_image = _deliver
    try:
        with TestClient(app.app) as client:
            unknown = client.get(
                "/api/assignment/image?device_id=frame-a&key=secret&revision=8"
            )
            found = client.get(
                "/api/assignment/image?device_id=frame-a&key=secret&revision=9"
            )
    finally:
        (
            app._assignment_device,
            app.store.read_assignment,
            app.bs.list_blobs_with_metadata,
            app._deliver_image,
        ) = originals
    assert unknown.status_code == 404 and not delivered[:-1]
    assert found.status_code == 200
    assert found.headers["x-image-key"] == record["image_key"]
    assert found.headers["x-content-crc32"] == "89abcdef"
    assert found.headers["x-image-format"] == "g16p"
    assert delivered == [(store.image_name("photo-1", store.G16P_EXT), "g16z")]


def test_image_rejects_dead_assignment_and_bad_credentials():
    record = _record(9)
    document = {"current": record, "journal": []}
    originals = (
        app._assignment_device,
        app.store.read_assignment,
        app.bs.list_blobs_with_metadata,
    )

    def _device(_device_id, key):
        if key != "secret":
            raise app.HTTPException(status_code=401, detail="Unauthorized")
        return DEVICE

    app._assignment_device = _device
    app.store.read_assignment = lambda _sas, _device_id: (document, '"etag"')
    app.bs.list_blobs_with_metadata = lambda _sas, _prefix: {}
    try:
        with TestClient(app.app) as client:
            unauthorized = client.get(
                "/api/assignment/image?device_id=frame-a&key=wrong&revision=9"
            )
            dead = client.get(
                "/api/assignment/image?device_id=frame-a&key=secret&revision=9"
            )
    finally:
        (
            app._assignment_device,
            app.store.read_assignment,
            app.bs.list_blobs_with_metadata,
        ) = originals
    assert unauthorized.status_code == 401
    assert dead.status_code == 410


def test_image_revision_is_isolated_per_device():
    record_a = _record(1)
    record_b = _record(2)
    record_b.update(device_id="frame-b", image_id="photo-2", image_key=store.image_key("photo-2"))
    documents = {
        "frame-a": {"current": record_a, "journal": []},
        "frame-b": {"current": record_b, "journal": []},
    }
    devices = {"frame-a": DEVICE, "frame-b": DEVICE_B}
    delivered: list[tuple[str, str]] = []
    originals = (
        app._assignment_device,
        app.store.read_assignment,
        app.bs.list_blobs_with_metadata,
        app._deliver_image,
    )

    def _device(device_id, key):
        if key != f"secret-{device_id}":
            raise app.HTTPException(status_code=401, detail="Unauthorized")
        return devices[device_id]

    app._assignment_device = _device
    app.store.read_assignment = lambda _sas, device_id: (documents[device_id], '"etag"')
    app.bs.list_blobs_with_metadata = lambda sas, _prefix: {
        store.image_name(
            "photo-1" if sas == DEVICE.container_sas_url else "photo-2",
            store.G16P_EXT,
        ): {"permanent": "true"}
    }

    def _deliver(_device, blob_name, image_format, *, proxy, headers=None, **_kwargs):
        delivered.append((blob_name, image_format))
        return Response(b"image", media_type="application/octet-stream", headers=headers)

    app._deliver_image = _deliver
    try:
        with TestClient(app.app) as client:
            cross_device = client.get(
                "/api/assignment/image?device_id=frame-a&key=secret-frame-a&revision=2"
            )
            own_image = client.get(
                "/api/assignment/image?device_id=frame-b&key=secret-frame-b&revision=2"
            )
    finally:
        (
            app._assignment_device,
            app.store.read_assignment,
            app.bs.list_blobs_with_metadata,
            app._deliver_image,
        ) = originals

    assert cross_device.status_code == 404
    assert own_image.status_code == 200
    assert delivered == [(store.image_name("photo-2", store.G16P_EXT), "g16z")]


def test_failed_assignment_marker_replays_and_commits_once():
    persisted = {
        "schema": 1,
        "device_id": "frame-a",
        "committed_revision": 0,
        "last_revision": 1,
        "current": _record(1),
        "journal": [],
    }
    persisted["current"].update(
        from_queue=False,
        planned_countdown=2,
        sel_meta={"permanent": True, "format": "g16z", "content_crc32": 1},
    )
    effects: list[str] = []
    attempts = 0
    originals = (
        app.store.read_assignment,
        app.store.write_assignment,
        app.store.commit_displayed,
        app.store.assignment_is_live,
        app.store.bs.list_blobs_with_metadata,
        app._assignment_pick,
    )
    app.store.read_assignment = lambda _sas, _device: (copy.deepcopy(persisted), '"etag"')

    def _write(_sas, _device, document, *, etag):
        nonlocal attempts
        attempts += 1
        if attempts == 1:
            raise RuntimeError("simulated CAS failure")
        persisted.clear()
        persisted.update(copy.deepcopy(document))
        return '"etag-2"'

    app.store.write_assignment = _write
    app.store.commit_displayed = lambda _sas, record: effects.append(record["displayed_at"])
    app.store.assignment_is_live = lambda _record, _blobs: True
    app.store.bs.list_blobs_with_metadata = lambda _sas, _prefix: {}
    app._assignment_pick = lambda _device, blobs=None: None
    try:
        try:
            app._commit_assignment_locked(DEVICE, 1, persisted["current"]["image_key"])
            raise AssertionError("marker failure did not propagate")
        except RuntimeError:
            pass
        current, _etag, accepted = app._commit_assignment_locked(
            DEVICE, 1, persisted["current"]["image_key"]
        )
    finally:
        (
            app.store.read_assignment,
            app.store.write_assignment,
            app.store.commit_displayed,
            app.store.assignment_is_live,
            app.store.bs.list_blobs_with_metadata,
            app._assignment_pick,
        ) = originals
    assert accepted and current is None and persisted["committed_revision"] == 1
    assert effects == [effects[0], effects[0]]


def main() -> int:
    tests = [
        test_current_honors_etag_and_changes_after_supersede,
        test_failed_assignment_marker_replays_and_commits_once,
        test_image_rejects_dead_assignment_and_bad_credentials,
        test_image_revision_is_isolated_per_device,
        test_image_rejects_unknown_revision_and_maps_wire_headers,
    ]
    failures = 0
    for test in tests:
        try:
            test()
            print(f"ok   {test.__name__}")
        except Exception as exc:  # noqa: BLE001
            failures += 1
            print(f"FAIL {test.__name__}: {exc!r}")
    print(f"\n{len(tests) - failures}/{len(tests)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())