"""Path-safe local filesystem implementation of the storage operation seam."""

from __future__ import annotations

import json
import os
import secrets
from pathlib import Path
from typing import Optional


class LocalBackend:
    """Store blobs below one persistent data root."""

    def __init__(self, root: str | Path) -> None:
        self.root = Path(root).resolve()
        self.root.mkdir(parents=True, exist_ok=True)

    def _path(self, container: str, blob_name: str = "") -> Path:
        candidate = (self.root / container / blob_name).resolve()
        if candidate != self.root and self.root not in candidate.parents:
            raise ValueError("storage path escapes the data root")
        return candidate

    def build_blob_url(self, container: str, blob_name: str) -> str:
        return self._path(container, blob_name).as_uri()

    def upload_blob(
        self,
        container: str,
        blob_name: str,
        payload: bytes,
        content_type: str = "application/octet-stream",
        metadata: Optional[dict] = None,
    ) -> None:
        del content_type
        path = self._path(container, blob_name)
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_name(f".{path.name}.{os.getpid()}.{secrets.token_hex(4)}.tmp")
        temp.write_bytes(payload)
        os.replace(temp, path)
        if metadata is not None:
            self.set_blob_metadata(container, blob_name, metadata)

    def set_blob_metadata(self, container: str, blob_name: str, metadata: dict) -> None:
        path = self._path(container, f"{blob_name}.metadata.json")
        path.parent.mkdir(parents=True, exist_ok=True)
        payload = json.dumps(metadata, separators=(",", ":"), sort_keys=True).encode()
        temp = path.with_name(f".{path.name}.{os.getpid()}.{secrets.token_hex(4)}.tmp")
        temp.write_bytes(payload)
        os.replace(temp, path)

    def download_blob(self, container: str, blob_name: str) -> Optional[bytes]:
        path = self._path(container, blob_name)
        try:
            return path.read_bytes()
        except FileNotFoundError:
            return None

    def delete_blob(self, container: str, blob_name: str) -> bool:
        deleted = False
        for path in (
            self._path(container, blob_name),
            self._path(container, f"{blob_name}.metadata.json"),
        ):
            try:
                path.unlink()
                deleted = True
            except FileNotFoundError:
                pass
        return deleted

    def list_blobs(self, container: str, prefix: str) -> list[str]:
        base = self._path(container)
        if not base.exists():
            return []
        return sorted(
            path.relative_to(base).as_posix()
            for path in base.rglob("*")
            if path.is_file()
            and not path.name.endswith(".metadata.json")
            and path.relative_to(base).as_posix().startswith(prefix)
        )

    def list_blobs_with_metadata(self, container: str, prefix: str) -> dict[str, dict]:
        result = {}
        for name in self.list_blobs(container, prefix):
            metadata_path = self._path(container, f"{name}.metadata.json")
            try:
                value = json.loads(metadata_path.read_text(encoding="utf-8"))
                result[name] = value if isinstance(value, dict) else {}
            except (FileNotFoundError, UnicodeDecodeError, ValueError):
                result[name] = {}
        return result