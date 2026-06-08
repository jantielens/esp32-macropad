"""Per-image and queue domain logic over the single-copy blob store.

Blob layout (blob-authoritative; ``state/queue.json`` is disposable soft-state):

    images/<id>.g16p          canonical panel image
    images/<id>__thumb.png    gallery thumbnail
    images/<id>.json          authoritative per-image meta
    state/queue.json          ordered list of ids to show next

All lifecycle facts (permanent, expires_at, last_shown_at) live in the per-image meta,
never in the queue. The queue only orders "what to show next" and is rebuildable: a
missing/corrupt queue, or a queued id with no backing blob, degrades to rotation-only.
"""

from __future__ import annotations

import math
import re
from datetime import datetime, timedelta, timezone
from typing import Optional

import blobstore as bs

IMAGE_PREFIX = "images/"
QUEUE_BLOB = "state/queue.json"
SCHEDULE_BLOB = "state/schedule.json"
SETTINGS_BLOB = "state/settings.json"
G16P_EXT = ".g16p"
JPEG_EXT = ".jpg"
THUMB_SUFFIX = "__thumb.png"
META_EXT = ".json"

# Canonical-image extensions, keyed by output format (see config.py). g16z keeps
# the historical .g16p extension so existing E1003 blobs stay valid; jpeg uses
# .jpg so on-device image libraries (Inkplate) pick the right decoder by URL.
FORMAT_EXT = {"g16z": G16P_EXT, "jpeg": JPEG_EXT}
DEFAULT_IMAGE_EXT = G16P_EXT
# All extensions we may have written, for format-agnostic scans/cleanup.
KNOWN_IMAGE_EXTS = (G16P_EXT, JPEG_EXT)
# Content type per format, for the canonical blob upload.
FORMAT_CONTENT_TYPE = {"g16z": "application/octet-stream", "jpeg": "image/jpeg"}

# Image ids are server-minted; restrict to a safe charset to prevent blob-name
# injection / path traversal when building URLs.
_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


def is_valid_id(image_id: str) -> bool:
    return bool(_ID_RE.match(image_id))


def format_ext(image_format: Optional[str]) -> str:
    """Canonical-blob extension for an output format (default .g16p)."""
    return FORMAT_EXT.get((image_format or "").lower(), DEFAULT_IMAGE_EXT)


def meta_image_ext(meta: dict) -> str:
    """Canonical-blob extension implied by a per-image meta's ``format`` field."""
    return format_ext(meta.get("format"))


def image_name(image_id: str, ext: str = DEFAULT_IMAGE_EXT) -> str:
    return f"{IMAGE_PREFIX}{image_id}{ext}"


def g16p_name(image_id: str) -> str:
    return image_name(image_id, G16P_EXT)


def _split_image_name(name: str) -> Optional[tuple[str, str]]:
    """Split a blob name into ``(image_id, ext)`` for canonical image blobs only.

    Returns None for thumbnails, meta JSON, non-image blobs, or invalid ids.
    """
    if not name.startswith(IMAGE_PREFIX):
        return None
    rest = name[len(IMAGE_PREFIX):]
    if rest.endswith(THUMB_SUFFIX) or rest.endswith(META_EXT):
        return None
    for ext in KNOWN_IMAGE_EXTS:
        if rest.endswith(ext):
            image_id = rest[:-len(ext)]
            if is_valid_id(image_id):
                return image_id, ext
    return None


def thumb_name(image_id: str) -> str:
    return f"{IMAGE_PREFIX}{image_id}{THUMB_SUFFIX}"


def meta_name(image_id: str) -> str:
    return f"{IMAGE_PREFIX}{image_id}{META_EXT}"


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _parse_iso(value: Optional[str]) -> Optional[datetime]:
    if not value:
        return None
    try:
        dt = datetime.fromisoformat(value)
    except ValueError:
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt


def is_expired(meta: dict, *, at: Optional[datetime] = None) -> bool:
    expires = _parse_iso(meta.get("expires_at"))
    if expires is None:
        return False
    return (at or datetime.now(timezone.utc)) >= expires


# --- Selection metadata (stamped on the .g16p blob) ---------------------------
#
# The fields /api/next needs to choose an image -- permanent, expires_at,
# last_shown_at, served_at -- are stamped as x-ms-meta-* pairs on the canonical
# .g16p blob so select_next() can read them all in one List Blobs call instead
# of a GET per image. The per-image .json remains the human-facing record of
# truth (captions, knobs, crop); these are a denormalized projection of its
# selection-relevant subset, kept in sync on every mutation that touches them.


def selection_blob_metadata(meta: dict) -> dict:
    """Project the selection-relevant fields into x-ms-meta string pairs.

    Null/absent timestamps are omitted (absence == None on read); ``permanent``
    is always present so its key doubles as the "metadata is stamped" marker.
    """
    md = {"permanent": "1" if meta.get("permanent", False) else "0"}
    for key in ("expires_at", "last_shown_at", "served_at", "uploaded_at"):
        value = meta.get(key)
        if value:
            md[key] = str(value)
    return md


def _selection_from_blob_metadata(md: dict) -> dict:
    """Inverse of selection_blob_metadata: reconstruct selection fields."""
    return {
        "permanent": md.get("permanent") == "1",
        "expires_at": md.get("expires_at") or None,
        "last_shown_at": md.get("last_shown_at") or None,
        "served_at": md.get("served_at") or None,
        "uploaded_at": md.get("uploaded_at") or None,
    }


# --- Queue (soft-state) -------------------------------------------------------


def read_queue(sas: str) -> list[str]:
    data = bs.download_json(sas, QUEUE_BLOB)
    if not isinstance(data, list):
        return []
    return [str(x) for x in data if is_valid_id(str(x))]


def write_queue(sas: str, ids: list[str]) -> None:
    bs.upload_json(sas, QUEUE_BLOB, ids)


def queue_remove(sas: str, image_id: str) -> None:
    ids = read_queue(sas)
    if image_id in ids:
        write_queue(sas, [i for i in ids if i != image_id])


def queue_unshift(sas: str, image_id: str) -> None:
    """Place an id at the front of the queue (show it next), de-duplicated."""
    ids = [i for i in read_queue(sas) if i != image_id]
    ids.insert(0, image_id)
    write_queue(sas, ids)


# --- Scheduler soft-state -----------------------------------------------------
#
# The two-bucket scheduler (see bucket_schedule_pick) needs one integer between
# polls: how many displays remain until the next temporary-photo slot. It lives
# in its own disposable blob so a lost/corrupt value simply restarts the cadence
# (temporary photos keep showing; only the phase resets) -- the image blobs stay
# the sole authoritative state.


def read_schedule(sas: str) -> int:
    """Temp-slot countdown (disposable soft-state). Missing/corrupt -> 0 (temp due)."""
    data = bs.download_json(sas, SCHEDULE_BLOB)
    if isinstance(data, dict):
        try:
            return max(0, int(data.get("temp_countdown", 0)))
        except (TypeError, ValueError):
            return 0
    return 0


def write_schedule(sas: str, temp_countdown: int) -> None:
    bs.upload_json(sas, SCHEDULE_BLOB, {"temp_countdown": int(max(0, temp_countdown))})


# --- Per-device settings (operator override) ----------------------------------
#
# A small map of per-device knobs the owner can tune from the gallery UI without
# editing CONFIG_JSON (which the app only reads). Currently just temp_min_spacing,
# the temporary-photo cadence. Absence falls back to the config default, so a
# lost/corrupt blob degrades gracefully to the deployed configuration.


def read_settings(sas: str) -> dict:
    """Per-device UI settings override. Missing/corrupt -> {} (use config defaults)."""
    data = bs.download_json(sas, SETTINGS_BLOB)
    return data if isinstance(data, dict) else {}


def write_settings(sas: str, settings: dict) -> None:
    bs.upload_json(sas, SETTINGS_BLOB, settings)


# --- Per-image meta -----------------------------------------------------------


def read_meta(sas: str, image_id: str) -> Optional[dict]:
    return bs.download_json(sas, meta_name(image_id))


def write_meta(sas: str, image_id: str, meta: dict) -> None:
    bs.upload_json(sas, meta_name(image_id), meta)


def list_image_ids(sas: str) -> list[str]:
    """All image ids that have a canonical image blob (any known format)."""
    names = bs.list_blobs(sas, IMAGE_PREFIX)
    ids: list[str] = []
    for name in names:
        split = _split_image_name(name)
        if split:
            ids.append(split[0])
    return ids


def delete_image(sas: str, image_id: str) -> None:
    """Remove an image's blob(s), thumb, meta, and any queue reference.

    Deletes every known canonical extension (delete_blob tolerates 404) so the
    format does not need to be known at delete time.
    """
    for ext in KNOWN_IMAGE_EXTS:
        bs.delete_blob(sas, image_name(image_id, ext))
    bs.delete_blob(sas, thumb_name(image_id))
    bs.delete_blob(sas, meta_name(image_id))
    queue_remove(sas, image_id)


def store_image(
    sas: str,
    image_id: str,
    image_bytes: bytes,
    thumb_png: bytes,
    meta: dict,
    *,
    enqueue: bool = True,
) -> None:
    """Persist a new image (blob + thumb + meta) and optionally enqueue it.

    The canonical blob's extension and content type are derived from the meta's
    ``format`` field (default g16z/.g16p).
    """
    image_format = meta.get("format")
    ext = meta_image_ext(meta)
    content_type = FORMAT_CONTENT_TYPE.get(
        (image_format or "").lower(), "application/octet-stream"
    )
    bs.upload_blob(
        sas,
        image_name(image_id, ext),
        image_bytes,
        content_type=content_type,
        metadata=selection_blob_metadata(meta),
    )
    bs.upload_blob(sas, thumb_name(image_id), thumb_png, content_type="image/png")
    write_meta(sas, image_id, meta)
    if enqueue:
        # LIFO: a new upload jumps to the front so the most recent action (upload
        # or "Show next") is what the device serves next.
        queue_unshift(sas, image_id)


# --- Two-bucket rotation scheduler (pure) -------------------------------------
#
# Rotation photos split into two buckets by lifecycle:
#   * permanent  -- permanent && no expiry  -> the everyday wallpaper pool
#   * temporary  -- permanent && has expiry -> short-lived photos to feature
# A temporary slot fires every `spacing` displays, where spacing is chosen so any
# single temporary photo repeats at most once per `n` displays. This makes a
# temporary photo's on-screen share independent of how big the permanent pool is
# (the failing property of a weight multiplier, whose share is boost/pool). The
# functions below are pure (no blob I/O) so they are unit-tested directly and
# drive the offline simulator/sweep.


def temp_slot_spacing(n: int, k: int, floor: int = 2) -> int:
    """Displays between featured slots for k featured photos at knob n.

    Each featured photo should repeat at most once per n displays, so the bucket
    fires every ceil(n / k) displays. Floored at ``floor`` (default 2) to cap the
    featured bucket's combined share: one permanent between featured slots caps it
    at 50%, a floor of 4 at 25%, etc. (see share_pct_to_floor).
    """
    return max(floor, math.ceil(n / max(1, k)))


def share_pct_to_floor(pct: int) -> int:
    """Spacing floor that caps the featured bucket at ~pct% of displays (pure).

    A featured slot fires at most once per ``floor`` displays, so the bucket's
    share is 1/floor. floor = ceil(100/pct) keeps the actual share <= pct
    (50 -> 2, 25 -> 4, 33 -> 4, 100 -> 1). Floored at 1.
    """
    return max(1, math.ceil(100 / max(1, pct)))


def is_fresh(sel: dict, *, now: datetime, window_days: int) -> bool:
    """Whether a permanent (no-expiry) photo is still in its post-upload boost window.

    Fresh photos join the featured bucket so a newly uploaded photo is shown often
    for a while instead of being lost at 1/pool odds, then graduate into normal
    permanent rotation automatically once uploaded_at ages past the window. A
    window of 0 (or a missing uploaded_at) means "never fresh".
    """
    if window_days <= 0:
        return False
    if not sel.get("permanent") or sel.get("expires_at"):
        return False
    uploaded = _parse_iso(sel.get("uploaded_at"))
    if uploaded is None:
        return False
    return (now - uploaded) < timedelta(days=window_days)


def _lru_id(items: list) -> Optional[str]:
    """Least-recently-shown id from [(id, last_shown_at | None), ...].

    Never-shown (None) sorts first; ties keep the first encountered, so callers
    that pass a stably-ordered list get deterministic picks.
    """
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
    """Pick the next rotation image from the permanent/featured buckets (pure).

    perm / temp are lists of (id, last_shown_at | None) for eligible images in
    each bucket; ``temp`` is the featured bucket (temporary + fresh photos).
    temp_countdown is displays remaining until the next featured slot (soft-state).
    n is the per-device min spacing; ``floor`` caps the featured bucket's combined
    share (see share_pct_to_floor).

    Returns (chosen_id, source, new_countdown) where source is "permanent",
    "temporary", or None when nothing is eligible.

    Cadence: when both buckets are populated, a featured photo is served once
    every temp_slot_spacing(n, len(temp), floor) displays and a permanent fills the
    rest; within the featured bucket the least-recently-shown photo rotates, so
    multiple featured photos share the slot fairly. Fallbacks: only permanents ->
    100% permanent; only featured -> they rotate among themselves (a missing
    permanent must not blank the screen).
    """
    if not perm and not temp:
        return None, None, temp_countdown
    if not temp:
        # No featured photos: pure permanent rotation; reset so a freshly added
        # featured photo is immediately due.
        return _lru_id(perm), "permanent", 0
    if not perm:
        # No separators available; rotate featured photos among themselves.
        return _lru_id(temp), "temporary", temp_countdown
    if temp_countdown <= 0:
        spacing = temp_slot_spacing(n, len(temp), floor)
        return _lru_id(temp), "temporary", spacing - 1
    return _lru_id(perm), "permanent", temp_countdown - 1


# --- Selection for /api/next --------------------------------------------------


def select_next(
    sas: str,
    *,
    temp_min_spacing: int = 4,
    fresh_window_days: int = 7,
    max_temp_share_pct: int = 50,
) -> Optional[str]:
    """Pick the next image id to serve, or None.

    Order: first servable id in the queue (explicit one-shots), then the
    two-bucket round-robin rotation. The featured bucket holds temporary photos
    (those with an expiry) and fresh photos (permanent photos uploaded within
    fresh_window_days); permanent photos past the window form the everyday pool.
    Skips expired images and queued ids with no backing blob, reaping expired
    blobs opportunistically.

    temp_min_spacing is the per-device cadence knob: a featured photo repeats at
    most once per this many displays. fresh_window_days sets how long a new photo
    stays featured (0 disables). max_temp_share_pct caps the featured bucket's
    combined share (via share_pct_to_floor), bounding bulk-upload bursts.

    Selection reads are metadata-only: a single List Blobs (include=metadata)
    call returns every image's selection state, so the cost is ~3 round-trips
    flat (list + queue + schedule) regardless of gallery size.
    """
    now = datetime.now(timezone.utc)

    # 1 round-trip: every image blob's selection metadata, inline.
    blobs = bs.list_blobs_with_metadata(sas, IMAGE_PREFIX)
    metas: dict[str, dict] = {}
    for name, md in blobs.items():
        split = _split_image_name(name)
        if not split:
            continue
        image_id, _ext = split
        if "permanent" in md:
            metas[image_id] = _selection_from_blob_metadata(md)
        else:
            # Backfill: image uploaded before metadata stamping existed. Read its
            # .json once and stamp the blob so subsequent polls are metadata-only.
            full = read_meta(sas, image_id) or {}
            bs.set_blob_metadata(sas, name, selection_blob_metadata(full))
            metas[image_id] = {
                "permanent": bool(full.get("permanent", False)),
                "expires_at": full.get("expires_at"),
                "last_shown_at": full.get("last_shown_at"),
                "served_at": full.get("served_at"),
                "uploaded_at": full.get("uploaded_at"),
            }

    existing = set(metas.keys())

    # 0) Deferred one-shot reap: a one-shot served on a previous poll kept its
    # blob alive so the /api/next redirect target stayed valid while the device
    # fetched it. Delete it now -- the device (which sleeps between cycles) has
    # long since downloaded it, and it was dequeued at serve time so it will
    # never be selected again.
    for image_id in list(existing):
        sel = metas[image_id]
        if not sel.get("permanent") and sel.get("served_at"):
            delete_image(sas, image_id)
            existing.discard(image_id)
            metas.pop(image_id, None)

    # 1) Queue: first non-expired id that still has a backing blob.
    for image_id in read_queue(sas):
        if image_id not in existing:
            queue_remove(sas, image_id)  # stale pointer
            continue
        if is_expired(metas[image_id], at=now):
            delete_image(sas, image_id)
            existing.discard(image_id)
            metas.pop(image_id, None)
            continue
        return image_id

    # 2) Two-bucket round-robin rotation. The featured bucket holds temporary
    # photos (have an expiry) and fresh photos (permanent, uploaded within
    # fresh_window_days); permanent photos past the window form the everyday pool.
    # Featured slots are spaced so any single featured photo repeats at most once
    # per `temp_min_spacing` displays, and max_temp_share_pct caps the bucket's
    # combined share. One-shots (`permanent=false`) are served via the queue only,
    # never here. Expired images are reaped as we scan.
    perm: list = []
    temp: list = []
    for image_id in sorted(existing):  # sorted -> deterministic LRU tie-break
        sel = metas[image_id]
        if is_expired(sel, at=now):
            delete_image(sas, image_id)
            continue
        if not sel.get("permanent"):
            continue
        shown = _parse_iso(sel.get("last_shown_at"))
        if sel.get("expires_at") or is_fresh(sel, now=now, window_days=fresh_window_days):
            temp.append((image_id, shown))  # temporary or fresh -> featured bucket
        else:
            perm.append((image_id, shown))

    countdown = read_schedule(sas)
    chosen, _source, new_countdown = bucket_schedule_pick(
        perm, temp, temp_countdown=countdown, n=temp_min_spacing,
        floor=share_pct_to_floor(max_temp_share_pct),
    )
    if chosen is not None and new_countdown != countdown:
        write_schedule(sas, new_countdown)
    return chosen


def mark_served(sas: str, image_id: str, meta: dict) -> None:
    """Apply serve-time effects: stamp last_shown_at; one-shot dequeue (deferred delete)."""
    if meta.get("permanent", False):
        # Permanent: stamp last_shown_at so rotation advances.
        meta["last_shown_at"] = now_iso()
    else:
        # One-shot: dequeue so it is never selected again, but keep the blob so
        # the /api/next redirect (or proxy fetch) can still deliver it. The
        # `served_at` stamp marks it for reaping on the next poll once the device
        # has finished downloading -- deleting it here would 404 the redirect.
        meta["served_at"] = now_iso()
    write_meta(sas, image_id, meta)
    # Keep the blob's selection metadata in lock-step with the .json so the next
    # poll's metadata-only read sees the updated last_shown_at / served_at.
    bs.set_blob_metadata(sas, image_name(image_id, meta_image_ext(meta)), selection_blob_metadata(meta))
    queue_remove(sas, image_id)
