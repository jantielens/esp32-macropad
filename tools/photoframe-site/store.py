"""Files-as-truth photo index and best-effort selection policy."""

from __future__ import annotations

import math
import re
import threading
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Optional

import blobstore as bs

PHOTO_CONTAINER = "photos"
STATE_CONTAINER = "state"
QUEUE_BLOB = "queue.json"
SETTINGS_BLOB = "settings.json"
SIDECAR_NAME = "sidecar.json"
_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


def is_valid_id(image_id: str) -> bool:
    return bool(_ID_RE.fullmatch(image_id))


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _parse_iso(value: Optional[str]) -> Optional[datetime]:
    if not value:
        return None
    try:
        result = datetime.fromisoformat(value)
    except ValueError:
        return None
    return result.replace(tzinfo=result.tzinfo or timezone.utc)


def is_expired(meta: dict, *, at: Optional[datetime] = None) -> bool:
    expires = _parse_iso(meta.get("expires_at"))
    return expires is not None and (at or datetime.now(timezone.utc)) >= expires


@dataclass(frozen=True)
class TransportDescriptor:
    image_key: str
    content_crc32: str
    format_code: int
    media_type: str
    width: int
    height: int
    blob_name: str
    content_length: int
    schedule_countdown: Optional[int] = None


MEDIA_TYPES = {
    1: "image/jpeg",
    2: "application/vnd.photoframe.g16p",
    3: "application/vnd.photoframe.g16z",
}


class PhotoIndex:
    """Derived in-memory index whose durable source is per-photo sidecars."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._photos: dict[str, dict] = {}

    def rebuild(self) -> None:
        rebuilt = {}
        for name in bs.list_blobs(PHOTO_CONTAINER, ""):
            if not name.endswith(f"/{SIDECAR_NAME}"):
                continue
            image_id = name.split("/", 1)[0]
            value = bs.download_json(PHOTO_CONTAINER, name)
            if is_valid_id(image_id) and isinstance(value, dict) and value.get("id") == image_id:
                rebuilt[image_id] = value
        with self._lock:
            self._photos = rebuilt

    def all(self) -> list[dict]:
        with self._lock:
            return [dict(meta) for meta in self._photos.values()]

    def get(self, image_id: str) -> Optional[dict]:
        with self._lock:
            meta = self._photos.get(image_id)
            return dict(meta) if meta else None

    def put(self, meta: dict) -> None:
        image_id = str(meta["id"])
        if not is_valid_id(image_id):
            raise ValueError("invalid image id")
        with self._lock:
            bs.upload_json(PHOTO_CONTAINER, f"{image_id}/{SIDECAR_NAME}", meta)
            self._photos[image_id] = dict(meta)

    def delete(self, image_id: str) -> None:
        with self._lock:
            prefix = f"{image_id}/"
            for name in bs.list_blobs(PHOTO_CONTAINER, prefix):
                bs.delete_blob(PHOTO_CONTAINER, name)
            self._photos.pop(image_id, None)
            queue_remove(image_id)

    def select(
        self,
        *,
        width: int,
        height: int,
        format_codes: tuple[int, ...],
        variant_keys: dict[int, str],
        temp_min_spacing: int,
        fresh_window_days: int,
        max_temp_share_pct: int,
        fingerprint: tuple[str, str] | None,
    ) -> Optional[TransportDescriptor]:
        with self._lock:
            now = datetime.now(timezone.utc)
            eligible: dict[str, tuple[dict, dict]] = {}
            for image_id, meta in self._photos.items():
                if is_expired(meta, at=now) or (not meta.get("permanent", False) and meta.get("served_at")):
                    continue
                variants = meta.get("variants") or []
                chosen_variant = None
                for code in format_codes:
                    chosen_variant = next(
                        (
                            variant for variant in variants
                            if int(variant.get("format_code", 0)) == code
                            and int(variant.get("width", 0)) == width
                            and int(variant.get("height", 0)) == height
                            and variant.get("profile_key") == variant_keys[code]
                        ),
                        None,
                    )
                    if chosen_variant is not None:
                        break
                if chosen_variant is not None:
                    eligible[image_id] = (meta, chosen_variant)

            if fingerprint and len(eligible) > 1:
                eligible = {
                    image_id: item for image_id, item in eligible.items()
                    if (image_id, item[1].get("content_crc32")) != fingerprint
                }
            if not eligible:
                return None

            selected_id = next((item for item in read_queue() if item in eligible), None)
            new_countdown = None
            if selected_id is None:
                perm, featured = [], []
                for image_id in sorted(eligible):
                    meta = eligible[image_id][0]
                    if not meta.get("permanent", False):
                        continue
                    shown = _parse_iso(meta.get("last_shown_at"))
                    if meta.get("expires_at") or is_fresh(meta, now=now, window_days=fresh_window_days):
                        featured.append((image_id, shown))
                    else:
                        perm.append((image_id, shown))
                countdown = read_schedule()
                selected_id, _source, new_countdown = bucket_schedule_pick(
                    perm,
                    featured,
                    temp_countdown=countdown,
                    n=temp_min_spacing,
                    floor=share_pct_to_floor(max_temp_share_pct),
                )
                if selected_id is not None and countdown == new_countdown:
                    new_countdown = None
            if selected_id is None:
                return None

            meta, variant = eligible[selected_id]
            code = int(variant["format_code"])
            return TransportDescriptor(
                image_key=selected_id,
                content_crc32=str(variant["content_crc32"]),
                format_code=code,
                media_type=MEDIA_TYPES[code],
                width=int(variant["width"]),
                height=int(variant["height"]),
                blob_name=str(variant["blob_name"]),
                content_length=int(variant["content_length"]),
                schedule_countdown=new_countdown,
            )

    def commit_selection(self, descriptor: TransportDescriptor) -> None:
        with self._lock:
            meta = self._photos.get(descriptor.image_key)
            if meta is None:
                return
            updated = dict(meta)
            if updated.get("permanent", False):
                updated["last_shown_at"] = now_iso()
            else:
                updated["served_at"] = now_iso()
            bs.upload_json(PHOTO_CONTAINER, f"{descriptor.image_key}/{SIDECAR_NAME}", updated)
            self._photos[descriptor.image_key] = updated
            if descriptor.schedule_countdown is not None:
                write_schedule(descriptor.schedule_countdown)
            queue_remove(descriptor.image_key)


def read_queue() -> list[str]:
    value = bs.download_json(STATE_CONTAINER, QUEUE_BLOB)
    return [str(item) for item in value if is_valid_id(str(item))] if isinstance(value, list) else []


def write_queue(ids: list[str]) -> None:
    bs.upload_json(STATE_CONTAINER, QUEUE_BLOB, ids)


def queue_remove(image_id: str) -> None:
    ids = read_queue()
    if image_id in ids:
        write_queue([item for item in ids if item != image_id])


def queue_unshift(image_id: str) -> None:
    write_queue([image_id] + [item for item in read_queue() if item != image_id])


def read_schedule() -> int:
    value = bs.download_json(STATE_CONTAINER, "schedule.json")
    try:
        return max(0, int(value.get("temp_countdown", 0))) if isinstance(value, dict) else 0
    except (TypeError, ValueError):
        return 0


def write_schedule(value: int) -> None:
    bs.upload_json(STATE_CONTAINER, "schedule.json", {"temp_countdown": max(0, value)})


def read_settings() -> dict:
    value = bs.download_json(STATE_CONTAINER, SETTINGS_BLOB)
    return value if isinstance(value, dict) else {}


def write_settings(settings: dict) -> None:
    bs.upload_json(STATE_CONTAINER, SETTINGS_BLOB, settings)


# The cadence functions below are ported unchanged from the original site.

def temp_slot_spacing(n: int, k: int, floor: int = 2) -> int:
    return max(floor, math.ceil(n / max(1, k)))


def share_pct_to_floor(pct: int) -> int:
    return max(1, math.ceil(100 / max(1, pct)))


def is_fresh(sel: dict, *, now: datetime, window_days: int) -> bool:
    if window_days <= 0:
        return False
    if not sel.get("permanent") or sel.get("expires_at"):
        return False
    uploaded = _parse_iso(sel.get("uploaded_at"))
    if uploaded is None:
        return False
    return (now - uploaded) < timedelta(days=window_days)


def _lru_id(items: list) -> Optional[str]:
    best: Optional[str] = None
    best_t: Optional[datetime] = None
    for image_id, shown in items:
        if best is None or (best_t is not None and (shown is None or shown < best_t)):
            best = image_id
            best_t = shown
    return best


def bucket_schedule_pick(
    perm: list,
    temp: list,
    *,
    temp_countdown: int,
    n: int,
    floor: int = 2,
) -> tuple:
    if not perm and not temp:
        return None, None, temp_countdown
    if not temp:
        return _lru_id(perm), "permanent", 0
    if not perm:
        return _lru_id(temp), "temporary", temp_countdown
    if temp_countdown <= 0:
        spacing = temp_slot_spacing(n, len(temp), floor)
        return _lru_id(temp), "temporary", spacing - 1
    return _lru_id(perm), "permanent", temp_countdown - 1


def estimate_displays_per_day(last_shown: list, *, now: Optional[datetime] = None,
                              default: float = 24.0, lookback_days: float = 14.0) -> float:
    now = now or datetime.now(timezone.utc)
    cutoff = now - timedelta(days=lookback_days)
    times = []
    for value in last_shown:
        parsed = value if isinstance(value, datetime) else _parse_iso(value)
        if parsed is not None and parsed >= cutoff:
            times.append(parsed)
    times = sorted(set(times))
    if len(times) < 2:
        return default
    gaps = sorted((b - a).total_seconds() for a, b in zip(times, times[1:]) if b > a)
    if not gaps:
        return default
    middle = len(gaps) // 2
    median = gaps[middle] if len(gaps) % 2 else (gaps[middle - 1] + gaps[middle]) / 2.0
    return 86400.0 / median if median > 0 else default


def expected_share(*, is_featured: bool, perm_count: int, featured_count: int,
                   n: int, floor: int) -> float:
    p, k = max(0, perm_count), max(0, featured_count)
    if is_featured:
        if k == 0:
            return 0.0
        if p == 0:
            return 1.0 / k
        return 1.0 / (temp_slot_spacing(n, k, floor) * k)
    if p == 0:
        return 0.0
    if k == 0:
        return 1.0 / p
    spacing = temp_slot_spacing(n, k, floor)
    return (spacing - 1) / (spacing * p)


def frequency_label(share: float, displays_per_day: float) -> str:
    rate_day = max(0.0, share) * max(0.0, displays_per_day)
    if rate_day <= 0.0:
        return "not in rotation"
    rate_hour = rate_day / 24.0
    if rate_hour >= 1.0:
        return f"~{round(rate_hour)}\u00d7/hour"
    if rate_day >= 1.0:
        return f"~{round(rate_day)}\u00d7/day"
    rate_week = rate_day * 7.0
    if rate_week >= 1.0:
        return f"~{round(rate_week)}\u00d7/week"
    interval_days = 1.0 / rate_day
    if interval_days < 14:
        return f"~once every {round(interval_days)} days"
    if interval_days < 60:
        return f"~once every {round(interval_days / 7)} weeks"
    return f"~once every {round(interval_days / 30)} months"