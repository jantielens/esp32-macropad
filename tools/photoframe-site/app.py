"""E-paper photoframe site composition root."""

from __future__ import annotations

import asyncio
import logging
import os
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Awaitable, Callable

from fastapi import FastAPI, Request
from fastapi.responses import Response
from starlette.exceptions import HTTPException as StarletteHTTPException
from starlette.middleware.sessions import SessionMiddleware
from starlette.types import Message, Receive, Scope, Send

import api_v1
import archive as archive_ops
import blobstore
import config
import ui
from next_image import NextImageService
from store import PhotoIndex

logger = logging.getLogger("epaper-photoframe")
DATA_ROOT = Path(os.environ.get("PHOTOFRAME_DATA_DIR", Path(__file__).with_name("data")))
_FORM_BODY_LIMIT = 1024 * 1024
_IMAGE_BODY_LIMIT = 32 * 1024 * 1024
_ARCHIVE_BODY_LIMIT = archive_ops.MAX_ARCHIVE_BYTES + 2 * 1024 * 1024
_BODY_TIMEOUT_SECONDS = 30.0


class _BodyLimitExceeded(Exception):
    pass


class _BodyReceiveTimedOut(Exception):
    pass


def _request_body_limit(path: str) -> int:
    if path in ("/devices/import", "/site/import"):
        return _ARCHIVE_BODY_LIMIT
    if path in ("/preview-base", "/upload"):
        return _IMAGE_BODY_LIMIT
    return _FORM_BODY_LIMIT


class RequestBodyGuardMiddleware:
    def __init__(self, application: Callable[[Scope, Receive, Send], Awaitable[None]]) -> None:
        self.application = application

    async def __call__(self, scope: Scope, receive: Receive, send: Send) -> None:
        if scope["type"] != "http" or scope.get("method") not in ("POST", "PUT", "PATCH"):
            await self.application(scope, receive, send)
            return

        limit = _request_body_limit(str(scope.get("path", "")))
        headers = {key.lower(): value for key, value in scope.get("headers", [])}
        try:
            content_length = int(headers.get(b"content-length", b"0"))
        except ValueError:
            await self._reject(scope, receive, send, 400, "Invalid Content-Length")
            return
        if content_length < 0:
            await self._reject(scope, receive, send, 400, "Invalid Content-Length")
            return
        if content_length > limit:
            await self._reject(scope, receive, send, 413, "Request body too large")
            return

        received = 0
        deadline = time.monotonic() + _BODY_TIMEOUT_SECONDS
        response_started = False

        async def guarded_receive() -> Message:
            nonlocal received
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise _BodyReceiveTimedOut
            try:
                message = await asyncio.wait_for(receive(), timeout=remaining)
            except TimeoutError as exc:
                raise _BodyReceiveTimedOut from exc
            if message["type"] == "http.request":
                received += len(message.get("body", b""))
                if received > limit:
                    raise _BodyLimitExceeded
            return message

        async def guarded_send(message: Message) -> None:
            nonlocal response_started
            if message["type"] == "http.response.start":
                response_started = True
            await send(message)

        try:
            await self.application(scope, guarded_receive, guarded_send)
        except _BodyLimitExceeded:
            if response_started:
                raise
            await self._reject(scope, receive, send, 413, "Request body too large")
        except _BodyReceiveTimedOut:
            if response_started:
                raise
            await self._reject(scope, receive, send, 408, "Request body timed out")

    @staticmethod
    async def _reject(scope: Scope, receive: Receive, send: Send,
                      status_code: int, message: str) -> None:
        response = Response(
            message,
            status_code=status_code,
            media_type="text/plain",
            headers={
                "Cache-Control": "no-store",
                "X-Content-Type-Options": "nosniff",
                "X-Frame-Options": "DENY",
                "Referrer-Policy": "no-referrer",
            },
        )
        await response(scope, receive, send)


@asynccontextmanager
async def lifespan(application: FastAPI):
    archive_ops.recover_site_import(DATA_ROOT)
    config.initialize_data_root(DATA_ROOT)
    blobstore.configure_local(DATA_ROOT)
    application.state.data_root = DATA_ROOT
    application.state.config = config.load_config(DATA_ROOT)
    application.state.index = PhotoIndex()
    application.state.index.rebuild()
    application.state.next_images = NextImageService(application.state.index)
    application.state.restart_required = False
    yield


app = FastAPI(title="E-paper Photoframe", lifespan=lifespan)

_cookie_secure = os.environ.get("COOKIE_SECURE", "").lower() in ("1", "true", "yes")
archive_ops.recover_site_import(DATA_ROOT)
_secret_key = config.session_secret(DATA_ROOT)

app.add_middleware(
    SessionMiddleware,
    secret_key=_secret_key,
    https_only=_cookie_secure,
    same_site="lax",
)
app.add_middleware(RequestBodyGuardMiddleware)


@app.middleware("http")
async def response_headers(request: Request, call_next):
    if getattr(request.app.state, "restart_required", False) and request.url.path != "/healthz":
        return Response("Restart required", status_code=503, media_type="text/plain",
                        headers={"Cache-Control": "no-store"})
    response = await call_next(request)
    response.headers.setdefault("X-Content-Type-Options", "nosniff")
    response.headers.setdefault("X-Frame-Options", "DENY")
    response.headers.setdefault("Referrer-Policy", "no-referrer")
    response.headers.setdefault(
        "Content-Security-Policy",
        "default-src 'self'; img-src 'self' data: blob:; "
        "style-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net; "
        "script-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net; "
        "connect-src 'self' https://cdn.jsdelivr.net; "
        "frame-ancestors 'none'; base-uri 'self'; form-action 'self'",
    )
    if request.url.path.startswith("/api/"):
        response.headers.setdefault("Cache-Control", "private, no-cache")
        response.headers.setdefault("Vary", "Authorization")
    return response


@app.exception_handler(StarletteHTTPException)
async def protocol_http_error(request: Request, exc: StarletteHTTPException) -> Response:
    headers = dict(exc.headers or {})
    if request.url.path.startswith("/api/"):
        headers.update(api_v1.PROTOCOL_HEADERS)
    return Response(str(exc.detail), status_code=exc.status_code, headers=headers, media_type="text/plain")


@app.exception_handler(Exception)
async def internal_error(request: Request, exc: Exception) -> Response:
    logger.exception("Unhandled request failure", exc_info=exc)
    headers = api_v1.PROTOCOL_HEADERS if request.url.path.startswith("/api/") else {}
    return Response("Internal Server Error", status_code=500, headers=headers, media_type="text/plain")


app.include_router(api_v1.router)
app.include_router(ui.router)


@app.get("/healthz")
def healthz() -> Response:
    return Response("ok", media_type="text/plain")


# Compatibility exports for the existing login-security regression tests.
_LoginThrottle = ui._LoginThrottle
_LOGIN_FREE_ATTEMPTS = ui._LOGIN_FREE_ATTEMPTS
_LOGIN_BASE_LOCK_S = ui._LOGIN_BASE_LOCK_S
_LOGIN_MAX_LOCK_S = ui._LOGIN_MAX_LOCK_S
_LOGIN_RESET_S = ui._LOGIN_RESET_S
_client_key = ui._client_key