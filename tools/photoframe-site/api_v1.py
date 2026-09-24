"""HTTP binding for the locked photoframe next-image Version 1 contract."""

from __future__ import annotations

import re
import hashlib
import hmac
import zlib
from urllib.parse import quote

from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse, Response
from starlette.background import BackgroundTask

import blobstore as bs
import config as site_config
import store
from store import TransportDescriptor

router = APIRouter(prefix="/api/v1")

PROTOCOL_HEADERS = {
    "Cache-Control": "private, no-cache",
    "Vary": "Authorization",
}
_KEY_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
_CRC_RE = re.compile(r"^[0-9a-f]{8}$")
_UNAUTHORIZED_BODY = b"Unauthorized"
_MAX_BATCH_COUNT = 17


def unauthorized_response() -> Response:
    return Response(
        content=_UNAUTHORIZED_BODY,
        status_code=401,
        media_type="text/plain",
        headers={**PROTOCOL_HEADERS, "WWW-Authenticate": "Bearer"},
    )


def _bearer_token(request: Request) -> str:
    value = request.headers.get("Authorization", "")
    scheme, separator, token = value.partition(" ")
    return token if separator and scheme.lower() == "bearer" else ""


def _fingerprint(request: Request) -> tuple[str, str] | None:
    key = request.headers.get("Photoframe-Current-Image-Key", "")
    crc = request.headers.get("Photoframe-Current-Content-CRC32", "")
    if _KEY_RE.fullmatch(key) and _CRC_RE.fullmatch(crc):
        return key, crc
    return None


def _descriptor_payload(frame_id: str, descriptor: TransportDescriptor) -> bytes | None:
    payload = bs.download_blob(
        store.DEVICE_CONTAINER,
        f"{frame_id}/images/{descriptor.image_key}/{descriptor.blob_name}",
    )
    actual_crc = f"{zlib.crc32(payload) & 0xffffffff:08x}" if payload is not None else ""
    if (payload is None or len(payload) != descriptor.content_length
            or actual_crc != descriptor.content_crc32):
        return None
    return payload


def _content_headers(descriptor: TransportDescriptor) -> dict[str, str]:
    return {
        **PROTOCOL_HEADERS,
        "Photoframe-Image-Key": descriptor.image_key,
        "Photoframe-Content-CRC32": descriptor.content_crc32,
    }


def _reference_signature(device_id: str, descriptor: TransportDescriptor, secret: str) -> str:
    fields = (device_id, descriptor.image_key, str(descriptor.format_code),
              str(descriptor.content_length), descriptor.content_crc32, descriptor.blob_name)
    return hmac.new(secret.encode("utf-8"),
                    ("photoframe-content-v1\0" + "\0".join(fields)).encode("utf-8"),
                    hashlib.sha256).hexdigest()


def _parse_batch_count(request: Request) -> int | None:
    values = request.query_params.getlist("count")
    if len(values) != 1 or not re.fullmatch(r"[0-9]{1,2}", values[0]):
        return None
    count = int(values[0])
    return count if 1 <= count <= _MAX_BATCH_COUNT else None


@router.get("/next")
def get_next(request: Request) -> Response:
    config = request.app.state.config
    frame = config.authenticate_frame(_bearer_token(request))
    if frame is None:
        return unauthorized_response()

    with request.app.state.next_images.selection_transaction():
        descriptor = request.app.state.next_images.select(frame, _fingerprint(request))
        if descriptor is None:
            return Response(status_code=204, headers=PROTOCOL_HEADERS,
                            background=BackgroundTask(store.record_telemetry, frame.device_id))

        payload = _descriptor_payload(frame.device_id, descriptor)
        if payload is None:
            return Response(
                "Internal Server Error", status_code=500, media_type="text/plain",
                headers=PROTOCOL_HEADERS,
                background=BackgroundTask(store.record_telemetry, frame.device_id),
            )
        if not request.app.state.index.commit_selection(frame.device_id, descriptor):
            return Response(
                "Internal Server Error", status_code=500, media_type="text/plain",
                headers=PROTOCOL_HEADERS,
                background=BackgroundTask(store.record_telemetry, frame.device_id),
            )
    return Response(
        payload,
        media_type=descriptor.media_type,
        headers=_content_headers(descriptor),
        background=BackgroundTask(
            store.record_telemetry, frame.device_id, descriptor.image_key
        )
    )


@router.get("/next-batch")
def get_next_batch(request: Request) -> Response:
    config = request.app.state.config
    frame = config.authenticate_frame(_bearer_token(request))
    if frame is None:
        return unauthorized_response()

    count = _parse_batch_count(request)
    if count is None:
        return Response(
            "Invalid count", status_code=400, media_type="text/plain", headers=PROTOCOL_HEADERS
        )

    excluded: set[tuple[str, str]] = set()
    fingerprint = _fingerprint(request)
    if fingerprint is not None:
        excluded.add(fingerprint)
    entries = []
    secret = site_config.session_secret(request.app.state.data_root)
    selection_conflicts = 0
    while len(entries) < count:
        with request.app.state.next_images.selection_transaction():
            descriptor = request.app.state.next_images.select(
                frame, None, excluded_fingerprints=excluded
            )
        if descriptor is None:
            break
        descriptor_fingerprint = (descriptor.image_key, descriptor.content_crc32)
        payload = _descriptor_payload(frame.device_id, descriptor)
        if payload is None:
            excluded.add(descriptor_fingerprint)
            continue
        del payload
        with request.app.state.next_images.selection_transaction():
            current = request.app.state.next_images.select(
                frame, None, excluded_fingerprints=excluded
            )
            if current != descriptor:
                selection_conflicts += 1
                if selection_conflicts >= 8:
                    break
                continue
            selection_conflicts = 0
            if not request.app.state.index.commit_selection(frame.device_id, descriptor):
                excluded.add(descriptor_fingerprint)
                continue
            excluded.add(descriptor_fingerprint)
            entries.append({
                "image_key": descriptor.image_key,
                "content_crc32": descriptor.content_crc32,
                "media_type": descriptor.media_type,
                "content_length": descriptor.content_length,
                "content_url": (
                    f"/api/v1/content/{descriptor.image_key}/{descriptor.format_code}/"
                    f"{descriptor.content_length}/{descriptor.content_crc32}"
                    f"?blob={quote(descriptor.blob_name, safe='')}"
                    f"&sig={_reference_signature(frame.device_id, descriptor, secret)}"
                ),
            })

    if not entries:
        return Response(
            status_code=204,
            headers=PROTOCOL_HEADERS,
            background=BackgroundTask(store.record_telemetry, frame.device_id),
        )
    return JSONResponse(
        {"images": entries},
        headers=PROTOCOL_HEADERS,
        background=BackgroundTask(store.record_telemetry, frame.device_id),
    )


@router.get("/content/{image_key}/{format_code}/{content_length}/{content_crc32}")
def get_exact_content(
    request: Request,
    image_key: str,
    format_code: str,
    content_length: str,
    content_crc32: str,
) -> Response:
    config = request.app.state.config
    frame = config.authenticate_frame(_bearer_token(request))
    if frame is None:
        return unauthorized_response()
    if (not _KEY_RE.fullmatch(image_key) or not _CRC_RE.fullmatch(content_crc32)
            or not re.fullmatch(r"[0-9]{1,3}", format_code)
            or not re.fullmatch(r"[0-9]{1,20}", content_length)):
        return Response("Not Found", status_code=404, media_type="text/plain",
                        headers=PROTOCOL_HEADERS)

    blob_values = request.query_params.getlist("blob")
    signature_values = request.query_params.getlist("sig")
    if (len(blob_values) != 1 or len(signature_values) != 1
            or not store.is_valid_blob_name(blob_values[0])
            or not re.fullmatch(r"[0-9a-f]{64}", signature_values[0])
            or int(format_code) not in frame.format_codes):
        return Response("Not Found", status_code=404, media_type="text/plain",
                        headers=PROTOCOL_HEADERS)
    descriptor = TransportDescriptor(
        image_key=image_key, format_code=int(format_code),
        content_length=int(content_length), content_crc32=content_crc32,
        blob_name=blob_values[0], media_type=store.MEDIA_TYPES[int(format_code)],
        width=frame.width, height=frame.height,
    )
    expected = _reference_signature(
        frame.device_id, descriptor, site_config.session_secret(request.app.state.data_root))
    if not hmac.compare_digest(signature_values[0], expected):
        return Response("Not Found", status_code=404, media_type="text/plain",
                        headers=PROTOCOL_HEADERS)
    payload = _descriptor_payload(frame.device_id, descriptor)
    if payload is None:
        return Response("Internal Server Error", status_code=500, media_type="text/plain",
                        headers=PROTOCOL_HEADERS)
    return Response(payload, media_type=descriptor.media_type,
                    headers=_content_headers(descriptor))