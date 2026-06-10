"""Azure Blob Storage helpers using container SAS URLs over the REST API.

Each device owns one container; the site is given a ``container_sas_url`` of the
form ``https://<account>.blob.core.windows.net/<container>?<sas-token>``. All
access is by building per-blob URLs from that SAS and calling the REST endpoints
directly with stdlib ``http.client`` -- no azure-storage SDK dependency. A
thread-local keep-alive connection per host is reused across calls so a burst of
blob operations (e.g. one /api/next poll) pays the TCP+TLS handshake once
instead of per call.

Blob layout (single-copy, blob-authoritative):

    images/<id>.g16p          canonical panel image
    images/<id>__thumb.png    gallery thumbnail
    images/<id>.json          authoritative per-image meta
    state/queue.json          ordered list of ids to show next (soft-state)
"""

from __future__ import annotations

import http.client
import json
import threading
import time
import xml.etree.ElementTree as ET
from typing import Optional
from urllib import parse

API_VERSION = "2020-10-02"


class BlobError(RuntimeError):
    """Raised when an Azure Blob REST call fails."""

    def __init__(self, status: int, message: str) -> None:
        super().__init__(f"blob error {status}: {message}")
        self.status = status


# --- Request profiling --------------------------------------------------------
#
# Optional per-request accounting of Azure Blob REST round-trips. The /api/next
# handler arms it with profile_begin() (which returns a sink) and drains it with
# profile_collect() to surface a Server-Timing breakdown. The sink is a
# lock-guarded object rather than thread-local state so the round-trips a request
# fans out across worker threads (parallel reads) all land in the same per-
# request tally; use_profile_sink() binds that sink in a worker thread for the
# duration of its work. When no sink is bound the spans are near-free no-ops, so
# leaving the instrumentation in place costs nothing on un-profiled requests.

_profile = threading.local()


class _ProfileSink:
    """Per-request, thread-safe tally of blob REST round-trips."""

    __slots__ = ("_lock", "calls")

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.calls: list[tuple[str, float]] = []

    def add(self, op: str, dur_ms: float) -> None:
        with self._lock:
            self.calls.append((op, dur_ms))

    def summary(self) -> tuple[int, float, dict]:
        with self._lock:
            calls = list(self.calls)
        total_ms = 0.0
        per_op: dict[str, list] = {}
        for op, dur in calls:
            total_ms += dur
            entry = per_op.setdefault(op, [0, 0.0])
            entry[0] += 1
            entry[1] += dur
        return len(calls), total_ms, per_op


def profile_begin() -> _ProfileSink:
    """Arm blob round-trip profiling for the current request; return its sink."""
    sink = _ProfileSink()
    _profile.sink = sink
    return sink


def profile_collect() -> tuple[int, float, dict]:
    """Disarm profiling; return ``(call_count, total_ms, {op: [count, ms]})``."""
    sink = getattr(_profile, "sink", None)
    _profile.sink = None
    if sink is None:
        return 0, 0.0, {}
    return sink.summary()


class use_profile_sink:
    """Bind a parent request's profiling sink onto a worker thread."""

    __slots__ = ("sink", "_prev")

    def __init__(self, sink: Optional[_ProfileSink]) -> None:
        self.sink = sink

    def __enter__(self) -> "use_profile_sink":
        self._prev = getattr(_profile, "sink", None)
        _profile.sink = self.sink
        return self

    def __exit__(self, *exc) -> bool:
        _profile.sink = self._prev
        return False


class _profile_span:
    """Context manager recording one blob REST round-trip when profiling is armed."""

    __slots__ = ("op", "_t")

    def __init__(self, op: str) -> None:
        self.op = op

    def __enter__(self) -> "_profile_span":
        self._t = time.perf_counter()
        return self

    def __exit__(self, *exc) -> bool:
        sink = getattr(_profile, "sink", None)
        if sink is not None:
            sink.add(self.op, (time.perf_counter() - self._t) * 1000.0)
        return False


# --- Transport: pooled keep-alive connections ---------------------------------
#
# One persistent connection per (thread, host). All of a device's blobs live on
# the same host, so a poll's ~handful of REST calls reuse a single TLS session.
# Connections are thread-local: parallel reads run in their own worker threads
# and so naturally get their own connections, with no cross-thread sharing of a
# non-thread-safe http.client object.

_pool = threading.local()


def _conn_for(host: str, scheme: str, timeout: float) -> http.client.HTTPConnection:
    conns = getattr(_pool, "conns", None)
    if conns is None:
        conns = {}
        _pool.conns = conns
    conn = conns.get(host)
    if conn is None:
        if scheme == "https":
            conn = http.client.HTTPSConnection(host, timeout=timeout)
        else:
            conn = http.client.HTTPConnection(host, timeout=timeout)
        conns[host] = conn
    return conn


def _drop_conn(host: str) -> None:
    conns = getattr(_pool, "conns", None)
    if conns and host in conns:
        try:
            conns[host].close()
        except Exception:
            pass
        del conns[host]


def _request(
    method: str,
    url: str,
    *,
    op: str,
    headers: Optional[dict] = None,
    data: Optional[bytes] = None,
    timeout: float = 30.0,
) -> tuple[int, dict, bytes]:
    """Perform one Azure Blob REST call over a pooled keep-alive connection.

    Returns ``(status, response_headers, body)``. HTTP error statuses (404, 5xx)
    are returned, not raised -- callers map them to BlobError/None as needed. A
    closed keep-alive socket (idle server-side timeout) is retried once on a
    fresh connection; the calls are idempotent (overwriting PUTs, GET/DELETE).
    """
    parts = parse.urlsplit(url)
    host = parts.netloc
    scheme = parts.scheme
    target = parts.path + (f"?{parts.query}" if parts.query else "")
    hdrs = dict(headers or {})
    last_exc: Optional[Exception] = None
    for attempt in range(2):
        conn = _conn_for(host, scheme, timeout)
        try:
            if conn.sock is not None:
                conn.sock.settimeout(timeout)
            with _profile_span(op):
                conn.request(method, target, body=data, headers=hdrs)
                resp = conn.getresponse()
                body = resp.read()  # drain so the connection can be reused
                status = resp.status
                resp_headers = {k.lower(): v for k, v in resp.getheaders()}
            return status, resp_headers, body
        except (http.client.HTTPException, ConnectionError, TimeoutError, OSError) as exc:
            last_exc = exc
            _drop_conn(host)
    raise BlobError(0, f"network error: {last_exc}")


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
    metadata: Optional[dict] = None,
) -> None:
    url = build_blob_url(container_sas_url, blob_name)
    headers = {
        "x-ms-blob-type": "BlockBlob",
        "x-ms-version": API_VERSION,
        "Content-Type": content_type,
        "Content-Length": str(len(payload)),
    }
    # x-ms-meta-* pairs ride on the blob itself, so selection state stays
    # blob-authoritative and is readable via List Blobs (no per-blob GET).
    for key, value in (metadata or {}).items():
        headers[f"x-ms-meta-{key}"] = str(value)
    status, _, _ = _request("PUT", url, op="upload", headers=headers, data=payload, timeout=120.0)
    if status not in (200, 201):
        raise BlobError(status, "unexpected status uploading blob")


def set_blob_metadata(container_sas_url: str, blob_name: str, metadata: dict) -> None:
    """Replace a blob's x-ms-meta-* metadata (Set Blob Metadata; headers only).

    Cheap headers-only PUT used to update selection state (last_shown_at /
    served_at) without rewriting the ~1.3 MB payload.
    """
    url = build_blob_url(container_sas_url, blob_name) + "&comp=metadata"
    headers = {"x-ms-version": API_VERSION, "Content-Length": "0"}
    for key, value in (metadata or {}).items():
        headers[f"x-ms-meta-{key}"] = str(value)
    status, _, _ = _request("PUT", url, op="set_meta", headers=headers, data=b"", timeout=30.0)
    if status not in (200, 201):
        raise BlobError(status, "unexpected status setting metadata")


def download_blob(container_sas_url: str, blob_name: str) -> Optional[bytes]:
    """Return blob bytes, or ``None`` if the blob does not exist (404)."""
    url = build_blob_url(container_sas_url, blob_name)
    headers = {"x-ms-version": API_VERSION}
    status, _, body = _request("GET", url, op="download", headers=headers, timeout=60.0)
    if status == 404:
        return None
    if status not in (200, 206):
        raise BlobError(status, _redacted(url))
    return body


def delete_blob(container_sas_url: str, blob_name: str) -> bool:
    """Delete a blob. Returns ``True`` if deleted, ``False`` if it was absent."""
    url = build_blob_url(container_sas_url, blob_name)
    headers = {"x-ms-version": API_VERSION}
    status, _, _ = _request("DELETE", url, op="delete", headers=headers, timeout=30.0)
    if status == 404:
        return False
    if status in (200, 202):
        return True
    raise BlobError(status, _redacted(url))


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
        status, _, body = _request("GET", url, op="list", headers={"x-ms-version": API_VERSION}, timeout=30.0)
        if status != 200:
            raise BlobError(status, _redacted(url))
        root = ET.fromstring(body)
        for elem in root.findall(".//{*}Blob/{*}Name"):
            if elem.text:
                names.append(elem.text)
        marker_elem = root.find(".//{*}NextMarker")
        marker = marker_elem.text if (marker_elem is not None and marker_elem.text) else None
        if not marker:
            break
    return names


def list_blobs_with_metadata(
    container_sas_url: str, prefix: str, *, max_results: int = 1000
) -> dict[str, dict]:
    """List blobs under ``prefix``, returning ``{name: {meta_key: value}}``.

    Uses List Blobs with ``include=metadata`` so every blob's x-ms-meta-* pairs
    come back inline in a single response -- one round-trip for the whole
    container instead of a GET per blob. Blobs with no metadata map to ``{}``.
    """
    container, token = _split_sas(container_sas_url)
    marker: Optional[str] = None
    out: dict[str, dict] = {}
    while True:
        query = (
            token
            + "&restype=container&comp=list"
            + "&include=metadata"
            + f"&maxresults={max_results}"
            + "&prefix="
            + parse.quote(prefix, safe="")
        )
        if marker:
            query += "&marker=" + parse.quote(marker, safe="")
        url = f"{container}?{query}"
        status, _, body = _request("GET", url, op="list_meta", headers={"x-ms-version": API_VERSION}, timeout=30.0)
        if status != 200:
            raise BlobError(status, _redacted(url))
        root = ET.fromstring(body)
        for blob in root.findall(".//{*}Blob"):
            name_el = blob.find("{*}Name")
            if name_el is None or not name_el.text:
                continue
            md: dict = {}
            meta_el = blob.find("{*}Metadata")
            if meta_el is not None:
                for child in meta_el:
                    # Azure title-cases metadata names in the List response
                    # (Permanent, Last_Shown_At). Metadata names are
                    # case-insensitive, so normalize to lowercase for callers.
                    tag = child.tag.split("}")[-1].lower()
                    md[tag] = child.text or ""
            out[name_el.text] = md
        marker_elem = root.find(".//{*}NextMarker")
        marker = marker_elem.text if (marker_elem is not None and marker_elem.text) else None
        if not marker:
            break
    return out


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
