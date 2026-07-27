"""File-based frame capabilities and independent human UI authentication."""

from __future__ import annotations

import fcntl
import hashlib
import hmac
import json
import os
import re
import secrets
import shutil
import threading
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

PBKDF2_ALGO = "sha256"
PBKDF2_ITERATIONS = 200_000
_HASH_PREFIX = "pbkdf2_sha256"

FORMAT_JPEG = 1
FORMAT_G16P = 2
FORMAT_G16Z = 3
SUPPORTED_FORMAT_CODES = (FORMAT_JPEG, FORMAT_G16P, FORMAT_G16Z)

MIN_TEMP_MIN_SPACING = 2
DEFAULT_TEMP_MIN_SPACING = 4
MIN_FRESH_WINDOW_DAYS = 0
DEFAULT_FRESH_WINDOW_DAYS = 7
MIN_MAX_TEMP_SHARE_PCT = 1
MAX_MAX_TEMP_SHARE_PCT = 100
DEFAULT_MAX_TEMP_SHARE_PCT = 50
DEFAULT_JPEG_QUALITY = 90
DEVICE_ID_PATTERN = r"[a-z0-9][a-z0-9-]{0,63}"
_CONFIG_MUTEX = threading.RLock()
_DATA_MUTEX = threading.RLock()


class ConfigError(RuntimeError):
    pass


def _write_once(path: Path, content: str, *, mode: int = 0o600) -> bool:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{secrets.token_hex(8)}.tmp")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        try:
            os.link(temporary, path)
            _fsync_directory(path.parent)
            return True
        except FileExistsError:
            return False
    finally:
        temporary.unlink(missing_ok=True)


def _fsync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def initialize_data_root(data_root: str | Path) -> None:
    config_root = Path(data_root) / "config"
    _write_once(config_root / "frames.json", '{"frames":{}}\n')
    _write_once(config_root / "users.json", '{"users":{}}\n')


def _validate_device_id(device_id: str) -> None:
    if not re.fullmatch(DEVICE_ID_PATTERN, device_id):
        raise ConfigError("Device ID must use lowercase letters, numbers, and hyphens.")


def _validate_profile(width: int, height: int, format_codes: tuple[int, ...]) -> None:
    if not 1 <= width <= 10_000 or not 1 <= height <= 10_000:
        raise ConfigError("Device dimensions must be between 1 and 10000 pixels.")
    if not format_codes or any(code not in SUPPORTED_FORMAT_CODES for code in format_codes):
        raise ConfigError("Select a supported output format.")
    if (FORMAT_G16P in format_codes or FORMAT_G16Z in format_codes) and width % 2:
        raise ConfigError("Gray16 output requires an even device width.")


def session_secret(data_root: str | Path) -> str:
    override = os.environ.get("SECRET_KEY")
    if override:
        return override
    path = Path(data_root) / "config" / "session-secret"
    _write_once(path, secrets.token_hex(32) + "\n")
    value = path.read_text(encoding="utf-8").strip()
    if len(value) < 32:
        raise ConfigError(f"invalid session secret: {path}")
    return value


def is_configured(value: Config) -> bool:
    return bool(value.users)


def setup_pending(data_root: str | Path) -> bool:
    return (Path(data_root) / "config" / "setup.pending.json").exists()


def _atomic_write_json(path: Path, value: dict) -> None:
    temporary = path.with_name(f".{path.name}.{secrets.token_hex(8)}.tmp")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(value, output, indent=2)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        _fsync_directory(path.parent)
    finally:
        temporary.unlink(missing_ok=True)


def _transaction_path(root: Path, value: str) -> Path:
    path = (root / value).resolve()
    resolved_root = root.resolve()
    if path == resolved_root or resolved_root not in path.parents:
        raise ConfigError("transaction path escapes the data root")
    return path


def _apply_config_transaction(config_root: Path, transaction: dict) -> None:
    namespace = transaction.get("namespace")
    if isinstance(namespace, dict):
        root = config_root.parent
        target = _transaction_path(root, str(namespace["target"]))
        backup = _transaction_path(root, str(namespace["backup"]))
        operation = str(namespace.get("operation", "replace"))
        if operation == "remove":
            if target.exists() and not backup.exists():
                os.replace(target, backup)
        elif operation == "replace":
            staged = _transaction_path(root, str(namespace["staged"]))
            if staged.exists():
                if target.exists() and not backup.exists():
                    os.replace(target, backup)
                os.replace(staged, target)
        else:
            raise ConfigError("invalid namespace transaction operation")
    _atomic_write_json(config_root / "frames.json", transaction["frame_data"])
    _atomic_write_json(config_root / "users.json", transaction["user_data"])


def _finish_config_transaction(config_root: Path, transaction: dict) -> None:
    namespace = transaction.get("namespace")
    if isinstance(namespace, dict):
        backup = _transaction_path(config_root.parent, str(namespace["backup"]))
        if backup.exists():
            shutil.rmtree(backup)


def _recover_config_transaction(root: Path) -> None:
    config_root = root / "config"
    pending_path = config_root / "config.pending.json"
    if not pending_path.exists():
        return
    with (config_root / "config.lock").open("a", encoding="utf-8") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if pending_path.exists():
            transaction = _read_object(pending_path)
            _apply_config_transaction(config_root, transaction)
            pending_path.unlink()
            _fsync_directory(config_root)
            _finish_config_transaction(config_root, transaction)


@contextmanager
def data_transaction_lock():
    with _DATA_MUTEX:
        yield


@contextmanager
def _config_transaction_lock(root: Path):
    config_root = root / "config"
    with data_transaction_lock():
        with _CONFIG_MUTEX:
            with (config_root / "config.lock").open("a", encoding="utf-8") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                yield


def _commit_config_transaction(root: Path, frame_data: dict, user_data: dict,
                               namespace: dict | None = None, *, lock_held: bool = False) -> None:
    config_root = root / "config"
    pending_path = config_root / "config.pending.json"
    def apply() -> None:
        transaction = {"frame_data": frame_data, "user_data": user_data}
        if namespace is not None:
            transaction["namespace"] = namespace
        _atomic_write_json(pending_path, transaction)
        _apply_config_transaction(config_root, transaction)
        pending_path.unlink()
        _fsync_directory(config_root)
        _finish_config_transaction(config_root, transaction)
    if lock_held:
        apply()
    else:
        with _config_transaction_lock(root):
            apply()


def initialize_site(
    data_root: str | Path,
    *,
    email: str,
    password: str,
    frame_id: str,
    width: int,
    height: int,
    format_codes: tuple[int, ...],
) -> tuple[Config, str]:
    root = Path(data_root)
    initialize_data_root(root)
    normalized_email = email.strip().lower()
    normalized_frame_id = frame_id.strip().lower()
    if "@" not in normalized_email or len(normalized_email) > 254:
        raise ConfigError("Enter a valid email address.")
    if len(password) < 12:
        raise ConfigError("Password must be at least 12 characters.")
    _validate_device_id(normalized_frame_id)
    _validate_profile(width, height, format_codes)

    config_root = root / "config"
    with (config_root / "setup.lock").open("a", encoding="utf-8") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        pending_path = config_root / "setup.pending.json"
        if pending_path.exists():
            transaction = _read_object(pending_path)
            _atomic_write_json(config_root / "frames.json", transaction["frame_data"])
            _atomic_write_json(config_root / "users.json", transaction["user_data"])
            pending_path.unlink()
            _fsync_directory(config_root)
            return load_config(root), str(transaction["token"])
        current = load_config(root)
        if current.frames or current.users:
            raise ConfigError("This site is already configured.")
        token = secrets.token_hex(32)
        frame_data = {"frames": {
            normalized_frame_id: {
                "token": token,
                "profile": {
                    "width": width,
                    "height": height,
                    "format_codes": list(format_codes),
                },
                "display_name": normalized_frame_id,
                "image_transform": {"rotate_deg": 0, "mirror_x": False, "mirror_y": False},
            }
        }}
        user_data = {"users": {
            normalized_email: {
                "password_hash": hash_password(password),
                "frames": [normalized_frame_id],
            }
        }}
        _atomic_write_json(pending_path, {
            "frame_data": frame_data,
            "user_data": user_data,
            "token": token,
        })
        _atomic_write_json(config_root / "frames.json", frame_data)
        _atomic_write_json(config_root / "users.json", user_data)
        pending_path.unlink()
        _fsync_directory(config_root)
        return load_config(root), token


@dataclass(frozen=True)
class Frame:
    frame_id: str
    token: str
    width: int
    height: int
    format_codes: tuple[int, ...]
    revoked: bool = False
    image_transform: dict | None = None
    jpeg_quality: int = DEFAULT_JPEG_QUALITY
    temp_min_spacing: int = DEFAULT_TEMP_MIN_SPACING
    fresh_window_days: int = DEFAULT_FRESH_WINDOW_DAYS
    max_temp_share_pct: int = DEFAULT_MAX_TEMP_SHARE_PCT
    display_name: str = ""

    @property
    def device_id(self) -> str:
        return self.frame_id

    @property
    def image_format(self) -> str:
        return {1: "jpeg", 2: "g16p", 3: "g16z"}[self.format_codes[0]]

    def variant_key(self, format_code: int) -> str:
        profile = {
            "format_code": format_code,
            "height": self.height,
            "image_transform": self.image_transform or {},
            "jpeg_quality": self.jpeg_quality if format_code == FORMAT_JPEG else None,
            "width": self.width,
        }
        encoded = json.dumps(profile, separators=(",", ":"), sort_keys=True).encode("utf-8")
        return hashlib.sha256(encoded).hexdigest()[:16]

    def variant_requirements(self) -> tuple[tuple[int, int, int, str], ...]:
        return tuple(
            (self.width, self.height, code, self.variant_key(code))
            for code in self.format_codes
        )


Device = Frame


@dataclass(frozen=True)
class User:
    email: str
    password_hash: str
    devices: tuple[str, ...]


@dataclass(frozen=True)
class Config:
    frames: dict[str, Frame]
    users: dict[str, User]

    @property
    def devices(self) -> dict[str, Frame]:
        return self.frames

    def device(self, device_id: str) -> Optional[Frame]:
        return self.frames.get(device_id)

    def user(self, email: str) -> Optional[User]:
        return self.users.get(email.lower())

    def authenticate_frame(self, token: str) -> Optional[Frame]:
        provided = token.encode("utf-8")
        matched = None
        for frame in self.frames.values():
            if hmac.compare_digest(provided, frame.token.encode("utf-8")):
                matched = frame
        return matched if matched is not None and not matched.revoked else None

    def required_variants(self) -> set[tuple[int, int, int]]:
        return {
            (frame.width, frame.height, code)
            for frame in self.frames.values()
            if not frame.revoked
            for code in frame.format_codes
        }

    def variant_requirements(self) -> set[tuple[int, int, int, str]]:
        return {
            (frame.width, frame.height, code, frame.variant_key(code))
            for frame in self.frames.values()
            if not frame.revoked
            for code in frame.format_codes
        }


def _read_object(path: Path, *, optional: bool = False) -> dict:
    if optional and not path.exists():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ConfigError(f"missing config file: {path}") from exc
    except ValueError as exc:
        raise ConfigError(f"invalid JSON in {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ConfigError(f"{path} must contain a JSON object")
    return value


def load_config(data_root: str | Path | None = None) -> Config:
    root = Path(data_root or os.environ.get("PHOTOFRAME_DATA_DIR", "data"))
    _recover_config_transaction(root)
    frame_data = _read_object(root / "config" / "frames.json")
    user_data = _read_object(root / "config" / "users.json", optional=True)

    frames = {}
    frame_tokens = set()
    entries = frame_data.get("frames", frame_data)
    for frame_id, entry in entries.items():
        if not isinstance(entry, dict):
            raise ConfigError(f"frame '{frame_id}' must be an object")
        _validate_device_id(frame_id)
        token = str(entry.get("token", ""))
        profile = entry.get("profile") or {}
        try:
            codes = tuple(int(code) for code in profile.get("format_codes", []))
            width, height = int(profile.get("width", 0)), int(profile.get("height", 0))
            jpeg_quality = int(entry.get("jpeg_quality", DEFAULT_JPEG_QUALITY))
        except (TypeError, ValueError) as exc:
            raise ConfigError(f"frame '{frame_id}' has invalid profile values") from exc
        if len(token.encode("utf-8")) < 16:
            raise ConfigError(f"frame '{frame_id}' token must contain at least 128 bits")
        if token in frame_tokens:
            raise ConfigError(f"frame '{frame_id}' reuses another frame token")
        frame_tokens.add(token)
        if not codes or len(set(codes)) != len(codes) or any(code not in SUPPORTED_FORMAT_CODES for code in codes):
            raise ConfigError(f"frame '{frame_id}' has invalid format_codes")
        if width <= 0 or height <= 0 or (FORMAT_G16P in codes or FORMAT_G16Z in codes) and width % 2:
            raise ConfigError(f"frame '{frame_id}' has invalid geometry")
        if not 1 <= jpeg_quality <= 100:
            raise ConfigError(f"frame '{frame_id}' has invalid jpeg_quality")
        frames[frame_id] = Frame(
            frame_id=frame_id,
            token=token,
            width=width,
            height=height,
            format_codes=codes,
            revoked=bool(entry.get("revoked", False)),
            image_transform=dict(entry.get("image_transform") or {}),
            jpeg_quality=jpeg_quality,
            temp_min_spacing=max(MIN_TEMP_MIN_SPACING, int(entry.get("temp_min_spacing", DEFAULT_TEMP_MIN_SPACING))),
            fresh_window_days=max(0, int(entry.get("fresh_window_days", DEFAULT_FRESH_WINDOW_DAYS))),
            max_temp_share_pct=min(100, max(1, int(entry.get("max_temp_share_pct", DEFAULT_MAX_TEMP_SHARE_PCT)))),
            display_name=str(entry.get("display_name") or frame_id),
        )

    users = {}
    for email, entry in (user_data.get("users", user_data)).items():
        if not isinstance(entry, dict) or not entry.get("password_hash"):
            raise ConfigError(f"user '{email}' needs a password_hash")
        users[email.lower()] = User(
            email=email.lower(),
            password_hash=str(entry["password_hash"]),
            devices=tuple(str(item) for item in entry.get("frames", entry.get("devices", []))),
        )
    return Config(frames=frames, users=users)


def _config_documents(root: Path) -> tuple[dict, dict]:
    frame_data = _read_object(root / "config" / "frames.json")
    user_data = _read_object(root / "config" / "users.json", optional=True)
    frame_data.setdefault("frames", {})
    user_data.setdefault("users", {})
    return frame_data, user_data


def _slug(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.strip().lower()).strip("-") or "device"
    return slug[:54].rstrip("-") or "device"


def add_frame(
    data_root: str | Path,
    *,
    owner_email: str,
    display_name: str,
    width: int,
    height: int,
    format_codes: tuple[int, ...],
) -> tuple[Config, str, str]:
    root = Path(data_root)
    _validate_profile(width, height, format_codes)
    with _config_transaction_lock(root):
        frame_data, user_data = _config_documents(root)
        owner_key = owner_email.strip().lower()
        owner = user_data["users"].get(owner_key)
        if not isinstance(owner, dict):
            raise ConfigError("Owner account not found.")
        base = _slug(display_name)
        for _ in range(16):
            device_id = f"{base}-{secrets.token_hex(3)}"
            if device_id not in frame_data["frames"]:
                break
        else:
            raise ConfigError("Could not mint a unique device ID.")
        _validate_device_id(device_id)
        token = secrets.token_hex(32)
        frame_data["frames"][device_id] = {
            "display_name": display_name.strip() or device_id,
            "token": token,
            "profile": {"width": width, "height": height, "format_codes": list(format_codes)},
            "image_transform": {"rotate_deg": 0, "mirror_x": False, "mirror_y": False},
        }
        devices = [str(item) for item in owner.get("frames", owner.get("devices", []))]
        owner["frames"] = devices + [device_id]
        owner.pop("devices", None)
        _commit_config_transaction(root, frame_data, user_data, lock_held=True)
    (root / "devices" / device_id / "images").mkdir(parents=True, exist_ok=True)
    (root / "devices" / device_id / "state").mkdir(parents=True, exist_ok=True)
    return load_config(root), device_id, token


def update_frame(
    data_root: str | Path,
    *,
    device_id: str,
    display_name: str,
    width: int,
    height: int,
    format_codes: tuple[int, ...],
    namespace: dict | None = None,
) -> Config:
    root = Path(data_root)
    _validate_device_id(device_id)
    _validate_profile(width, height, format_codes)
    with _config_transaction_lock(root):
        frame_data, user_data = _config_documents(root)
        entry = frame_data["frames"].get(device_id)
        if not isinstance(entry, dict):
            raise ConfigError("Device not found.")
        entry["display_name"] = display_name.strip() or device_id
        entry["profile"] = {"width": width, "height": height, "format_codes": list(format_codes)}
        _commit_config_transaction(root, frame_data, user_data, namespace=namespace, lock_held=True)
    return load_config(root)


def rotate_frame_token(data_root: str | Path, device_id: str) -> tuple[Config, str]:
    root = Path(data_root)
    _validate_device_id(device_id)
    with _config_transaction_lock(root):
        frame_data, user_data = _config_documents(root)
        entry = frame_data["frames"].get(device_id)
        if not isinstance(entry, dict):
            raise ConfigError("Device not found.")
        existing = {str(item.get("token", "")) for item in frame_data["frames"].values()
                    if isinstance(item, dict)}
        token = secrets.token_hex(32)
        while token in existing:
            token = secrets.token_hex(32)
        entry["token"] = token
        entry["revoked"] = False
        _commit_config_transaction(root, frame_data, user_data, lock_held=True)
    return load_config(root), token


def remove_frame(data_root: str | Path, device_id: str, *, remove_namespace: bool = False) -> Config:
    root = Path(data_root)
    _validate_device_id(device_id)
    with _config_transaction_lock(root):
        frame_data, user_data = _config_documents(root)
        if frame_data["frames"].pop(device_id, None) is None:
            raise ConfigError("Device not found.")
        for entry in user_data["users"].values():
            if not isinstance(entry, dict):
                continue
            devices = [str(item) for item in entry.get("frames", entry.get("devices", []))]
            entry["frames"] = [item for item in devices if item != device_id]
            entry.pop("devices", None)
        namespace = None
        if remove_namespace:
            suffix = secrets.token_hex(4)
            namespace = {
                "operation": "remove",
                "target": f"devices/{device_id}",
                "backup": f"devices/.{device_id}.remove-{suffix}",
            }
        _commit_config_transaction(root, frame_data, user_data, namespace=namespace, lock_held=True)
    return load_config(root)


def hash_password(password: str, *, iterations: int = PBKDF2_ITERATIONS) -> str:
    salt = os.urandom(16)
    digest = hashlib.pbkdf2_hmac(PBKDF2_ALGO, password.encode(), salt, iterations)
    return f"{_HASH_PREFIX}${iterations}${salt.hex()}${digest.hex()}"


def verify_password(password: str, stored: str) -> bool:
    if not valid_password_hash(stored):
        return False
    try:
        prefix, iterations, salt_hex, digest_hex = stored.split("$")
        expected = bytes.fromhex(digest_hex)
        candidate = hashlib.pbkdf2_hmac(
            PBKDF2_ALGO, password.encode(), bytes.fromhex(salt_hex), int(iterations)
        )
        return hmac.compare_digest(candidate, expected)
    except (AttributeError, ValueError):
        return False


def valid_password_hash(stored: str) -> bool:
    try:
        prefix, iterations, salt_hex, digest_hex = stored.split("$")
        iteration_count = int(iterations)
        return (prefix == _HASH_PREFIX
                and str(iteration_count) == iterations
                and PBKDF2_ITERATIONS <= iteration_count <= 1_000_000
                and re.fullmatch(r"[0-9a-f]{32}", salt_hex) is not None
                and re.fullmatch(r"[0-9a-f]{64}", digest_hex) is not None)
    except (AttributeError, ValueError):
        return False


def user_can_access(user: User, frame_id: str) -> bool:
    return frame_id in user.devices