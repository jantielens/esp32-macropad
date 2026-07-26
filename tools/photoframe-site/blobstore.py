"""Backend-dispatched storage seam used by the photoframe site."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Optional, Protocol

from local_backend import LocalBackend


class StorageBackend(Protocol):
    def build_blob_url(self, container: str, blob_name: str) -> str: ...
    def upload_blob(self, container: str, blob_name: str, payload: bytes,
                    content_type: str = "application/octet-stream",
                    metadata: Optional[dict] = None) -> None: ...
    def set_blob_metadata(self, container: str, blob_name: str, metadata: dict) -> None: ...
    def download_blob(self, container: str, blob_name: str) -> Optional[bytes]: ...
    def delete_blob(self, container: str, blob_name: str) -> bool: ...
    def list_blobs(self, container: str, prefix: str) -> list[str]: ...
    def list_blobs_with_metadata(self, container: str, prefix: str) -> dict[str, dict]: ...


_backend: StorageBackend | None = None


def configure_local(data_root: str | Path) -> None:
    global _backend
    _backend = LocalBackend(data_root)


def _active() -> StorageBackend:
    if _backend is None:
        raise RuntimeError("storage backend is not configured")
    return _backend


def build_blob_url(container: str, blob_name: str) -> str:
    return _active().build_blob_url(container, blob_name)


def upload_blob(container: str, blob_name: str, payload: bytes,
                content_type: str = "application/octet-stream",
                metadata: Optional[dict] = None) -> None:
    _active().upload_blob(container, blob_name, payload, content_type, metadata)


def set_blob_metadata(container: str, blob_name: str, metadata: dict) -> None:
    _active().set_blob_metadata(container, blob_name, metadata)


def download_blob(container: str, blob_name: str) -> Optional[bytes]:
    return _active().download_blob(container, blob_name)


def delete_blob(container: str, blob_name: str) -> bool:
    return _active().delete_blob(container, blob_name)


def list_blobs(container: str, prefix: str) -> list[str]:
    return _active().list_blobs(container, prefix)


def list_blobs_with_metadata(container: str, prefix: str) -> dict[str, dict]:
    return _active().list_blobs_with_metadata(container, prefix)


def download_json(container: str, blob_name: str):
    raw = download_blob(container, blob_name)
    if raw is None:
        return None
    try:
        return json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return None


def upload_json(container: str, blob_name: str, obj) -> None:
    payload = json.dumps(obj, separators=(",", ":"), sort_keys=True).encode("utf-8")
    upload_blob(container, blob_name, payload, content_type="application/json")