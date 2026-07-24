#!/usr/bin/env python3
"""Focused tests for the durable photoframe assignment transaction primitives."""

from __future__ import annotations

import os
import sys
import copy
from datetime import datetime, timedelta, timezone

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import store  # noqa: E402
import backfill_crc  # noqa: E402


def _pick(*, from_queue: bool = False, crc: int = 0x89ABCDEF) -> store.NextPick:
    return store.NextPick(
        image_id="photo-1",
        ext=store.G16P_EXT,
        sel_meta={
            "permanent": not from_queue,
            "format": "g16z",
            "content_crc32": crc,
        },
        from_queue=from_queue,
        planned_countdown=3,
    )


def test_revision_order_wraps_per_rfc1982():
    assert store.revision_newer(1, 0xFFFFFFFF)
    assert store.revision_at_or_before(0xFFFFFFFF, 1)
    assert not store.revision_newer(0x80000000, 0)
    assert store.alloc_revision(0xFFFFFFFF) == 1


def test_assignment_names_do_not_alias_distinct_device_ids():
    assert store.assignment_name("frame/a") != store.assignment_name("frame?a")


def test_read_assignment_rejects_stored_owner_mismatch():
    original = store.bs.download_json_with_etag
    store.bs.download_json_with_etag = lambda _sas, _name: (
        {"schema": 1, "device_id": "frame-b", "last_revision": 99},
        '"etag"',
    )
    try:
        document, etag = store.read_assignment("sas", "frame-a")
    finally:
        store.bs.download_json_with_etag = original
    assert document["device_id"] == "frame-a" and document["last_revision"] == 0
    assert etag == '"etag"'


def test_pending_preserves_transport_crc_and_opaque_key():
    pending = store.pending_from_pick("frame-a", 7, _pick())
    assert pending["content_crc32"] == 0x89ABCDEF
    assert pending["image_key"] == store.image_key("photo-1")
    assert pending["image_key"] != pending["image_id"]


def test_selection_metadata_omits_unknown_crc_but_keeps_zero():
    assert "content_crc32" not in store.selection_blob_metadata(
        {"permanent": True, "content_crc32": None}
    )
    assert store.selection_blob_metadata(
        {"permanent": True, "content_crc32": 0}
    )["content_crc32"] == "0"


def test_install_pending_allocates_monotonic_revision():
    document = {"last_revision": 0xFFFFFFFF, "current": None, "journal": []}
    pending = store.install_pending(document, "frame-a", _pick())
    assert pending is not None and pending["revision"] == 1
    assert document["last_revision"] == 1


def test_supersede_keeps_recent_revision_and_dequeues_one_shot():
    document = {"current": store.pending_from_pick("frame-a", 8, _pick(from_queue=True))}
    removed: list[tuple[str, str]] = []
    original = store.queue_remove
    store.queue_remove = lambda sas, image_id: removed.append((sas, image_id))
    try:
        store.supersede_current(document, dequeue_one_shot_sas="sas")
    finally:
        store.queue_remove = original
    assert document["current"] is None
    assert document["journal"][0]["state"] == "superseded"
    assert removed == [("sas", "photo-1")]


def test_assignment_liveness_checks_blob_and_expiry():
    record = store.pending_from_pick("frame-a", 3, _pick())
    blob = store.image_name("photo-1", store.G16P_EXT)
    now = datetime(2026, 7, 1, tzinfo=timezone.utc)
    assert store.assignment_is_live(record, {blob: {}}, at=now)
    assert not store.assignment_is_live(record, {}, at=now)
    record["sel_meta"]["expires_at"] = (now - timedelta(seconds=1)).isoformat()
    assert not store.assignment_is_live(record, {blob: {}}, at=now)


def test_repeated_planning_performs_no_display_writes():
    blob_name = store.image_name("photo-1", store.G16P_EXT)
    blobs = {
        blob_name: {
            "permanent": "1",
            "format": "g16z",
            "content_crc32": "123",
        }
    }
    calls: list[str] = []
    originals = store.write_schedule, store.queue_remove, store.bs.set_blob_metadata
    store.write_schedule = lambda *_args, **_kwargs: calls.append("schedule")
    store.queue_remove = lambda *_args, **_kwargs: calls.append("queue")
    store.bs.set_blob_metadata = lambda *_args, **_kwargs: calls.append("metadata")
    try:
        for _ in range(5):
            pick = store.plan_next("sas", blobs=blobs, queue=[], countdown=0)
            assert pick is not None and pick.image_id == "photo-1"
    finally:
        store.write_schedule, store.queue_remove, store.bs.set_blob_metadata = originals
    assert calls == []


def test_backfill_computed_zero_is_stamped_once_by_key_presence():
    blob_name = store.image_name("photo-1", store.G16P_EXT)
    metadata = {blob_name: {"permanent": "1", "format": "g16z"}}
    downloads: list[str] = []
    stamps: list[dict] = []
    originals = (
        backfill_crc.bs.list_blobs_with_metadata,
        backfill_crc.bs.download_blob,
        backfill_crc.bs.set_blob_metadata,
    )
    backfill_crc.bs.list_blobs_with_metadata = lambda _sas, _prefix: metadata
    backfill_crc.bs.download_blob = lambda _sas, name: downloads.append(name) or b""

    def _stamp(_sas, name, updated):
        stamps.append(dict(updated))
        metadata[name] = dict(updated)

    backfill_crc.bs.set_blob_metadata = _stamp
    try:
        assert backfill_crc.backfill_device("sas") == (1, 0)
        assert backfill_crc.backfill_device("sas") == (0, 1)
    finally:
        (
            backfill_crc.bs.list_blobs_with_metadata,
            backfill_crc.bs.download_blob,
            backfill_crc.bs.set_blob_metadata,
        ) = originals
    assert downloads == [blob_name]
    assert stamps[0]["content_crc32"] == "0"


def test_commit_displayed_replays_absolute_effects():
    pending = store.pending_from_pick("frame-a", 5, _pick(from_queue=True))
    pending["displayed_at"] = "2026-07-01T12:00:00+00:00"
    metadata_calls: list[dict] = []
    queue_calls: list[str] = []
    schedule_calls: list[int] = []
    originals = store.bs.set_blob_metadata, store.queue_remove, store.write_schedule
    store.bs.set_blob_metadata = lambda _sas, _name, metadata: metadata_calls.append(metadata)
    store.queue_remove = lambda _sas, image_id: queue_calls.append(image_id)
    store.write_schedule = lambda _sas, value: schedule_calls.append(value)
    try:
        store.commit_displayed("sas", pending)
        store.commit_displayed("sas", pending)
    finally:
        store.bs.set_blob_metadata, store.queue_remove, store.write_schedule = originals
    assert metadata_calls[0] == metadata_calls[1]
    assert metadata_calls[0]["served_at"] == pending["displayed_at"]
    assert queue_calls == ["photo-1", "photo-1"]
    assert schedule_calls == [3, 3]


def test_invalidate_clears_assignments_but_retains_revision_high_water():
    document = {
        "schema": 1,
        "device_id": "frame-a",
        "committed_revision": 4,
        "last_revision": 8,
        "current": {"revision": 8},
        "journal": [{"revision": 7}],
    }
    writes: list[dict] = []
    originals = store.read_assignment, store.write_assignment
    store.read_assignment = lambda _sas, _device: (document, "etag-a")
    store.write_assignment = (
        lambda _sas, _device, value, *, etag: writes.append((dict(value), etag))
    )
    try:
        store.invalidate_assignment("sas", "frame-a")
    finally:
        store.read_assignment, store.write_assignment = originals
    saved, etag = writes[0]
    assert saved["current"] is None and saved["journal"] == []
    assert saved["last_revision"] == 8 and etag == "etag-a"


def test_supersede_retains_depth_two_journal():
    document = _document_with_current(3)
    document["journal"] = [
        {"revision": 2, "state": "superseded"},
        {"revision": 1, "state": "superseded"},
    ]
    store.supersede_current(document)
    assert document["current"] is None
    assert [entry["revision"] for entry in document["journal"]] == [3, 2]
    assert all(entry["state"] == "superseded" for entry in document["journal"])


def _document_with_current(revision: int = 1) -> dict:
    return {
        "schema": 1,
        "device_id": "frame-a",
        "committed_revision": 0,
        "last_revision": revision,
        "current": store.pending_from_pick("frame-a", revision, _pick()),
        "journal": [],
    }


def test_ack_commits_once_and_preserves_successor_on_duplicate():
    document = _document_with_current()
    effects: list[int] = []
    original = store.commit_displayed
    store.commit_displayed = lambda _sas, record: effects.append(record["revision"])
    image_key = document["current"]["image_key"]
    try:
        current, accepted, changed = store.commit_assignment(
            "sas", "frame-a", document, 1, image_key, _pick
        )
        duplicate, duplicate_accepted, duplicate_changed = store.commit_assignment(
            "sas", "frame-a", document, 1, image_key, _pick
        )
    finally:
        store.commit_displayed = original
    assert accepted and duplicate_accepted
    assert changed and not duplicate_changed and effects == [1]
    assert current is not None and current["revision"] == 2
    assert duplicate == current and document["committed_revision"] == 1


def test_committing_one_device_does_not_mutate_another_device():
    sas_a = "sas-a"
    sas_b = "sas-b"
    document_a = _document_with_current()
    document_a["current"].update(from_queue=True, planned_countdown=3)
    document_b = {
        "schema": 1,
        "device_id": "frame-b",
        "committed_revision": 4,
        "last_revision": 5,
        "current": store.pending_from_pick("frame-b", 5, _pick()),
        "journal": [],
    }
    queues = {sas_a: ["photo-1"], sas_b: ["photo-b"]}
    schedules = {sas_a: 9, sas_b: 7}
    metadata = {
        sas_a: {"permanent": True, "last_shown_at": "2026-06-01T00:00:00+00:00"},
        sas_b: {"permanent": True, "last_shown_at": "2026-06-02T00:00:00+00:00"},
    }
    device_b_before = copy.deepcopy(document_b)
    queue_b_before = list(queues[sas_b])
    schedule_b_before = schedules[sas_b]
    metadata_b_before = copy.deepcopy(metadata[sas_b])
    originals = store.bs.set_blob_metadata, store.queue_remove, store.write_schedule

    def _set_metadata(sas, _name, updated):
        metadata[sas] = copy.deepcopy(updated)

    def _remove(sas, image_id):
        queues[sas] = [value for value in queues[sas] if value != image_id]

    store.bs.set_blob_metadata = _set_metadata
    store.queue_remove = _remove
    store.write_schedule = lambda sas, value: schedules.__setitem__(sas, value)
    try:
        current, accepted, changed = store.commit_assignment(
            sas_a,
            "frame-a",
            document_a,
            1,
            document_a["current"]["image_key"],
            lambda: None,
        )
    finally:
        store.bs.set_blob_metadata, store.queue_remove, store.write_schedule = originals

    assert accepted and changed and current is None
    assert document_a["committed_revision"] == 1
    assert queues[sas_a] == [] and schedules[sas_a] == 3
    assert metadata[sas_a]["last_shown_at"] != "2026-06-01T00:00:00+00:00"
    assert document_b == device_b_before
    assert queues[sas_b] == queue_b_before
    assert schedules[sas_b] == schedule_b_before
    assert metadata[sas_b] == metadata_b_before


def test_dead_uncommitted_ack_is_rejected_before_effects():
    document = _document_with_current()
    effects: list[int] = []
    original = store.commit_displayed
    store.commit_displayed = lambda _sas, record: effects.append(record["revision"])
    try:
        current, accepted, changed = store.commit_assignment(
            "sas",
            "frame-a",
            document,
            1,
            document["current"]["image_key"],
            _pick,
            lambda _record: False,
        )
    finally:
        store.commit_displayed = original
    assert current is None and not accepted and not changed
    assert effects == [] and document["committed_revision"] == 0


def test_late_superseded_ack_does_not_replace_current():
    document = _document_with_current(2)
    superseded = store.pending_from_pick("frame-a", 1, _pick(from_queue=True))
    superseded["state"] = "superseded"
    document["journal"] = [superseded]
    effects: list[int] = []
    original = store.commit_displayed
    store.commit_displayed = lambda _sas, record: effects.append(record["revision"])
    try:
        current, accepted, changed = store.commit_assignment(
            "sas", "frame-a", document, 1, superseded["image_key"], _pick
        )
    finally:
        store.commit_displayed = original
    assert accepted and changed and effects == [1]
    assert current is not None and current["revision"] == 2
    assert document["current"]["revision"] == 2
    assert document["journal"][0]["state"] == "committed"


def test_crash_before_assignment_marker_replays_absolute_effects():
    persisted = _document_with_current()
    effects: list[str] = []
    original = store.commit_displayed
    store.commit_displayed = lambda _sas, record: effects.append(record["displayed_at"])
    image_key = persisted["current"]["image_key"]
    try:
        abandoned = copy.deepcopy(persisted)
        _current, accepted, changed = store.commit_assignment(
            "sas", "frame-a", abandoned, 1, image_key, _pick
        )
        assert accepted and changed
        current, accepted, changed = store.commit_assignment(
            "sas", "frame-a", persisted, 1, image_key, _pick
        )
    finally:
        store.commit_displayed = original
    assert accepted and changed and current is not None and current["revision"] == 2
    assert effects == [effects[0], effects[0]]
    assert persisted["committed_revision"] == 1


def _collect_tests():
    return [
        (name, obj)
        for name, obj in sorted(globals().items())
        if name.startswith("test_") and callable(obj)
    ]


def main() -> int:
    failures = 0
    for name, fn in _collect_tests():
        try:
            fn()
            print(f"ok   {name}")
        except Exception as exc:  # noqa: BLE001
            failures += 1
            print(f"FAIL {name}: {exc!r}")
    total = len(_collect_tests())
    print(f"\n{total - failures}/{total} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())