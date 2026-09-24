"""Single shared next-image selection and transport descriptor core."""

from __future__ import annotations
import threading
from contextlib import contextmanager
from typing import Iterator

from config import Frame
from store import PhotoIndex, TransportDescriptor, read_settings


class NextImageService:
    def __init__(self, index: PhotoIndex) -> None:
        self.index = index
        self._selection_lock = threading.RLock()

    @contextmanager
    def selection_transaction(self) -> Iterator[None]:
        """Keep candidate selection and its history commits from interleaving."""
        with self._selection_lock:
            yield

    def select(
        self,
        frame: Frame,
        fingerprint: tuple[str, str] | None,
        excluded_fingerprints: set[tuple[str, str]] | None = None,
    ) -> TransportDescriptor | None:
        # Device scoping is selection policy only; the HTTP contract never constrained the pool.
        settings = read_settings(frame.device_id)
        with self._selection_lock:
            return self.index.select(
                device_id=frame.device_id,
                width=frame.width,
                height=frame.height,
                format_codes=frame.format_codes,
                variant_keys={code: frame.variant_key(code) for code in frame.format_codes},
                temp_min_spacing=int(settings.get("temp_min_spacing", frame.temp_min_spacing)),
                fresh_window_days=int(settings.get("fresh_window_days", frame.fresh_window_days)),
                max_temp_share_pct=int(settings.get("max_temp_share_pct", frame.max_temp_share_pct)),
                fingerprint=fingerprint,
                excluded_fingerprints=excluded_fingerprints,
            )

    def resolve_reference(
        self,
        frame: Frame,
        *,
        image_key: str,
        format_code: int,
        content_length: int,
        content_crc32: str,
    ) -> TransportDescriptor | None:
        if format_code not in frame.format_codes:
            return None
        return self.index.descriptor_for_reference(
            device_id=frame.device_id,
            image_key=image_key,
            content_crc32=content_crc32,
            format_code=format_code,
            content_length=content_length,
            width=frame.width,
            height=frame.height,
            profile_key=frame.variant_key(format_code),
        )