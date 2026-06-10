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

# Output format profile. ``g16z`` is the calibrated/dithered E1003 transport and
# stays the default so existing devices are byte-identical. ``jpeg`` is a
# resize-only profile for panels whose firmware library does its own grayscale +
# dither (e.g. Inkplate).
FORMAT_G16Z = "g16z"
FORMAT_JPEG = "jpeg"
SUPPORTED_FORMATS = (FORMAT_G16Z, FORMAT_JPEG)
DEFAULT_FORMAT = FORMAT_G16Z
DEFAULT_JPEG_QUALITY = 90

# How ``/api/next`` delivers the payload to the device. ``redirect`` (default)
# 302s the device straight to the blob's SAS URL (zero-copy, fastest) and suits
# clients that follow HTTP redirects (e.g. our own E1003 firmware). ``inline``
# streams the bytes through the app, for clients that cannot follow redirects
# (e.g. the InkplateLibrary image loader, which defaults to no-follow).
SERVE_REDIRECT = "redirect"
SERVE_INLINE = "inline"
SUPPORTED_SERVE_MODES = (SERVE_REDIRECT, SERVE_INLINE)
DEFAULT_SERVE_MODE = SERVE_REDIRECT

# Temporary-photo cadence knob. A temporary photo (one with an expiry) repeats at
# most once per ``temp_min_spacing`` displays, so its on-screen share is
# pool-size-independent (see store.bucket_schedule_pick). The default of 4 means
# a single temporary photo takes ~1 in every 4 displays. Alternation caps the
# temporary bucket at 50%, so values below 2 are meaningless and clamped up.
MIN_TEMP_MIN_SPACING = 2
DEFAULT_TEMP_MIN_SPACING = 4

# Fresh-photo window. A newly uploaded permanent photo (one with no expiry) is
# treated as "fresh" for this many days after upload: it joins the featured
# bucket alongside temporary photos and gets the same elevated, pool-independent
# cadence, so a new photo is shown often instead of being lost at 1/pool odds.
# Graduation is automatic and stateless -- derived from the photo's uploaded_at,
# so once it ages past the window it drops into normal permanent rotation with no
# event or extra stored state. 0 disables the fresh boost.
MIN_FRESH_WINDOW_DAYS = 0
DEFAULT_FRESH_WINDOW_DAYS = 7

# Cap on the combined featured (temporary + fresh) bucket's share of displays.
# Enforced as a spacing floor (at least one permanent between featured slots): a
# share of P% maps to a floor of ceil(100/P), so 50% -> floor 2 (alternation),
# 25% -> floor 4. This bounds the burst when many photos are featured at once
# (e.g. a bulk upload of fresh photos). Clamped to 1..100.
MIN_MAX_TEMP_SHARE_PCT = 1
MAX_MAX_TEMP_SHARE_PCT = 100
DEFAULT_MAX_TEMP_SHARE_PCT = 50


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
    image_format: str = DEFAULT_FORMAT
    jpeg_quality: int = DEFAULT_JPEG_QUALITY
    serve_mode: str = DEFAULT_SERVE_MODE
    temp_min_spacing: int = DEFAULT_TEMP_MIN_SPACING
    fresh_window_days: int = DEFAULT_FRESH_WINDOW_DAYS
    max_temp_share_pct: int = DEFAULT_MAX_TEMP_SHARE_PCT


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
        output = entry.get("output") or {}
        image_format = str(output.get("format", DEFAULT_FORMAT)).lower()
        if image_format not in SUPPORTED_FORMATS:
            raise ConfigError(
                f"device '{device_id}' has unsupported output.format '{image_format}' "
                f"(expected one of {', '.join(SUPPORTED_FORMATS)})"
            )
        try:
            jpeg_quality = int(output.get("jpeg_quality", DEFAULT_JPEG_QUALITY))
        except (TypeError, ValueError):
            raise ConfigError(f"device '{device_id}' output.jpeg_quality must be an integer")
        if not 1 <= jpeg_quality <= 100:
            raise ConfigError(f"device '{device_id}' output.jpeg_quality must be 1..100")
        serve_mode = str(entry.get("serve_mode", DEFAULT_SERVE_MODE)).lower()
        if serve_mode not in SUPPORTED_SERVE_MODES:
            raise ConfigError(
                f"device '{device_id}' has unsupported serve_mode '{serve_mode}' "
                f"(expected one of {', '.join(SUPPORTED_SERVE_MODES)})"
            )
        try:
            temp_min_spacing = int(entry.get("temp_min_spacing", DEFAULT_TEMP_MIN_SPACING))
        except (TypeError, ValueError):
            raise ConfigError(f"device '{device_id}' temp_min_spacing must be an integer")
        # Alternation already caps the temporary bucket at 50%; clamp up so a
        # too-small value cannot ask for more than one-temp-every-other-display.
        temp_min_spacing = max(MIN_TEMP_MIN_SPACING, temp_min_spacing)
        try:
            fresh_window_days = int(entry.get("fresh_window_days", DEFAULT_FRESH_WINDOW_DAYS))
        except (TypeError, ValueError):
            raise ConfigError(f"device '{device_id}' fresh_window_days must be an integer")
        fresh_window_days = max(MIN_FRESH_WINDOW_DAYS, fresh_window_days)
        try:
            max_temp_share_pct = int(entry.get("max_temp_share_pct", DEFAULT_MAX_TEMP_SHARE_PCT))
        except (TypeError, ValueError):
            raise ConfigError(f"device '{device_id}' max_temp_share_pct must be an integer")
        max_temp_share_pct = min(
            MAX_MAX_TEMP_SHARE_PCT, max(MIN_MAX_TEMP_SHARE_PCT, max_temp_share_pct)
        )
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
            image_format=image_format,
            jpeg_quality=jpeg_quality,
            serve_mode=serve_mode,
            temp_min_spacing=temp_min_spacing,
            fresh_window_days=fresh_window_days,
            max_temp_share_pct=max_temp_share_pct,
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
