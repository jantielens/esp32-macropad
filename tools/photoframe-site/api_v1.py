"""HTTP binding for the locked photoframe next-image Version 1 contract."""

from __future__ import annotations

import re
import zlib

from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse, Response
from starlette.background import BackgroundTask

import blobstore as bs
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
    with request.app.state.next_images.selection_transaction():
        while len(entries) < count:
            descriptor = request.app.state.next_images.select(
                frame, None, excluded_fingerprints=excluded
            )
            if descriptor is None:
                break
            descriptor_fingerprint = (descriptor.image_key, descriptor.content_crc32)
            excluded.add(descriptor_fingerprint)
            payload = _descriptor_payload(frame.device_id, descriptor)
            if payload is None:
                continue
            del payload
            if not request.app.state.index.commit_selection(frame.device_id, descriptor):
                continue
            entries.append({
                "image_key": descriptor.image_key,
                "content_crc32": descriptor.content_crc32,
                "media_type": descriptor.media_type,
                "content_length": descriptor.content_length,
                "content_url": (
                    f"/api/v1/content/{descriptor.image_key}/{descriptor.format_code}/"
                    f"{descriptor.content_length}/{descriptor.content_crc32}"
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

    descriptor = request.app.state.next_images.resolve_reference(
        frame,
        image_key=image_key,
        format_code=int(format_code),
        content_length=int(content_length),
        content_crc32=content_crc32,
    )
    if descriptor is None:
        return Response("Not Found", status_code=404, media_type="text/plain",
                        headers=PROTOCOL_HEADERS)
    payload = _descriptor_payload(frame.device_id, descriptor)
    if payload is None:
        return Response("Internal Server Error", status_code=500, media_type="text/plain",
                        headers=PROTOCOL_HEADERS)
    return Response(payload, media_type=descriptor.media_type,
                    headers=_content_headers(descriptor))