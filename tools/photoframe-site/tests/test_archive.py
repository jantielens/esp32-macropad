"""Device/site archive round-trip, integrity, and extraction-safety tests."""

from __future__ import annotations

import hashlib
import io
import json
import os
import stat
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import archive  # noqa: E402
import config  # noqa: E402


TOKEN = "0123456789abcdef0123456789abcdef"
PASSWORD = "a-secure-password"
OWNER = "owner@example.com"
TRANSPORT = b"G16P" + bytes(range(32))


def _site(*, with_device: bool) -> Path:
    root = Path(tempfile.mkdtemp())
    (root / "config").mkdir()
    frames = {}
    owned = []
    if with_device:
        frames["device-one"] = {
            "display_name": "Device One",
            "token": TOKEN,
            "profile": {"width": 8, "height": 4, "format_codes": [2]},
        }
        owned.append("device-one")
    (root / "config/frames.json").write_text(json.dumps({"frames": frames}))
    (root / "config/users.json").write_text(json.dumps({"users": {
        OWNER: {"password_hash": config.hash_password(PASSWORD), "frames": owned}
    }}))
    (root / "config/session-secret").write_text("s" * 64 + "\n")
    if with_device:
        image = root / "devices/device-one/images/image-one"
        image.mkdir(parents=True)
        (image / "source.bin").write_bytes(b"source-original")
        (image / "transport-8x4-2.g16p").write_bytes(TRANSPORT)
        (image / "thumb.png").write_bytes(b"thumbnail")
        (image / "sidecar.json").write_text(json.dumps({
            "id": "image-one",
            "source_name": "source.bin",
            "permanent": True,
            "uploaded_at": "2026-01-01T00:00:00+00:00",
            "variants": [{
                "width": 8, "height": 4, "format_code": 2,
                "profile_key": "fixture",
                "blob_name": "transport-8x4-2.g16p",
                "content_length": len(TRANSPORT),
                "content_crc32": f"{zlib.crc32(TRANSPORT) & 0xffffffff:08x}",
            }],
        }))
        state = root / "devices/device-one/state"
        state.mkdir()
        (state / "settings.json").write_text('{"fresh_window_days":9}')
    return root


def _entries(payload: bytes) -> tuple[dict, dict[str, bytes]]:
    with zipfile.ZipFile(io.BytesIO(payload)) as bundle:
        values = {info.filename: bundle.read(info) for info in bundle.infolist()}
    return json.loads(values.pop("manifest.json")), values


def _bundle(manifest: dict, entries: dict[str, bytes], *, special: tuple[str, bytes, int] | None = None) -> bytes:
    manifest = dict(manifest)
    manifest["files"] = {name: hashlib.sha256(value).hexdigest() for name, value in entries.items()}
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as bundle:
        for name, value in entries.items():
            bundle.writestr(name, value)
        if special:
            name, value, mode = special
            info = zipfile.ZipInfo(name)
            info.external_attr = mode << 16
            bundle.writestr(info, value)
        bundle.writestr("manifest.json", json.dumps(manifest))
    return output.getvalue()


def _snapshot(root: Path) -> dict[str, bytes]:
    return {path.relative_to(root).as_posix(): path.read_bytes()
            for path in root.rglob("*") if path.is_file()}


def test_device_export_import_round_trip_preserves_bytes_crc_and_config() -> None:
    source = _site(with_device=True)
    payload = archive.export_device(source, "device-one", OWNER)
    target = _site(with_device=False)
    configured, device_id = archive.import_device(target, payload, OWNER)
    assert device_id == "device-one"
    assert configured.device(device_id).token == TOKEN
    imported = target / "devices/device-one/images/image-one"
    assert (imported / "source.bin").read_bytes() == b"source-original"
    assert (imported / "transport-8x4-2.g16p").read_bytes() == TRANSPORT
    sidecar = json.loads((imported / "sidecar.json").read_text())
    assert sidecar["variants"][0]["content_crc32"] == f"{zlib.crc32(TRANSPORT) & 0xffffffff:08x}"
    assert json.loads((target / "devices/device-one/state/settings.json").read_text()) == {
        "fresh_window_days": 9
    }


def test_site_export_import_round_trip_replaces_all_data() -> None:
    source = _site(with_device=True)
    payload = archive.export_site(source)
    target = _site(with_device=False)
    (target / "target-only.txt").write_text("remove me")
    archive.import_site(target, payload)
    assert not (target / "target-only.txt").exists()
    assert (target / "devices/device-one/images/image-one/transport-8x4-2.g16p").read_bytes() == TRANSPORT
    assert json.loads((target / "config/frames.json").read_text())["frames"]["device-one"]["token"] == TOKEN
    assert (target / "config/session-secret").read_text() == "s" * 64 + "\n"


def test_archive_schema_policy_is_explicit() -> None:
    assert archive.SCHEMA_VERSION == 1
    assert archive.SUPPORTED_IMPORT_SCHEMA_VERSIONS == (1,)


def test_different_site_version_warns_and_imports() -> None:
    source = _site(with_device=True)
    manifest, entries = _entries(archive.export_device(source, "device-one", OWNER))
    manifest["site_version"] = "1.22.0"
    target = _site(with_device=False)
    with mock.patch.object(archive.logger, "warning") as warning:
        configured, device_id = archive.import_device(target, _bundle(manifest, entries), OWNER)
    assert device_id == "device-one"
    assert configured.device(device_id).token == TOKEN
    warning.assert_called_once()


def test_newer_and_older_archive_schemas_are_rejected_clearly() -> None:
    source = _site(with_device=True)
    manifest, entries = _entries(archive.export_device(source, "device-one", OWNER))
    target = _site(with_device=False)

    manifest["schema_version"] = 2
    try:
        archive.import_device(target, _bundle(manifest, entries), OWNER)
        raise AssertionError("newer archive schema accepted")
    except archive.ArchiveError as exc:
        assert str(exc) == (
            "This archive was created by a newer version of the site; "
            "upgrade before importing."
        )

    manifest["schema_version"] = 0
    try:
        archive.import_device(target, _bundle(manifest, entries), OWNER)
        raise AssertionError("older archive schema accepted")
    except archive.ArchiveError as exc:
        assert str(exc) == (
            "This archive uses an older unsupported schema; export it from a "
            "compatible site before importing."
        )


def test_corrupt_checksum_and_crc_leave_target_unchanged() -> None:
    source = _site(with_device=True)
    original = archive.export_device(source, "device-one", OWNER)
    manifest, entries = _entries(original)
    target = _site(with_device=False)
    before = _snapshot(target)

    entries["device/namespace/images/image-one/transport-8x4-2.g16p"] = b"corrupt"
    raw_bad_checksum = io.BytesIO()
    with zipfile.ZipFile(raw_bad_checksum, "w") as bundle:
        for name, value in entries.items():
            bundle.writestr(name, value)
        bundle.writestr("manifest.json", json.dumps(manifest))
    try:
        archive.import_device(target, raw_bad_checksum.getvalue(), OWNER)
        raise AssertionError("checksum corruption accepted")
    except archive.ArchiveError:
        pass
    assert _snapshot(target) == before

    crc_bundle = _bundle(manifest, entries)
    try:
        archive.import_device(target, crc_bundle, OWNER)
        raise AssertionError("CRC corruption accepted")
    except archive.ArchiveError:
        pass
    assert _snapshot(target) == before


def test_zip_slip_symlink_and_bad_device_id_are_rejected_before_writes() -> None:
    source = _site(with_device=True)
    original = archive.export_device(source, "device-one", OWNER)
    manifest, entries = _entries(original)
    target = _site(with_device=False)
    before = _snapshot(target)
    escaped = target.parent / "escaped.txt"
    escaped.unlink(missing_ok=True)

    slip = _bundle(manifest, entries, special=("../escaped.txt", b"owned", stat.S_IFREG | 0o600))
    for malicious in (
        slip,
        _bundle(manifest, entries, special=("device/link", b"target", stat.S_IFLNK | 0o777)),
    ):
        try:
            archive.import_device(target, malicious, OWNER)
            raise AssertionError("unsafe archive accepted")
        except archive.ArchiveError:
            pass
        assert not escaped.exists()
        assert _snapshot(target) == before

    frame = json.loads(entries["device/frame.json"])
    frame["device_id"] = "../bad"
    entries["device/frame.json"] = json.dumps(frame).encode()
    manifest["device_id"] = "../bad"
    try:
        archive.import_device(target, _bundle(manifest, entries), OWNER)
        raise AssertionError("invalid device ID accepted")
    except archive.ArchiveError:
        pass
    assert _snapshot(target) == before


def test_malformed_variant_table_is_rejected() -> None:
    source = _site(with_device=True)
    manifest, entries = _entries(archive.export_device(source, "device-one", OWNER))
    sidecar_name = next(name for name in entries if name.endswith("/sidecar.json"))
    sidecar = json.loads(entries[sidecar_name])
    sidecar["variants"] = False
    entries[sidecar_name] = json.dumps(sidecar).encode()
    target = _site(with_device=False)
    before = _snapshot(target)
    try:
        archive.import_device(target, _bundle(manifest, entries), OWNER)
        raise AssertionError("malformed variants accepted")
    except archive.ArchiveError:
        pass
    assert _snapshot(target) == before


def test_device_import_cannot_take_over_another_users_device() -> None:
    source = _site(with_device=True)
    payload = archive.export_device(source, "device-one", OWNER)
    target = _site(with_device=True)
    users = json.loads((target / "config/users.json").read_text())
    users["users"]["other@example.com"] = {
        "password_hash": config.hash_password(PASSWORD), "frames": []
    }
    (target / "config/users.json").write_text(json.dumps(users))
    before = _snapshot(target)
    try:
        archive.import_device(target, payload, "other@example.com")
        raise AssertionError("cross-user device replacement accepted")
    except archive.ArchiveError:
        pass
    assert _snapshot(target) == before


def test_site_import_rejects_invalid_ownership_before_replacement() -> None:
    source = _site(with_device=True)
    manifest, entries = _entries(archive.export_site(source))
    users = json.loads(entries["data/config/users.json"])
    users["users"][OWNER]["frames"] = ["device-one", "unknown-device"]
    entries["data/config/users.json"] = json.dumps(users).encode()
    payload = _bundle(manifest, entries)
    target = _site(with_device=False)
    before = _snapshot(target)
    try:
        archive.import_site(target, payload)
        raise AssertionError("invalid ownership graph accepted")
    except archive.ArchiveError:
        pass
    assert _snapshot(target) == before


def test_interrupted_site_root_swap_is_recovered() -> None:
    root = _site(with_device=False)
    replacement = _site(with_device=True)
    frames = json.loads((replacement / "config/frames.json").read_text())
    frames["frames"]["device-one"]["display_name"] = "Recovered Site"
    (replacement / "config/frames.json").write_text(json.dumps(frames))
    staged = root.with_name(f".{root.name}.import-test")
    backup = root.with_name(f".{root.name}.backup-test")
    journal = archive._site_journal(root)
    os.replace(replacement, staged)
    config._atomic_write_json(journal, {"staged": staged.name, "backup": backup.name})
    os.replace(root, backup)
    archive.recover_site_import(root)
    assert config.load_config(root).frames["device-one"].display_name == "Recovered Site"
    assert not staged.exists()
    assert not backup.exists()
    assert not journal.exists()


if __name__ == "__main__":
    tests = [value for name, value in sorted(globals().items())
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
