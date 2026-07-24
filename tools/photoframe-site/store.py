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
import hashlib
import zlib
from datetime import datetime, timedelta, timezone
from typing import Callable, NamedTuple, Optional

import blobstore as bs

IMAGE_PREFIX = "images/"
QUEUE_BLOB = "state/queue.json"
SCHEDULE_BLOB = "state/schedule.json"
SETTINGS_BLOB = "state/settings.json"
ASSIGNMENT_PREFIX = "state/assignment/"
ASSIGNMENT_SCHEMA = 1
ASSIGNMENT_JOURNAL_DEPTH = 2
G16P_EXT = ".g16p"
JPEG_EXT = ".jpg"
THUMB_SUFFIX = "__thumb.png"
META_EXT = ".json"

# Canonical-image extensions, keyed by output format (see config.py). g16z keeps
# the historical .g16p extension so existing E1003 blobs stay valid; jpeg uses
# .jpg so on-device image libraries (Inkplate) pick the right decoder by URL.
FORMAT_EXT = {"g16z": G16P_EXT, "jpeg": JPEG_EXT}
DEFAULT_IMAGE_EXT = G16P_EXT
# Inverse of FORMAT_EXT: resolve the output format from a canonical blob's
# extension so /api/next can derive media type + X-Image-Format without a meta GET.
_EXT_FORMAT = {ext: fmt for fmt, ext in FORMAT_EXT.items()}
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


def format_from_ext(ext: str) -> str:
    """Output format implied by a canonical-blob extension (inverse of format_ext)."""
    return _EXT_FORMAT.get((ext or "").lower(), "g16z")


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
    ``format`` rides along so the canonical-blob extension and the served
    ``X-Image-Format`` can be resolved from the List Blobs response alone, with
    no per-image .json GET on the /api/next hot path.
    """
    md = {"permanent": "1" if meta.get("permanent", False) else "0"}
    for key in ("expires_at", "last_shown_at", "served_at", "uploaded_at"):
        value = meta.get(key)
        if value:
            md[key] = str(value)
    image_format = meta.get("format")
    if image_format:
        md["format"] = str(image_format)
    if meta.get("content_crc32") is not None:
        md["content_crc32"] = str(int(meta["content_crc32"]) & 0xFFFFFFFF)
    return md


def _selection_from_blob_metadata(md: dict) -> dict:
    """Inverse of selection_blob_metadata: reconstruct selection fields."""
    content_crc32 = None
    if "content_crc32" in md:
        try:
            content_crc32 = int(md["content_crc32"]) & 0xFFFFFFFF
        except (TypeError, ValueError):
            content_crc32 = None
    return {
        "permanent": md.get("permanent") == "1",
        "expires_at": md.get("expires_at") or None,
        "last_shown_at": md.get("last_shown_at") or None,
        "served_at": md.get("served_at") or None,
        "uploaded_at": md.get("uploaded_at") or None,
        "format": md.get("format") or None,
        "content_crc32": content_crc32,
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


def list_selection_meta(sas: str) -> dict[str, dict]:
    """Map every image id to its blob selection metadata (one List Blobs call).

    Stamped images map to their reconstructed selection fields; unstamped (legacy)
    images map to ``{}``. The gallery merges the mutable timestamps from here over
    the per-image .json -- which select_next/mark_served no longer rewrites -- so
    blob metadata stays the authority for last_shown_at / served_at.
    """
    blobs = bs.list_blobs_with_metadata(sas, IMAGE_PREFIX)
    out: dict[str, dict] = {}
    for name, md in blobs.items():
        split = _split_image_name(name)
        if not split:
            continue
        image_id = split[0]
        out[image_id] = _selection_from_blob_metadata(md) if "permanent" in md else {}
    return out


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


def content_crc32(payload: bytes) -> int:
    return zlib.crc32(payload) & 0xFFFFFFFF


def image_key(image_id: str) -> str:
    if not is_valid_id(image_id):
        raise ValueError("invalid image id")
    return hashlib.sha256(image_id.encode("utf-8")).digest()[:8].hex()


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


# --- Expected-exposure estimates (gallery "how often is this shown" hints) -----
#
# The scheduler is deterministic, so a photo's *share* of displays is closed-form
# arithmetic on the current gallery + config -- no simulation needed. Turning that
# share into "per hour/day/week" needs a displays-per-day figure, which we infer
# from recent last_shown_at history (the server never sees the device's poll
# cadence directly). These are best-effort hints, not guarantees.


def estimate_displays_per_day(
    last_shown: list,
    *,
    now: Optional[datetime] = None,
    default: float = 24.0,
    lookback_days: float = 14.0,
) -> float:
    """Estimate displays/day from recent last_shown_at history (median serve gap).

    Each serve stamps exactly one photo's last_shown_at, so the distinct recent
    timestamps across the gallery sample the device's poll cadence. Uses the
    median gap between consecutive serves (robust to downtime and outliers), and
    falls back to ``default`` until at least two recent serves are available.

    ``last_shown`` is an iterable of ISO strings and/or datetimes; None/blank and
    entries older than ``lookback_days`` are ignored.
    """
    now = now or datetime.now(timezone.utc)
    cutoff = now - timedelta(days=lookback_days)
    times = []
    for value in last_shown:
        dt = value if isinstance(value, datetime) else _parse_iso(value)
        if dt is not None and dt >= cutoff:
            times.append(dt)
    times = sorted(set(times))
    if len(times) < 2:
        return default
    gaps = [(b - a).total_seconds() for a, b in zip(times, times[1:]) if b > a]
    if not gaps:
        return default
    gaps.sort()
    mid = len(gaps) // 2
    median = gaps[mid] if len(gaps) % 2 else (gaps[mid - 1] + gaps[mid]) / 2.0
    if median <= 0:
        return default
    return 86400.0 / median


def expected_share(
    *,
    is_featured: bool,
    perm_count: int,
    featured_count: int,
    n: int,
    floor: int,
) -> float:
    """Expected fraction of displays one photo receives under the scheduler.

    Mirrors bucket_schedule_pick exactly: the featured slot fires every
    ``s = temp_slot_spacing(n, k, floor)`` displays and is shared by the ``k``
    featured photos (1/(s*k) each); the remaining s-1 of every s displays are
    shared by the ``p`` permanents ((s-1)/(s*p) each). Degenerate buckets fall
    back to even rotation within whichever bucket is non-empty.
    """
    p = max(0, perm_count)
    k = max(0, featured_count)
    if is_featured:
        if k == 0:
            return 0.0
        if p == 0:
            return 1.0 / k  # only featured photos -> they rotate among themselves
        s = temp_slot_spacing(n, k, floor)
        return 1.0 / (s * k)
    if p == 0:
        return 0.0
    if k == 0:
        return 1.0 / p  # no featured photos -> pure permanent rotation
    s = temp_slot_spacing(n, k, floor)
    return (s - 1) / (s * p)


def frequency_label(share: float, displays_per_day: float) -> str:
    """Short human 'how often is this shown' string from a share + cadence.

    Picks the coarsest-but-still-meaningful horizon: a rate of >=1/hour reads as
    '~N\u00d7/hour', >=1/day as '~N\u00d7/day', >=1/week as '~N\u00d7/week', and
    anything rarer as an interval ('~once every N weeks'). Best-effort, not exact.
    """
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


# --- Selection for /api/next --------------------------------------------------


class NextPick(NamedTuple):
    """Result of select_next: which image to serve and how it was chosen.

    ``ext`` is the canonical blob's extension (e.g. ``.g16p``/``.jpg``) and
    ``sel_meta`` carries the selection fields plus a resolved ``format``, so the
    handler can build the redirect URL, media type, and ``X-Image-Format`` header
    -- and call mark_served -- without any per-image .json GET.
    """

    image_id: str
    ext: str
    sel_meta: dict
    from_queue: bool
    planned_countdown: int


def plan_next(
    sas: str,
    *,
    temp_min_spacing: int = 4,
    fresh_window_days: int = 7,
    max_temp_share_pct: int = 50,
    blobs: Optional[dict] = None,
    queue: Optional[list] = None,
    countdown: Optional[int] = None,
) -> Optional[NextPick]:
    """Plan the next image without applying display effects.

    Order: first servable id in the queue (explicit one-shots), then the
    two-bucket round-robin rotation. The featured bucket holds temporary photos
    (those with an expiry) and fresh photos (permanent photos uploaded within
    fresh_window_days); permanent photos past the window form the everyday pool.
    Skips expired images and queued ids with no backing blob. Cleanup is left to
    maintenance paths so repeated planning never mutates queue or image state.

    temp_min_spacing is the per-device cadence knob: a featured photo repeats at
    most once per this many displays. fresh_window_days sets how long a new photo
    stays featured (0 disables). max_temp_share_pct caps the featured bucket's
    combined share (via share_pct_to_floor), bounding bulk-upload bursts.

    Selection reads are metadata-only: a single List Blobs (include=metadata)
    call returns every image's selection state, so the cost is ~3 round-trips
    flat (list + queue + schedule) regardless of gallery size. The handler may
    pass already-fetched ``blobs``/``queue``/``countdown`` (e.g. read in parallel)
    to fold those into the single fan-out; any left None are fetched here so
    standalone callers keep working. The proposed countdown is returned in the
    pick and is committed only after the frame acknowledges a display.
    """
    now = datetime.now(timezone.utc)

    # 1 round-trip (or reuse a prefetched one): every image blob's metadata.
    if blobs is None:
        blobs = bs.list_blobs_with_metadata(sas, IMAGE_PREFIX)
    metas: dict[str, dict] = {}
    ext_by_id: dict[str, str] = {}
    for name, md in blobs.items():
        split = _split_image_name(name)
        if not split:
            continue
        image_id, ext = split
        ext_by_id[image_id] = ext
        if "permanent" in md:
            metas[image_id] = _selection_from_blob_metadata(md)
        else:
            # Legacy image: read its JSON projection without stamping metadata.
            # Planning must remain side-effect-free; the standalone backfill owns
            # transport CRC and metadata migration.
            full = read_meta(sas, image_id) or {}
            metas[image_id] = {
                "permanent": bool(full.get("permanent", False)),
                "expires_at": full.get("expires_at"),
                "last_shown_at": full.get("last_shown_at"),
                "served_at": full.get("served_at"),
                "uploaded_at": full.get("uploaded_at"),
                "format": full.get("format"),
                "content_crc32": full.get("content_crc32"),
            }

    existing = set(metas.keys())

    if countdown is None:
        countdown = read_schedule(sas)

    def _pick(image_id: str, *, from_queue: bool, planned_countdown: int) -> NextPick:
        ext = ext_by_id.get(image_id, DEFAULT_IMAGE_EXT)
        sel = dict(metas[image_id])
        # Format is authoritative from the on-disk extension; stamp it so the
        # handler resolves media type + X-Image-Format without a .json GET.
        sel["format"] = format_from_ext(ext)
        return NextPick(
            image_id=image_id,
            ext=ext,
            sel_meta=sel,
            from_queue=from_queue,
            planned_countdown=planned_countdown,
        )

    # 1) Queue: first non-expired id that still has a backing blob.
    if queue is None:
        queue = read_queue(sas)
    for image_id in queue:
        if image_id not in existing:
            continue
        if is_expired(metas[image_id], at=now):
            continue
        return _pick(image_id, from_queue=True, planned_countdown=countdown)

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
            continue
        if not sel.get("permanent"):
            continue
        shown = _parse_iso(sel.get("last_shown_at"))
        if sel.get("expires_at") or is_fresh(sel, now=now, window_days=fresh_window_days):
            temp.append((image_id, shown))  # temporary or fresh -> featured bucket
        else:
            perm.append((image_id, shown))

    chosen, _source, new_countdown = bucket_schedule_pick(
        perm, temp, temp_countdown=countdown, n=temp_min_spacing,
        floor=share_pct_to_floor(max_temp_share_pct),
    )
    if chosen is None:
        return None
    return _pick(chosen, from_queue=False, planned_countdown=new_countdown)


def select_next(
    sas: str,
    *,
    temp_min_spacing: int = 4,
    fresh_window_days: int = 7,
    max_temp_share_pct: int = 50,
    blobs: Optional[dict] = None,
    queue: Optional[list] = None,
    countdown: Optional[int] = None,
) -> Optional[NextPick]:
    """Legacy immediate selection, retaining countdown-at-serve behavior."""
    if blobs is None:
        blobs = bs.list_blobs_with_metadata(sas, IMAGE_PREFIX)
    blobs = dict(blobs)
    if queue is None:
        queue = read_queue(sas)
    queue = list(queue)

    now = datetime.now(timezone.utc)
    metas: dict[str, dict] = {}
    canonical_names: dict[str, str] = {}
    for name, metadata in list(blobs.items()):
        split = _split_image_name(name)
        if split is None:
            continue
        image_id, _ext = split
        canonical_names[image_id] = name
        if "permanent" in metadata:
            metas[image_id] = _selection_from_blob_metadata(metadata)
        else:
            full = read_meta(sas, image_id) or {}
            stamped = selection_blob_metadata(full)
            bs.set_blob_metadata(sas, name, stamped)
            blobs[name] = stamped
            metas[image_id] = _selection_from_blob_metadata(stamped)

    for image_id, selection in list(metas.items()):
        if not selection.get("permanent") and selection.get("served_at"):
            delete_image(sas, image_id)
            blobs.pop(canonical_names[image_id], None)
            metas.pop(image_id, None)

    maintained_queue: list[str] = []
    for image_id in queue:
        if image_id not in metas:
            queue_remove(sas, image_id)
            continue
        if is_expired(metas[image_id], at=now):
            delete_image(sas, image_id)
            blobs.pop(canonical_names[image_id], None)
            metas.pop(image_id, None)
            continue
        maintained_queue.append(image_id)

    for image_id, selection in list(metas.items()):
        if is_expired(selection, at=now):
            delete_image(sas, image_id)
            blobs.pop(canonical_names[image_id], None)
            metas.pop(image_id, None)

    if countdown is None:
        countdown = read_schedule(sas)
    pick = plan_next(
        sas,
        temp_min_spacing=temp_min_spacing,
        fresh_window_days=fresh_window_days,
        max_temp_share_pct=max_temp_share_pct,
        blobs=blobs,
        queue=maintained_queue,
        countdown=countdown,
    )
    if (
        pick is not None
        and not pick.from_queue
        and pick.planned_countdown != countdown
    ):
        write_schedule(sas, pick.planned_countdown)
    return pick


def mark_served(sas: str, image_id: str, sel_meta: dict, ext: str, *, from_queue: bool) -> None:
    """Apply serve-time effects without a .json round-trip.

    Stamps the mutable selection timestamp on the blob's x-ms-meta-* (which is
    authoritative for last_shown_at / served_at -- the gallery merges these over
    the frozen .json) and dequeues one-shots. ``sel_meta`` is the selection dict
    returned by select_next; ``ext`` is the canonical blob extension; ``from_queue``
    is True only for queue (one-shot) picks, so rotation picks skip queue_remove.

    Permanent picks advance rotation via last_shown_at. One-shots stamp served_at
    (marking them for deferred reap on the next poll) and dequeue, but keep the
    blob alive so the /api/next redirect / proxy fetch can still deliver it.
    """
    if sel_meta.get("permanent", False):
        sel_meta["last_shown_at"] = now_iso()
    else:
        sel_meta["served_at"] = now_iso()
    bs.set_blob_metadata(sas, image_name(image_id, ext), selection_blob_metadata(sel_meta))
    if from_queue:
        queue_remove(sas, image_id)


def commit_displayed(sas: str, pending: dict) -> None:
    """Replay-safe absolute display effects for one assignment record."""
    sel_meta = dict(pending.get("sel_meta") or {})
    displayed_at = pending.get("displayed_at") or now_iso()
    if sel_meta.get("permanent", False):
        sel_meta["last_shown_at"] = displayed_at
    else:
        sel_meta["served_at"] = displayed_at
    bs.set_blob_metadata(
        sas,
        image_name(str(pending["image_id"]), str(pending["ext"])),
        selection_blob_metadata(sel_meta),
    )
    if pending.get("from_queue"):
        queue_remove(sas, str(pending["image_id"]))
    write_schedule(sas, int(pending["planned_countdown"]))


# --- Assignment transaction --------------------------------------------------


def assignment_name(device_id: str) -> str:
    digest = hashlib.sha256(device_id.encode("utf-8")).hexdigest()
    return f"{ASSIGNMENT_PREFIX}{digest}.json"


def read_assignment(sas: str, device_id: str) -> tuple[dict, Optional[str]]:
    data, etag = bs.download_json_with_etag(sas, assignment_name(device_id))
    if (
        not isinstance(data, dict)
        or data.get("schema") != ASSIGNMENT_SCHEMA
        or data.get("device_id") != device_id
    ):
        data = {
            "schema": ASSIGNMENT_SCHEMA,
            "device_id": device_id,
            "committed_revision": 0,
            "last_revision": 0,
            "current": None,
            "journal": [],
        }
    return data, etag


def write_assignment(
    sas: str, device_id: str, document: dict, *, etag: Optional[str]
) -> Optional[str]:
    return bs.upload_json(
        sas,
        assignment_name(device_id),
        document,
        if_match=etag,
        if_none_match="*" if etag is None else None,
    )


def revision_newer(left: int, right: int) -> bool:
    delta = (int(left) - int(right)) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


def revision_at_or_before(left: int, right: int) -> bool:
    return int(left) == int(right) or revision_newer(right, left)


def alloc_revision(previous: int) -> int:
    value = (int(previous) + 1) & 0xFFFFFFFF
    return value or 1


def pending_from_pick(device_id: str, revision: int, pick: NextPick) -> dict:
    return {
        "schema": ASSIGNMENT_SCHEMA,
        "device_id": device_id,
        "revision": revision,
        "image_id": pick.image_id,
        "image_key": image_key(pick.image_id),
        "content_crc32": int(pick.sel_meta.get("content_crc32") or 0),
        "format": pick.sel_meta.get("format") or format_from_ext(pick.ext),
        "ext": pick.ext,
        "from_queue": pick.from_queue,
        "planned_countdown": pick.planned_countdown,
        "sel_meta": pick.sel_meta,
        "created_at": now_iso(),
        "state": "pending",
    }


def find_assignment(document: dict, revision: int) -> Optional[dict]:
    current = document.get("current")
    if isinstance(current, dict) and int(current.get("revision", 0)) == int(revision):
        return current
    for entry in document.get("journal") or []:
        if isinstance(entry, dict) and int(entry.get("revision", 0)) == int(revision):
            return entry
    return None


def assignment_is_live(record: dict, blobs: dict, *, at: Optional[datetime] = None) -> bool:
    blob_name = image_name(str(record.get("image_id", "")), str(record.get("ext", "")))
    if blob_name not in blobs:
        return False
    metadata = blobs[blob_name]
    selection = (
        _selection_from_blob_metadata(metadata)
        if "permanent" in metadata
        else record.get("sel_meta") or {}
    )
    return not is_expired(selection, at=at)


def supersede_current(document: dict, *, dequeue_one_shot_sas: Optional[str] = None) -> None:
    current = document.get("current")
    if not isinstance(current, dict):
        return
    superseded = dict(current)
    superseded["state"] = "superseded"
    if dequeue_one_shot_sas is not None and superseded.get("from_queue"):
        queue_remove(dequeue_one_shot_sas, str(superseded["image_id"]))
    previous = [entry for entry in document.get("journal", []) if isinstance(entry, dict)]
    document["journal"] = [superseded, *previous][:ASSIGNMENT_JOURNAL_DEPTH]
    document["current"] = None


def install_pending(document: dict, device_id: str, pick: Optional[NextPick]) -> Optional[dict]:
    if pick is None:
        document["current"] = None
        return None
    revision = alloc_revision(int(document.get("last_revision", 0)))
    document["last_revision"] = revision
    current = pending_from_pick(device_id, revision, pick)
    document["current"] = current
    return current


def invalidate_assignment(sas: str, device_id: str) -> None:
    document, etag = read_assignment(sas, device_id)
    if document.get("current") is None and not document.get("journal"):
        return
    document["current"] = None
    document["journal"] = []
    write_assignment(sas, device_id, document, etag=etag)


def commit_assignment(
    sas: str,
    device_id: str,
    document: dict,
    revision: int,
    expected_image_key: str,
    plan_successor: Callable[[], Optional[NextPick]],
    validate_record: Optional[Callable[[dict], bool]] = None,
) -> tuple[Optional[dict], bool, bool]:
    """Apply effects before mutating the assignment marker.

    Returns ``(current, accepted, marker_changed)``. The caller must CAS-write
    the changed document last; that write is the transaction's linearization
    point. If it fails, replay applies the same absolute effects and retries.
    """
    record = find_assignment(document, revision)
    if record is None or record.get("image_key") != expected_image_key:
        return None, False, False

    committed_revision = int(document.get("committed_revision", 0))
    if revision_at_or_before(revision, committed_revision):
        current = document.get("current")
        return current if isinstance(current, dict) else None, True, False
    if record.get("state") not in ("pending", "superseded"):
        current = document.get("current")
        return current if isinstance(current, dict) else None, True, False
    if validate_record is not None and not validate_record(record):
        return None, False, False

    record["displayed_at"] = record.get("displayed_at") or record["created_at"]
    commit_displayed(sas, record)
    committed = dict(record)
    committed["state"] = "committed"
    document["committed_revision"] = revision

    current = document.get("current")
    if isinstance(current, dict) and int(current.get("revision", 0)) == revision:
        document["current"] = None
        document["journal"] = [committed]
        current = install_pending(document, device_id, plan_successor())
    else:
        document["journal"] = [committed]
        current = document.get("current")
    return current if isinstance(current, dict) else None, True, True

