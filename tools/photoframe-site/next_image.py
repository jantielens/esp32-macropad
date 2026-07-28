"""Single shared next-image selection and transport descriptor core."""

from __future__ import annotations

from config import Frame
from store import PhotoIndex, TransportDescriptor, read_settings


class NextImageService:
    def __init__(self, index: PhotoIndex) -> None:
        self.index = index

    def select(
        self,
        frame: Frame,
        fingerprint: tuple[str, str] | None,
    ) -> TransportDescriptor | None:
        # Device scoping is selection policy only; the HTTP contract never constrained the pool.
        settings = read_settings(frame.device_id)
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
        )