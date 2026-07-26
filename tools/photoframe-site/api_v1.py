"""HTTP binding for the locked photoframe next-image Version 1 contract."""

from __future__ import annotations

import re
import zlib

from fastapi import APIRouter, Request
from fastapi.responses import Response

import blobstore as bs

router = APIRouter(prefix="/api/v1")

PROTOCOL_HEADERS = {
    "Cache-Control": "private, no-cache",
    "Vary": "Authorization",
}
_KEY_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
_CRC_RE = re.compile(r"^[0-9a-f]{8}$")
_UNAUTHORIZED_BODY = b"Unauthorized"


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


@router.get("/next")
def get_next(request: Request) -> Response:
    config = request.app.state.config
    frame = config.authenticate_frame(_bearer_token(request))
    if frame is None:
        return unauthorized_response()

    descriptor = request.app.state.next_images.select(frame, _fingerprint(request))
    if descriptor is None:
        return Response(status_code=204, headers=PROTOCOL_HEADERS)

    payload = bs.download_blob("photos", descriptor.blob_name)
    actual_crc = f"{zlib.crc32(payload) & 0xffffffff:08x}" if payload is not None else ""
    if payload is None or len(payload) != descriptor.content_length or actual_crc != descriptor.content_crc32:
        return Response("Internal Server Error", status_code=500, media_type="text/plain", headers=PROTOCOL_HEADERS)
    request.app.state.index.commit_selection(descriptor)
    return Response(
        payload,
        media_type=descriptor.media_type,
        headers={
            **PROTOCOL_HEADERS,
            "Photoframe-Image-Key": descriptor.image_key,
            "Photoframe-Content-CRC32": descriptor.content_crc32,
        },
    )