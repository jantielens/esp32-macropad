"""File-based frame capabilities and independent human UI authentication."""

from __future__ import annotations

import fcntl
import hashlib
import hmac
import json
import os
import re
import secrets
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
    return bool(value.frames and value.users)


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
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", normalized_frame_id):
        raise ConfigError("Frame ID must use lowercase letters, numbers, and hyphens.")
    if not 1 <= width <= 10_000 or not 1 <= height <= 10_000:
        raise ConfigError("Frame dimensions must be between 1 and 10000 pixels.")
    if not format_codes or any(code not in SUPPORTED_FORMAT_CODES for code in format_codes):
        raise ConfigError("Select a supported output format.")
    if (FORMAT_G16P in format_codes or FORMAT_G16Z in format_codes) and width % 2:
        raise ConfigError("Gray16 output requires an even frame width.")

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
    frame_data = _read_object(root / "config" / "frames.json")
    user_data = _read_object(root / "config" / "users.json", optional=True)

    frames = {}
    frame_tokens = set()
    entries = frame_data.get("frames", frame_data)
    for frame_id, entry in entries.items():
        if not isinstance(entry, dict):
            raise ConfigError(f"frame '{frame_id}' must be an object")
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


def hash_password(password: str, *, iterations: int = PBKDF2_ITERATIONS) -> str:
    salt = os.urandom(16)
    digest = hashlib.pbkdf2_hmac(PBKDF2_ALGO, password.encode(), salt, iterations)
    return f"{_HASH_PREFIX}${iterations}${salt.hex()}${digest.hex()}"


def verify_password(password: str, stored: str) -> bool:
    try:
        prefix, iterations, salt_hex, digest_hex = stored.split("$")
        if prefix != _HASH_PREFIX:
            return False
        expected = bytes.fromhex(digest_hex)
        candidate = hashlib.pbkdf2_hmac(
            PBKDF2_ALGO, password.encode(), bytes.fromhex(salt_hex), int(iterations)
        )
        return hmac.compare_digest(candidate, expected)
    except (AttributeError, ValueError):
        return False


def user_can_access(user: User, frame_id: str) -> bool:
    return frame_id in user.devices