"""Configuration loading and authentication for the photoframe site.

Single source of configuration: the ``CONFIG_JSON`` environment variable (an App
Service application setting in production, a local file or inline JSON in dev). It is
read **per request** so operators can edit config in the portal without a redeploy.

Human passwords are stored only as salted **PBKDF2** hashes (stdlib ``hashlib``).
Device pull keys are compared in constant time. There is no self-registration; a small
CLI (``hash_password.py``) mints password hashes for operators.

Config shape (see PRD Section 9):

    {
      "devices": {
        "E1003-1": {
          "container_sas_url": "https://.../<container>?<sas>",
          "api_key": "<device-pull-key>",
          "resolution": {"width": 1872, "height": 1404},
          "image_transform": {"rotate_deg": 0, "mirror_x": false, "mirror_y": false}
        }
      },
      "users": {
        "owner@example.com": {
          "password_hash": "pbkdf2_sha256$<iter>$<salt_hex>$<hash_hex>",
          "devices": ["E1003-1"]
        }
      }
    }
"""

from __future__ import annotations

import hashlib
import hmac
import json
import os
from dataclasses import dataclass
from typing import Optional

CONFIG_ENV = "CONFIG_JSON"
PBKDF2_ALGO = "sha256"
PBKDF2_ITERATIONS = 200_000
_HASH_PREFIX = "pbkdf2_sha256"

# E1003 panel default; per-device config may override.
DEFAULT_WIDTH = 1872
DEFAULT_HEIGHT = 1404


class ConfigError(RuntimeError):
    """Raised when CONFIG_JSON is missing or malformed."""


@dataclass(frozen=True)
class Device:
    device_id: str
    container_sas_url: str
    api_key: str
    width: int
    height: int
    image_transform: dict


@dataclass(frozen=True)
class User:
    email: str
    password_hash: str
    devices: tuple[str, ...]


@dataclass(frozen=True)
class Config:
    devices: dict[str, Device]
    users: dict[str, User]

    def device(self, device_id: str) -> Optional[Device]:
        return self.devices.get(device_id)

    def user(self, email: str) -> Optional[User]:
        return self.users.get(email)


def _load_raw() -> dict:
    raw = os.environ.get(CONFIG_ENV)
    if not raw:
        raise ConfigError(f"{CONFIG_ENV} is not set")
    raw = raw.strip()
    # Allow CONFIG_JSON to be either inline JSON or a path to a JSON file (dev).
    if raw.startswith("{"):
        text = raw
    elif os.path.isfile(raw):
        with open(raw, "r", encoding="utf-8") as handle:
            text = handle.read()
    else:
        text = raw
    try:
        data = json.loads(text)
    except ValueError as exc:
        raise ConfigError(f"{CONFIG_ENV} is not valid JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise ConfigError(f"{CONFIG_ENV} must be a JSON object")
    return data


def load_config() -> Config:
    """Parse CONFIG_JSON into a typed Config. Call per request."""
    data = _load_raw()

    devices: dict[str, Device] = {}
    for device_id, entry in (data.get("devices") or {}).items():
        if not isinstance(entry, dict):
            raise ConfigError(f"device '{device_id}' must be an object")
        sas = entry.get("container_sas_url")
        api_key = entry.get("api_key")
        if not sas or not api_key:
            raise ConfigError(f"device '{device_id}' needs container_sas_url and api_key")
        resolution = entry.get("resolution") or {}
        transform = entry.get("image_transform") or {}
        devices[device_id] = Device(
            device_id=device_id,
            container_sas_url=str(sas),
            api_key=str(api_key),
            width=int(resolution.get("width", DEFAULT_WIDTH)),
            height=int(resolution.get("height", DEFAULT_HEIGHT)),
            image_transform={
                "rotate_deg": int(transform.get("rotate_deg", 0) or 0),
                "mirror_x": bool(transform.get("mirror_x", False)),
                "mirror_y": bool(transform.get("mirror_y", False)),
            },
        )

    users: dict[str, User] = {}
    for email, entry in (data.get("users") or {}).items():
        if not isinstance(entry, dict):
            raise ConfigError(f"user '{email}' must be an object")
        password_hash = entry.get("password_hash")
        if not password_hash:
            raise ConfigError(f"user '{email}' needs a password_hash")
        allowed = entry.get("devices") or []
        users[email.lower()] = User(
            email=email.lower(),
            password_hash=str(password_hash),
            devices=tuple(str(d) for d in allowed),
        )

    return Config(devices=devices, users=users)


# --- Password hashing ---------------------------------------------------------


def hash_password(password: str, *, iterations: int = PBKDF2_ITERATIONS) -> str:
    """Mint a salted PBKDF2 hash string: ``pbkdf2_sha256$<iter>$<salt>$<hash>``."""
    salt = os.urandom(16)
    digest = hashlib.pbkdf2_hmac(PBKDF2_ALGO, password.encode("utf-8"), salt, iterations)
    return f"{_HASH_PREFIX}${iterations}${salt.hex()}${digest.hex()}"


def verify_password(password: str, stored: str) -> bool:
    """Constant-time verification of a password against a stored PBKDF2 hash."""
    try:
        prefix, iter_str, salt_hex, hash_hex = stored.split("$")
        if prefix != _HASH_PREFIX:
            return False
        iterations = int(iter_str)
        salt = bytes.fromhex(salt_hex)
        expected = bytes.fromhex(hash_hex)
    except (ValueError, AttributeError):
        return False
    candidate = hashlib.pbkdf2_hmac(PBKDF2_ALGO, password.encode("utf-8"), salt, iterations)
    return hmac.compare_digest(candidate, expected)


def verify_device_key(provided: str, expected: str) -> bool:
    """Constant-time device pull-key comparison."""
    if not provided or not expected:
        return False
    return hmac.compare_digest(provided, expected)


# --- Authorization helpers ----------------------------------------------------


def user_can_access(user: User, device_id: str) -> bool:
    return device_id in user.devices
