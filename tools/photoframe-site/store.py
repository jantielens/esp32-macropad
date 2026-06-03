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

import re
from datetime import datetime, timezone
from typing import Optional

import blobstore as bs

IMAGE_PREFIX = "images/"
QUEUE_BLOB = "state/queue.json"
G16P_EXT = ".g16p"
THUMB_SUFFIX = "__thumb.png"
META_EXT = ".json"

# Image ids are server-minted; restrict to a safe charset to prevent blob-name
# injection / path traversal when building URLs.
_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


def is_valid_id(image_id: str) -> bool:
    return bool(_ID_RE.match(image_id))


def g16p_name(image_id: str) -> str:
    return f"{IMAGE_PREFIX}{image_id}{G16P_EXT}"


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
    for key in ("expires_at", "last_shown_at", "served_at"):
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


# --- Per-image meta -----------------------------------------------------------


def read_meta(sas: str, image_id: str) -> Optional[dict]:
    return bs.download_json(sas, meta_name(image_id))


def write_meta(sas: str, image_id: str, meta: dict) -> None:
    bs.upload_json(sas, meta_name(image_id), meta)


def list_image_ids(sas: str) -> list[str]:
    """All image ids that have a canonical G16P blob."""
    names = bs.list_blobs(sas, IMAGE_PREFIX)
    ids: list[str] = []
    for name in names:
        if name.endswith(G16P_EXT):
            image_id = name[len(IMAGE_PREFIX):-len(G16P_EXT)]
            if is_valid_id(image_id):
                ids.append(image_id)
    return ids


def delete_image(sas: str, image_id: str) -> None:
    """Remove an image's blob, thumb, meta, and any queue reference."""
    bs.delete_blob(sas, g16p_name(image_id))
    bs.delete_blob(sas, thumb_name(image_id))
    bs.delete_blob(sas, meta_name(image_id))
    queue_remove(sas, image_id)


def store_image(
    sas: str,
    image_id: str,
    g16p_bytes: bytes,
    thumb_png: bytes,
    meta: dict,
    *,
    enqueue: bool = True,
) -> None:
    """Persist a new image (blob + thumb + meta) and optionally enqueue it."""
    bs.upload_blob(
        sas,
        g16p_name(image_id),
        g16p_bytes,
        content_type="application/octet-stream",
        metadata=selection_blob_metadata(meta),
    )
    bs.upload_blob(sas, thumb_name(image_id), thumb_png, content_type="image/png")
    write_meta(sas, image_id, meta)
    if enqueue:
        # LIFO: a new upload jumps to the front so the most recent action (upload
        # or "Show next") is what the device serves next.
        queue_unshift(sas, image_id)


# --- Selection for /api/next --------------------------------------------------


def select_next(sas: str) -> Optional[str]:
    """Pick the next image id to serve, or None.

    Order: first servable id in the queue, then least-recently-shown permanent
    image (rotation). Skips expired images and queued ids with no backing blob.
    Cleans up expired images opportunistically.

    Selection reads are metadata-only: a single List Blobs (include=metadata)
    call returns every image's selection state, so the cost is ~2 round-trips
    flat (list + queue) regardless of gallery size.
    """
    now = datetime.now(timezone.utc)

    # 1 round-trip: every image blob's selection metadata, inline.
    blobs = bs.list_blobs_with_metadata(sas, IMAGE_PREFIX)
    metas: dict[str, dict] = {}
    for name, md in blobs.items():
        if not name.endswith(G16P_EXT):
            continue
        image_id = name[len(IMAGE_PREFIX):-len(G16P_EXT)]
        if not is_valid_id(image_id):
            continue
        if "permanent" in md:
            metas[image_id] = _selection_from_blob_metadata(md)
        else:
            # Backfill: image uploaded before metadata stamping existed. Read its
            # .json once and stamp the blob so subsequent polls are metadata-only.
            full = read_meta(sas, image_id) or {}
            bs.set_blob_metadata(sas, g16p_name(image_id), selection_blob_metadata(full))
            metas[image_id] = {
                "permanent": bool(full.get("permanent", False)),
                "expires_at": full.get("expires_at"),
                "last_shown_at": full.get("last_shown_at"),
                "served_at": full.get("served_at"),
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

    # 2) Rotation: least-recently-shown permanent, non-expired image.
    best_id: Optional[str] = None
    best_shown: Optional[datetime] = None
    for image_id in existing:
        sel = metas[image_id]
        if is_expired(sel, at=now):
            delete_image(sas, image_id)
            continue
        if not sel.get("permanent"):
            continue
        shown = _parse_iso(sel.get("last_shown_at"))
        # nulls (never shown) sort first
        if best_id is None or (
            best_shown is not None and (shown is None or shown < best_shown)
        ):
            best_id = image_id
            best_shown = shown
    return best_id


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
    bs.set_blob_metadata(sas, g16p_name(image_id), selection_blob_metadata(meta))
    queue_remove(sas, image_id)
