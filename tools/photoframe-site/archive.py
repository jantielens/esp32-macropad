"""Checksummed device and site archive export/import operations."""

from __future__ import annotations

import hashlib
import io
import json
import os
import re
import secrets
import shutil
import stat
import tempfile
import zipfile
import zlib
from pathlib import Path, PurePosixPath

import config as cfg

SCHEMA_VERSION = 1
SITE_VERSION = "1.23.0"
MAX_ARCHIVE_BYTES = 128 * 1024 * 1024
MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_TOTAL_BYTES = 128 * 1024 * 1024
MAX_FILES = 10_000
MANIFEST_NAME = "manifest.json"
_DEVICE_ID_RE = re.compile(rf"^{cfg.DEVICE_ID_PATTERN}$")
_DRIVE_RE = re.compile(r"^[A-Za-z]:")


class ArchiveError(ValueError):
    pass


def _json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _raw_config(root: Path) -> tuple[dict, dict]:
    try:
        frames = json.loads((root / "config/frames.json").read_text(encoding="utf-8"))
        users = json.loads((root / "config/users.json").read_text(encoding="utf-8"))
    except (FileNotFoundError, ValueError) as exc:
        raise ArchiveError("Site configuration is missing or invalid.") from exc
    return frames, users


def _archive(entries: dict[str, bytes], *, bundle_type: str, device_id: str | None,
             device_count: int, image_count: int) -> bytes:
    checksums = {name: hashlib.sha256(payload).hexdigest() for name, payload in entries.items()}
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "site_version": SITE_VERSION,
        "type": bundle_type,
        "device_id": device_id,
        "counts": {"devices": device_count, "images": image_count, "files": len(entries)},
        "files": checksums,
    }
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for name, payload in sorted(entries.items()):
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | 0o600) << 16
            bundle.writestr(info, payload)
        info = zipfile.ZipInfo(MANIFEST_NAME)
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = (stat.S_IFREG | 0o600) << 16
        bundle.writestr(info, _json_bytes(manifest))
    return output.getvalue()


def export_device(data_root: str | Path, device_id: str, owner_email: str) -> bytes:
    with cfg.data_transaction_lock():
        return _export_device(Path(data_root), device_id, owner_email)


def _export_device(root: Path, device_id: str, owner_email: str) -> bytes:
    if not _DEVICE_ID_RE.fullmatch(device_id):
        raise ArchiveError("Invalid device ID.")
    frame_data, _user_data = _raw_config(root)
    frame = (frame_data.get("frames") or {}).get(device_id)
    if not isinstance(frame, dict):
        raise ArchiveError("Device not found.")
    entries = {
        "device/frame.json": _json_bytes({"device_id": device_id, "frame": frame}),
        "device/ownership.json": _json_bytes({"owner": owner_email}),
    }
    namespace = root / "devices" / device_id
    if namespace.exists():
        for path in sorted(namespace.rglob("*")):
            if path.is_file():
                entries[f"device/namespace/{path.relative_to(namespace).as_posix()}"] = path.read_bytes()
    image_count = sum(1 for name in entries if name.endswith("/sidecar.json"))
    return _archive(entries, bundle_type="device", device_id=device_id,
                    device_count=1, image_count=image_count)


def export_site(data_root: str | Path) -> bytes:
    with cfg.data_transaction_lock():
        return _export_site(Path(data_root))


def _export_site(root: Path) -> bytes:
    entries: dict[str, bytes] = {}
    for path in sorted(root.rglob("*")):
        if (path.is_file() and ".tmp" not in path.name
            and not path.name.endswith((".pending.json", ".lock"))):
            entries[f"data/{path.relative_to(root).as_posix()}"] = path.read_bytes()
    frames, _users = _raw_config(root)
    device_count = len(frames.get("frames") or {})
    image_count = sum(1 for name in entries if name.endswith("/sidecar.json"))
    return _archive(entries, bundle_type="site", device_id=None,
                    device_count=device_count, image_count=image_count)


def _safe_name(info: zipfile.ZipInfo) -> str:
    name = info.filename
    if (not name or "\\" in name or name.startswith(("/", "//"))
            or _DRIVE_RE.match(name) or info.is_dir()):
        raise ArchiveError(f"Unsafe archive entry: {name!r}")
    path = PurePosixPath(name)
    if any(part in ("", ".", "..") for part in path.parts):
        raise ArchiveError(f"Unsafe archive entry: {name!r}")
    mode = info.external_attr >> 16
    file_type = stat.S_IFMT(mode)
    if file_type not in (0, stat.S_IFREG):
        raise ArchiveError(f"Non-regular archive entry: {name!r}")
    if info.flag_bits & 0x1:
        raise ArchiveError("Encrypted archives are not supported.")
    if info.file_size > MAX_FILE_BYTES:
        raise ArchiveError(f"Archive entry is too large: {name!r}")
    return path.as_posix()


def _validated_entries(payload: bytes, expected_type: str) -> tuple[dict, dict[str, bytes]]:
    if len(payload) > MAX_ARCHIVE_BYTES:
        raise ArchiveError("Archive exceeds the size limit.")
    try:
        bundle = zipfile.ZipFile(io.BytesIO(payload))
    except (zipfile.BadZipFile, OSError) as exc:
        raise ArchiveError("Archive is not a valid ZIP file.") from exc
    with bundle:
        infos = bundle.infolist()
        if len(infos) > MAX_FILES:
            raise ArchiveError("Archive contains too many files.")
        names: dict[str, zipfile.ZipInfo] = {}
        total = 0
        for info in infos:
            name = _safe_name(info)
            if name in names:
                raise ArchiveError(f"Duplicate archive entry: {name}")
            names[name] = info
            total += info.file_size
            if total > MAX_TOTAL_BYTES:
                raise ArchiveError("Archive expands beyond the size limit.")
        manifest_info = names.get(MANIFEST_NAME)
        if manifest_info is None:
            raise ArchiveError("Archive manifest is missing.")
        try:
            manifest = json.loads(bundle.read(manifest_info).decode("utf-8"))
        except (UnicodeDecodeError, ValueError) as exc:
            raise ArchiveError("Archive manifest is invalid.") from exc
        if not isinstance(manifest, dict) or manifest.get("schema_version") != SCHEMA_VERSION:
            raise ArchiveError("Archive schema version is unsupported.")
        if manifest.get("site_version") != SITE_VERSION:
            raise ArchiveError("Archive site version is unsupported.")
        if manifest.get("type") != expected_type:
            raise ArchiveError(f"Expected a {expected_type} bundle.")
        checksums = manifest.get("files")
        if not isinstance(checksums, dict):
            raise ArchiveError("Archive checksum table is invalid.")
        payload_names = set(names) - {MANIFEST_NAME}
        if set(checksums) != payload_names:
            raise ArchiveError("Archive file set does not match the manifest.")
        entries = {}
        for name in sorted(payload_names):
            value = bundle.read(names[name])
            if not secrets.compare_digest(hashlib.sha256(value).hexdigest(), str(checksums[name])):
                raise ArchiveError(f"Archive checksum mismatch: {name}")
            entries[name] = value
    counts = manifest.get("counts")
    try:
        file_count = int(counts.get("files", -1)) if isinstance(counts, dict) else -1
        declared_images = int(counts.get("images", -1)) if isinstance(counts, dict) else -1
        declared_devices = int(counts.get("devices", -1)) if isinstance(counts, dict) else -1
    except (TypeError, ValueError) as exc:
        raise ArchiveError("Archive counts are invalid.") from exc
    if file_count != len(entries):
        raise ArchiveError("Archive file count does not match the manifest.")
    image_count = sum(1 for name in entries if name.endswith("/sidecar.json"))
    if declared_images != image_count:
        raise ArchiveError("Archive image count does not match the manifest.")
    if expected_type == "device":
        device_count = 1
    else:
        try:
            frame_data = json.loads(entries["data/config/frames.json"].decode("utf-8"))
            device_count = len(frame_data.get("frames") or {})
        except (KeyError, UnicodeDecodeError, ValueError, AttributeError) as exc:
            raise ArchiveError("Site device configuration is invalid.") from exc
    if declared_devices != device_count:
        raise ArchiveError("Archive device count does not match the manifest.")
    _verify_transport_crcs(entries, expected_type)
    return manifest, entries


def _verify_transport_crcs(entries: dict[str, bytes], bundle_type: str) -> None:
    marker = "device/namespace/images/" if bundle_type == "device" else "data/devices/"
    for name, payload in entries.items():
        if not name.endswith("/sidecar.json") or marker not in name:
            continue
        try:
            sidecar = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, ValueError) as exc:
            raise ArchiveError(f"Invalid image sidecar: {name}") from exc
        if not isinstance(sidecar, dict):
            raise ArchiveError(f"Invalid image sidecar: {name}")
        parent = name.rsplit("/", 1)[0]
        variants = sidecar.get("variants", [])
        if not isinstance(variants, list) or any(not isinstance(item, dict) for item in variants):
            raise ArchiveError(f"Invalid image variants: {name}")
        for variant in variants:
            blob_name = str(variant.get("blob_name", ""))
            if (not blob_name or PurePosixPath(blob_name).name != blob_name
                    or blob_name in (".", "..")):
                raise ArchiveError(f"Invalid transport path in {name}")
            blob_path = f"{parent}/{blob_name}"
            transport = entries.get(blob_path)
            if transport is None:
                raise ArchiveError(f"Missing transport blob: {blob_path}")
            expected = str(variant.get("content_crc32", ""))
            actual = f"{zlib.crc32(transport) & 0xffffffff:08x}"
            if not secrets.compare_digest(expected, actual):
                raise ArchiveError(f"Transport CRC mismatch: {blob_path}")


def _extract(entries: dict[str, bytes], root: Path) -> None:
    for name, payload in entries.items():
        destination = root.joinpath(*PurePosixPath(name).parts)
        resolved = destination.resolve()
        extraction_root = root.resolve()
        if extraction_root not in resolved.parents:
            raise ArchiveError(f"Archive path escapes extraction root: {name}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(payload)


def _device_record(manifest: dict, entries: dict[str, bytes]) -> tuple[str, dict]:
    try:
        value = json.loads(entries["device/frame.json"].decode("utf-8"))
    except (KeyError, UnicodeDecodeError, ValueError) as exc:
        raise ArchiveError("Device profile is missing or invalid.") from exc
    device_id = str(value.get("device_id", "")) if isinstance(value, dict) else ""
    if (not _DEVICE_ID_RE.fullmatch(device_id)
            or device_id != manifest.get("device_id")):
        raise ArchiveError("Bundle device ID is invalid or inconsistent.")
    frame = value.get("frame")
    if not isinstance(frame, dict):
        raise ArchiveError("Device profile is invalid.")
    profile = frame.get("profile") or {}
    try:
        codes = tuple(int(code) for code in profile.get("format_codes", []))
        cfg._validate_profile(int(profile.get("width", 0)), int(profile.get("height", 0)), codes)
    except (TypeError, ValueError, cfg.ConfigError) as exc:
        raise ArchiveError("Device profile is invalid.") from exc
    if len(str(frame.get("token", "")).encode("utf-8")) < 16:
        raise ArchiveError("Device token is invalid.")
    return device_id, frame


def inspect_device_archive(payload: bytes) -> str:
    manifest, entries = _validated_entries(payload, "device")
    device_id, _frame = _device_record(manifest, entries)
    return device_id


def import_device(data_root: str | Path, payload: bytes, owner_email: str,
                  expected_device_id: str | None = None) -> tuple[cfg.Config, str]:
    root = Path(data_root)
    manifest, entries = _validated_entries(payload, "device")
    device_id, frame = _device_record(manifest, entries)
    if expected_device_id is not None and device_id != expected_device_id:
        raise ArchiveError("Bundle device ID does not match the selected device.")
    current_frames, current_users = _raw_config(root)
    current_owner = current_users.get("users", {}).get(owner_email.lower())
    if not isinstance(current_owner, dict):
        raise ArchiveError("Importing administrator account was not found.")
    current_owned = [str(item) for item in current_owner.get(
        "frames", current_owner.get("devices", []))]
    if device_id in current_frames.get("frames", {}) and device_id not in current_owned:
        raise ArchiveError("Cannot replace a device owned by another user.")
    with tempfile.TemporaryDirectory(prefix="photoframe-device-", dir=root.parent) as temporary:
        extracted = Path(temporary)
        _extract(entries, extracted)
        source = extracted / "device/namespace"
        staged = root / "devices" / f".{device_id}.import-{secrets.token_hex(4)}"
        target = root / "devices" / device_id
        backup = root / "devices" / f".{device_id}.backup-{secrets.token_hex(4)}"
        with cfg._config_transaction_lock(root):
            frame_data, user_data = _raw_config(root)
            frames = frame_data.setdefault("frames", {})
            users = user_data.setdefault("users", {})
            owner = users.get(owner_email.lower())
            if not isinstance(owner, dict):
                raise ArchiveError("Importing administrator account was not found.")
            owner_devices = [str(item) for item in owner.get("frames", owner.get("devices", []))]
            if device_id in frames and device_id not in owner_devices:
                raise ArchiveError("Cannot replace a device owned by another user.")
            if any(str(item.get("token", "")) == str(frame.get("token", "")) and key != device_id
                   for key, item in frames.items() if isinstance(item, dict)):
                raise ArchiveError("Device token conflicts with another device.")
            staged.parent.mkdir(parents=True, exist_ok=True)
            if source.exists():
                shutil.copytree(source, staged)
            else:
                (staged / "images").mkdir(parents=True)
                (staged / "state").mkdir(parents=True)
            frames[device_id] = frame
            for entry in users.values():
                if isinstance(entry, dict):
                    owned = [str(item) for item in entry.get("frames", entry.get("devices", []))]
                    entry["frames"] = [item for item in owned if item != device_id]
                    entry.pop("devices", None)
            owner["frames"] = list(owner.get("frames", [])) + [device_id]
            validation = extracted / "validation/config"
            validation.mkdir(parents=True)
            (validation / "frames.json").write_bytes(_json_bytes(frame_data))
            (validation / "users.json").write_bytes(_json_bytes(user_data))
            try:
                cfg.load_config(validation.parent)
            except cfg.ConfigError as exc:
                raise ArchiveError(f"Imported device configuration is invalid: {exc}") from exc
            cfg._commit_config_transaction(root, frame_data, user_data, namespace={
                "operation": "replace",
                "staged": staged.relative_to(root).as_posix(),
                "target": target.relative_to(root).as_posix(),
                "backup": backup.relative_to(root).as_posix(),
            }, lock_held=True)
    return cfg.load_config(root), device_id


def import_site(data_root: str | Path, payload: bytes) -> None:
    root = Path(data_root)
    _manifest, entries = _validated_entries(payload, "site")
    with tempfile.TemporaryDirectory(prefix="photoframe-site-", dir=root.parent) as temporary:
        extracted = Path(temporary)
        _extract(entries, extracted)
        staged = extracted / "data"
        _validate_site(staged)
        suffix = secrets.token_hex(4)
        staged_root = root.with_name(f".{root.name}.import-{suffix}")
        backup = root.with_name(f".{root.name}.backup-{suffix}")
        journal = _site_journal(root)
        with cfg.data_transaction_lock():
            recover_site_import(root)
            os.replace(staged, staged_root)
            cfg._fsync_directory(root.parent)
            cfg._atomic_write_json(journal, {
                "staged": staged_root.name,
                "backup": backup.name,
            })
            _complete_site_import(root)


def _validate_site(root: Path) -> cfg.Config:
    try:
        frame_data, user_data = _raw_config(root)
        secret = (root / "config/session-secret").read_text(encoding="utf-8").strip()
    except (cfg.ConfigError, FileNotFoundError, UnicodeError) as exc:
        raise ArchiveError(f"Imported site configuration is invalid: {exc}") from exc
    raw_users = user_data.get("users") or {}
    if not isinstance(raw_users, dict) or len(raw_users) != 1:
        raise ArchiveError("Site bundles must contain exactly one administrator account.")
    raw_email = next(iter(raw_users))
    if raw_email != raw_email.strip().lower() or "@" not in raw_email or len(raw_email) > 254:
        raise ArchiveError("Site administrator email is invalid or non-canonical.")
    try:
        configured = cfg.load_config(root)
    except cfg.ConfigError as exc:
        raise ArchiveError(f"Imported site configuration is invalid: {exc}") from exc
    if len(configured.users) != 1:
        raise ArchiveError("Site bundles must contain exactly one administrator account.")
    owners = {device_id: 0 for device_id in configured.frames}
    for email, user in configured.users.items():
        raw_user = raw_users.get(email)
        if not isinstance(raw_user, dict) or not cfg.valid_password_hash(str(raw_user.get("password_hash", ""))):
            raise ArchiveError(f"User {email!r} has an invalid password hash.")
        if len(set(user.devices)) != len(user.devices):
            raise ArchiveError(f"User {email!r} contains duplicate device ownership.")
        for device_id in user.devices:
            if device_id not in configured.frames:
                raise ArchiveError(f"User {email!r} references an unknown device.")
            owners[device_id] += 1
    if any(count != 1 for count in owners.values()):
        raise ArchiveError("Every device must have exactly one owner.")
    if len(secret) < 32:
        raise ArchiveError("Imported site session secret is invalid.")
    if len(frame_data.get("frames") or {}) != len(configured.frames):
        raise ArchiveError("Imported site frame configuration is invalid.")
    return configured


def _site_journal(root: Path) -> Path:
    return root.parent / f".{root.name}.site-import.pending.json"


def _journal_sibling(root: Path, value: object) -> Path:
    name = str(value)
    if not name or Path(name).name != name or not name.startswith(f".{root.name}."):
        raise ArchiveError("Site import journal contains an invalid path.")
    return root.parent / name


def _complete_site_import(root: Path) -> None:
    journal = _site_journal(root)
    transaction = cfg._read_object(journal)
    staged = _journal_sibling(root, transaction.get("staged"))
    backup = _journal_sibling(root, transaction.get("backup"))
    if staged.exists():
        if root.exists():
            if backup.exists():
                raise ArchiveError("Site import recovery found conflicting roots.")
            os.replace(root, backup)
            cfg._fsync_directory(root.parent)
        os.replace(staged, root)
        cfg._fsync_directory(root.parent)
    elif not root.exists() and backup.exists():
        os.replace(backup, root)
        cfg._fsync_directory(root.parent)
    if not root.exists():
        raise ArchiveError("Site import recovery could not restore the data root.")
    shutil.rmtree(backup, ignore_errors=True)
    shutil.rmtree(staged, ignore_errors=True)
    journal.unlink(missing_ok=True)
    cfg._fsync_directory(root.parent)


def recover_site_import(data_root: str | Path) -> None:
    root = Path(data_root)
    journal = _site_journal(root)
    if not journal.exists():
        return
    with cfg.data_transaction_lock():
        if journal.exists():
            _complete_site_import(root)
