"""Azure Blob Storage helpers using container SAS URLs over the REST API.

Each device owns one container; the site is given a ``container_sas_url`` of the
form ``https://<account>.blob.core.windows.net/<container>?<sas-token>``. All
access is by building per-blob URLs from that SAS and calling the REST endpoints
directly with stdlib ``urllib`` -- no azure-storage SDK dependency.

Blob layout (single-copy, blob-authoritative):

    images/<id>.g16p          canonical panel image
    images/<id>__thumb.png    gallery thumbnail
    images/<id>.json          authoritative per-image meta
    state/queue.json          ordered list of ids to show next (soft-state)
"""

from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from typing import Optional
from urllib import error, parse
from urllib import request as urllib_request

API_VERSION = "2020-10-02"


class BlobError(RuntimeError):
    """Raised when an Azure Blob REST call fails."""

    def __init__(self, status: int, message: str) -> None:
        super().__init__(f"blob error {status}: {message}")
        self.status = status


def _split_sas(container_sas_url: str) -> tuple[str, str]:
    base, _, token = container_sas_url.partition("?")
    if not token:
        raise ValueError("SAS URL must include a query string token")
    parsed = parse.urlsplit(base)
    if parsed.scheme not in ("http", "https") or not parsed.netloc:
        raise ValueError("Invalid container SAS URL (expected https://<host>/<container>?<sas>)")
    return base.rstrip("/"), token


def build_blob_url(container_sas_url: str, blob_name: str) -> str:
    container, token = _split_sas(container_sas_url)
    blob_path = parse.quote(blob_name, safe="/")
    return f"{container}/{blob_path}?{token}"


def _redacted(url: str) -> str:
    try:
        p = parse.urlsplit(url)
        return f"{p.scheme}://{p.netloc}{p.path}"
    except Exception:
        return "<invalid url>"


def upload_blob(
    container_sas_url: str,
    blob_name: str,
    payload: bytes,
    content_type: str = "application/octet-stream",
) -> None:
    url = build_blob_url(container_sas_url, blob_name)
    req = urllib_request.Request(url, method="PUT", data=payload)
    req.add_header("x-ms-blob-type", "BlockBlob")
    req.add_header("x-ms-version", API_VERSION)
    req.add_header("Content-Type", content_type)
    req.add_header("Content-Length", str(len(payload)))
    try:
        with urllib_request.urlopen(req, timeout=120) as resp:
            if resp.status not in (200, 201):
                raise BlobError(resp.status, "unexpected status uploading blob")
    except error.HTTPError as e:
        raise BlobError(e.code, _redacted(url)) from e
    except error.URLError as e:
        raise BlobError(0, f"network error: {e.reason}") from e


def download_blob(container_sas_url: str, blob_name: str) -> Optional[bytes]:
    """Return blob bytes, or ``None`` if the blob does not exist (404)."""
    url = build_blob_url(container_sas_url, blob_name)
    req = urllib_request.Request(url, method="GET")
    req.add_header("x-ms-version", API_VERSION)
    try:
        with urllib_request.urlopen(req, timeout=60) as resp:
            return resp.read()
    except error.HTTPError as e:
        if e.code == 404:
            return None
        raise BlobError(e.code, _redacted(url)) from e
    except error.URLError as e:
        raise BlobError(0, f"network error: {e.reason}") from e


def delete_blob(container_sas_url: str, blob_name: str) -> bool:
    """Delete a blob. Returns ``True`` if deleted, ``False`` if it was absent."""
    url = build_blob_url(container_sas_url, blob_name)
    req = urllib_request.Request(url, method="DELETE")
    req.add_header("x-ms-version", API_VERSION)
    try:
        with urllib_request.urlopen(req, timeout=30) as resp:
            return resp.status in (200, 202)
    except error.HTTPError as e:
        if e.code == 404:
            return False
        raise BlobError(e.code, _redacted(url)) from e
    except error.URLError as e:
        raise BlobError(0, f"network error: {e.reason}") from e


def list_blobs(container_sas_url: str, prefix: str, *, max_results: int = 1000) -> list[str]:
    container, token = _split_sas(container_sas_url)
    marker: Optional[str] = None
    names: list[str] = []
    while True:
        query = (
            token
            + "&restype=container&comp=list"
            + f"&maxresults={max_results}"
            + "&prefix="
            + parse.quote(prefix, safe="")
        )
        if marker:
            query += "&marker=" + parse.quote(marker, safe="")
        url = f"{container}?{query}"
        req = urllib_request.Request(url, method="GET")
        req.add_header("x-ms-version", API_VERSION)
        try:
            with urllib_request.urlopen(req, timeout=30) as resp:
                body = resp.read()
        except error.HTTPError as e:
            raise BlobError(e.code, _redacted(url)) from e
        except error.URLError as e:
            raise BlobError(0, f"network error: {e.reason}") from e
        root = ET.fromstring(body)
        for elem in root.findall(".//{*}Blob/{*}Name"):
            if elem.text:
                names.append(elem.text)
        marker_elem = root.find(".//{*}NextMarker")
        marker = marker_elem.text if (marker_elem is not None and marker_elem.text) else None
        if not marker:
            break
    return names


# --- JSON convenience wrappers ------------------------------------------------


def download_json(container_sas_url: str, blob_name: str) -> Optional[dict]:
    raw = download_blob(container_sas_url, blob_name)
    if raw is None:
        return None
    try:
        return json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        # Corrupt soft-state degrades to "absent" rather than breaking the caller.
        return None


def upload_json(container_sas_url: str, blob_name: str, obj) -> None:
    payload = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    upload_blob(container_sas_url, blob_name, payload, content_type="application/json")
