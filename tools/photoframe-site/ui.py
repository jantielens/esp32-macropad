"""Human management UI routes over the local photo library."""

from __future__ import annotations

import io
import hashlib
import json
import logging
import os
import ipaddress
import secrets
import shutil
import threading
import time
import zlib
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Optional

from fastapi import APIRouter, Depends, File, Form, HTTPException, Request, UploadFile
from fastapi.responses import HTMLResponse, RedirectResponse, Response
from fastapi.templating import Jinja2Templates
from PIL import Image

import blobstore as bs
import archive as archive_ops
import config as cfg
import gray16
import knobs
import store
import transport

THUMB_MAX = 360
MAX_DECODED_IMAGE_PIXELS = 40_000_000
ONLINE_WINDOW = timedelta(hours=2)
logger = logging.getLogger("epaper-photoframe.ui")

_LOGIN_FREE_ATTEMPTS = 5
_LOGIN_BASE_LOCK_S = 5.0
_LOGIN_MAX_LOCK_S = 15 * 60.0
_LOGIN_RESET_S = 15 * 60.0
_TRUSTED_PROXY_IPS = {
    value.strip() for value in os.environ.get("TRUSTED_PROXY_IPS", "").split(",")
    if value.strip()
}


def _csrf_token(request: Request) -> str:
    token = request.session.get("csrf_token")
    if not isinstance(token, str) or len(token) < 32:
        token = secrets.token_urlsafe(32)
        request.session["csrf_token"] = token
    return token


def _csrf_template_context(request: Request) -> dict[str, str]:
    return {"csrf_token": _csrf_token(request)}


async def _require_csrf(request: Request) -> None:
    if request.method not in ("POST", "PUT", "PATCH", "DELETE"):
        return
    provided = request.headers.get("x-csrf-token")
    if not provided:
        form = await request.form()
        value = form.get("csrf_token")
        provided = str(value) if value is not None else ""
    expected = request.session.get("csrf_token")
    if (not isinstance(expected, str) or not provided
            or not secrets.compare_digest(expected, provided)):
        raise HTTPException(status_code=403, detail="Invalid CSRF token")


router = APIRouter(dependencies=[Depends(_require_csrf)])
templates = Jinja2Templates(
    directory=Path(__file__).with_name("templates"),
    context_processors=[_csrf_template_context],
)


class _LoginThrottle:
    def __init__(self, *, free_attempts: int = _LOGIN_FREE_ATTEMPTS,
                 base_lock_s: float = _LOGIN_BASE_LOCK_S,
                 max_lock_s: float = _LOGIN_MAX_LOCK_S,
                 reset_s: float = _LOGIN_RESET_S) -> None:
        self._lock = threading.Lock()
        self._state: dict[str, list[float]] = {}
        self._free_attempts = free_attempts
        self._base_lock_s = base_lock_s
        self._max_lock_s = max_lock_s
        self._reset_s = reset_s

    def retry_after(self, key: str) -> int:
        now = time.monotonic()
        with self._lock:
            entry = self._state.get(key)
            if not entry:
                return 0
            remaining = entry[1] - now
            return int(remaining) + 1 if remaining > 0 else 0

    def record_failure(self, key: str) -> None:
        now = time.monotonic()
        with self._lock:
            self._state = {
                item: value for item, value in self._state.items()
                if now - value[2] <= self._reset_s
            }
            count = self._state.get(key, [0.0, 0.0, now])[0] + 1
            over = int(count) - self._free_attempts
            lock_seconds = min(
                self._max_lock_s,
                self._base_lock_s * (2 ** (over - 1)),
            ) if over > 0 else 0
            self._state[key] = [count, now + lock_seconds, now]

    def record_success(self, key: str) -> None:
        with self._lock:
            self._state.pop(key, None)


_login_ip_throttle = _LoginThrottle()
_login_account_throttle = _LoginThrottle(free_attempts=10)
_login_global_throttle = _LoginThrottle(
    free_attempts=100,
    base_lock_s=1.0,
    max_lock_s=60.0,
    reset_s=5 * 60.0,
)
_login_throttle = _login_ip_throttle


def _reset_authentication_throttles() -> None:
    global _login_ip_throttle, _login_account_throttle, _login_global_throttle, _login_throttle
    _login_ip_throttle = _LoginThrottle()
    _login_account_throttle = _LoginThrottle(free_attempts=10)
    _login_global_throttle = _LoginThrottle(
        free_attempts=100,
        base_lock_s=1.0,
        max_lock_s=60.0,
        reset_s=5 * 60.0,
    )
    _login_throttle = _login_ip_throttle


def _client_key(request: Request) -> str:
    peer = request.client.host if request.client else "unknown"
    if peer not in _TRUSTED_PROXY_IPS:
        return peer
    forwarded = request.headers.get("x-forwarded-for", "").split(",", 1)[0].strip()
    try:
        return str(ipaddress.ip_address(forwarded)) if forwarded else peer
    except ValueError:
        return peer


def _authentication_keys(request: Request, account: str, purpose: str) -> tuple[str, str, str]:
    normalized = account.strip().lower()
    account_digest = hashlib.sha256(normalized.encode("utf-8")).hexdigest()
    return (
        f"{purpose}:ip:{_client_key(request)}",
        f"{purpose}:account:{account_digest}",
        f"{purpose}:global",
    )


def _authentication_retry_after(request: Request, account: str, purpose: str) -> int:
    keys = _authentication_keys(request, account, purpose)
    return max(
        _login_ip_throttle.retry_after(keys[0]),
        _login_account_throttle.retry_after(keys[1]),
        _login_global_throttle.retry_after(keys[2]),
    )


def _record_authentication_failure(request: Request, account: str, purpose: str) -> None:
    keys = _authentication_keys(request, account, purpose)
    _login_ip_throttle.record_failure(keys[0])
    _login_account_throttle.record_failure(keys[1])
    _login_global_throttle.record_failure(keys[2])


def _record_authentication_success(request: Request, account: str, purpose: str) -> None:
    keys = _authentication_keys(request, account, purpose)
    _login_ip_throttle.record_success(keys[0])
    _login_account_throttle.record_success(keys[1])


def _current_user(request: Request) -> Optional[cfg.User]:
    email = request.session.get("user")
    return request.app.state.config.user(str(email)) if email else None


def _setup_redirect(request: Request) -> Response | None:
    if cfg.setup_pending(request.app.state.data_root) or not cfg.is_configured(request.app.state.config):
        return RedirectResponse("/setup", status_code=303)
    return None


def _require_user(request: Request) -> cfg.User | Response:
    return _current_user(request) or RedirectResponse("/login", status_code=303)


def _frame_for_user(request: Request, user: cfg.User, frame_id: str) -> cfg.Frame | None:
    frame = request.app.state.config.device(frame_id)
    return frame if frame and cfg.user_can_access(user, frame_id) else None


def _is_site_admin(request: Request, user: cfg.User) -> bool:
    users = request.app.state.config.users
    return len(users) == 1 and user.email in users


def _new_control_nonce(request: Request, purpose: str) -> str:
    nonce = secrets.token_urlsafe(32)
    key = f"{purpose}_nonces"
    outstanding = request.session.get(key, [])
    if not isinstance(outstanding, list):
        outstanding = []
    request.session[key] = [*outstanding[-4:], nonce]
    return nonce


def _consume_control_nonce(request: Request, purpose: str, provided: str | None) -> bool:
    key = f"{purpose}_nonces"
    outstanding = request.session.get(key, [])
    if not isinstance(outstanding, list) or not provided:
        return False
    matched = next((value for value in outstanding
                    if secrets.compare_digest(str(value), provided)), None)
    if matched is None:
        return False
    remaining = [value for value in outstanding if value != matched]
    if remaining:
        request.session[key] = remaining
    else:
        request.session.pop(key, None)
    return True


def _format_codes(raw: str) -> tuple[int, ...]:
    try:
        codes = tuple(int(item.strip()) for item in raw.split(",") if item.strip())
    except ValueError as exc:
        raise cfg.ConfigError("Format codes must be comma-separated numbers.") from exc
    if len(set(codes)) != len(codes):
        raise cfg.ConfigError("Format codes must not contain duplicates.")
    return codes


def _reauthenticate(request: Request, user: cfg.User, password: str, purpose: str) -> str | None:
    if _authentication_retry_after(request, user.email, purpose):
        return "Too many attempts. Please wait and try again."
    if not cfg.verify_password(password, user.password_hash):
        _record_authentication_failure(request, user.email, purpose)
        return "Invalid password."
    _record_authentication_success(request, user.email, purpose)
    return None


def _read_archive_upload(file: UploadFile) -> bytes:
    chunks = []
    total = 0
    while True:
        chunk = file.file.read(1024 * 1024)
        if not chunk:
            return b"".join(chunks)
        total += len(chunk)
        if total > archive_ops.MAX_ARCHIVE_BYTES:
            raise archive_ops.ArchiveError("Archive exceeds the size limit.")
        chunks.append(chunk)


def _validate_decoded_image(image: Image.Image) -> None:
    width, height = image.size
    if width <= 0 or height <= 0 or width > 20_000 or height > 20_000:
        raise ValueError("Image dimensions exceed the supported range.")
    if width * height > MAX_DECODED_IMAGE_PIXELS:
        raise ValueError("Image exceeds the decoded pixel limit.")


@router.get("/", response_class=HTMLResponse)
def index(request: Request) -> Response:
    setup_redirect = _setup_redirect(request)
    if setup_redirect:
        return setup_redirect
    user = _current_user(request)
    if user is None:
        return RedirectResponse("/login", status_code=303)
    devices = []
    for frame_id in user.devices:
        frame = request.app.state.config.device(frame_id)
        if frame is None:
            devices.append({"id": frame_id, "missing": True})
            continue
        settings = store.read_settings(frame_id)
        telemetry = store.read_telemetry(frame_id)
        last_request = store._parse_iso(telemetry.get("last_request_at"))
        if last_request is None:
            presence = "never-seen"
        elif datetime.now(timezone.utc) - last_request <= ONLINE_WINDOW:
            presence = "online"
        else:
            presence = "stale"
        current_key = str(telemetry.get("image_key", ""))
        current = request.app.state.index.get(frame_id, current_key) if current_key else None
        devices.append({
            "id": frame_id,
            "name": frame.display_name,
            "format": frame.image_format,
            "format_label": frame.image_format.upper(),
            "resolution": f"{frame.width}\u00d7{frame.height}",
            "jpeg_quality": frame.jpeg_quality if 1 in frame.format_codes else None,
            "temp_min_spacing": int(settings.get("temp_min_spacing", frame.temp_min_spacing)),
            "fresh_window_days": int(settings.get("fresh_window_days", frame.fresh_window_days)),
            "max_temp_share_pct": int(settings.get("max_temp_share_pct", frame.max_temp_share_pct)),
            "temp_min_spacing_overridden": "temp_min_spacing" in settings,
            "fresh_window_days_overridden": "fresh_window_days" in settings,
            "max_temp_share_pct_overridden": "max_temp_share_pct" in settings,
            "token_status": "revoked" if frame.revoked else "active",
            "presence": presence,
            "last_request_at": telemetry.get("last_request_at"),
            "current_image": current,
        })
    return templates.TemplateResponse(request, "devices.html", {
        "user": user, "devices": devices,
        "add_nonce": _new_control_nonce(request, "device_add"),
    })


@router.get("/devices/add", response_class=HTMLResponse)
def device_add_form(request: Request) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    return _render_device_add(request, user)


def _render_device_add(
    request: Request,
    user: cfg.User,
    *,
    create_error: str | None = None,
    import_error: str | None = None,
    values: dict | None = None,
    status_code: int = 200,
) -> Response:
    return templates.TemplateResponse(request, "device_form.html", {
        "user": user, "error": create_error, "import_error": import_error,
        "nonce": _new_control_nonce(request, "device_add"),
        "import_nonce": _new_control_nonce(request, "device_import"),
        "values": values or {"display_name": "E1003", "width": 1872, "height": 1404,
                             "format_codes": "3,2"},
    }, status_code=status_code)


@router.post("/devices/add", response_class=HTMLResponse)
def device_add_submit(
    request: Request,
    display_name: str = Form(...),
    width: int = Form(...),
    height: int = Form(...),
    format_codes: str = Form(...),
    control_nonce: str | None = Form(None),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    if not _consume_control_nonce(request, "device_add", control_nonce):
        return Response("Invalid device add session", status_code=403)
    values = {"display_name": display_name, "width": width, "height": height,
              "format_codes": format_codes}
    try:
        configured, device_id, _token = cfg.add_frame(
            request.app.state.data_root, owner_email=user.email, display_name=display_name,
            width=width, height=height, format_codes=_format_codes(format_codes),
        )
    except cfg.ConfigError as exc:
        return _render_device_add(request, user, create_error=str(exc), values=values,
                                  status_code=400)
    request.app.state.config = configured
    return RedirectResponse(f"/settings?device_id={device_id}", status_code=303)


@router.get("/devices/{device_id}/edit", response_class=HTMLResponse)
def device_edit_form(request: Request, device_id: str) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    return RedirectResponse(f"/settings?device_id={device_id}", status_code=303)


@router.post("/devices/{device_id}/edit", response_class=HTMLResponse)
def device_edit_submit(
    request: Request,
    device_id: str,
    display_name: str = Form(...),
    width: int = Form(...),
    height: int = Form(...),
    format_codes: str = Form(...),
    control_nonce: str | None = Form(None),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    if not _consume_control_nonce(request, "device_edit", control_nonce):
        return Response("Invalid device edit session", status_code=403)
    try:
        codes = _format_codes(format_codes)
        cfg._validate_profile(width, height, codes)
    except cfg.ConfigError as exc:
        return _render_settings(
            request, user, frame, profile_error=str(exc),
            profile_values={"display_name": display_name, "width": width, "height": height,
                            "format_codes": format_codes},
            status_code=400,
        )
    if not _apply_device_edit(request.app, device_id, display_name, width, height, codes):
        return _render_settings(
            request, user, frame,
            profile_error="Could not update the device profile. The previous profile is still active.",
            profile_values={"display_name": display_name, "width": width, "height": height,
                            "format_codes": format_codes},
            status_code=500,
        )
    return RedirectResponse(f"/settings?device_id={device_id}", status_code=303)


@router.get("/login", response_class=HTMLResponse)
def login_form(request: Request, error: Optional[str] = None) -> Response:
    setup_redirect = _setup_redirect(request)
    if setup_redirect:
        return setup_redirect
    return templates.TemplateResponse(request, "login.html", {"error": error})


@router.post("/login")
def login_submit(request: Request, email: str = Form(...), password: str = Form(...)) -> Response:
    setup_redirect = _setup_redirect(request)
    if setup_redirect:
        return setup_redirect
    normalized_email = email.strip().lower()
    if _authentication_retry_after(request, normalized_email, "login"):
        return RedirectResponse("/login?error=Too+many+attempts.+Please+wait+and+try+again.", status_code=303)
    user = request.app.state.config.user(normalized_email)
    if user is None or not cfg.verify_password(password, user.password_hash):
        _record_authentication_failure(request, normalized_email, "login")
        return RedirectResponse("/login?error=Invalid+credentials", status_code=303)
    _record_authentication_success(request, normalized_email, "login")
    request.session["user"] = user.email
    return RedirectResponse("/", status_code=303)


@router.get("/setup", response_class=HTMLResponse)
def setup_form(request: Request) -> Response:
    if cfg.is_configured(request.app.state.config) and not cfg.setup_pending(request.app.state.data_root):
        return Response("Not Found", status_code=404)
    nonce = secrets.token_urlsafe(32)
    request.session["setup_nonce"] = nonce
    response = templates.TemplateResponse(request, "setup.html", {
        "error": None,
        "setup_nonce": nonce,
        "values": {"frame_id": "e1003-living-room", "width": 1872, "height": 1404},
    })
    response.headers["Cache-Control"] = "no-store"
    return response


@router.post("/setup", response_class=HTMLResponse)
def setup_submit(
    request: Request,
    email: str = Form(...),
    password: str = Form(...),
    password_confirm: str = Form(...),
    frame_id: str = Form(...),
    width: int = Form(...),
    height: int = Form(...),
    output_profile: str = Form("gray16"),
    setup_nonce: Optional[str] = Form(None),
) -> Response:
    if cfg.is_configured(request.app.state.config) and not cfg.setup_pending(request.app.state.data_root):
        return Response("Not Found", status_code=404)
    expected_nonce = request.session.get("setup_nonce")
    if not expected_nonce or not setup_nonce or not secrets.compare_digest(str(expected_nonce), setup_nonce):
        return Response("Invalid setup session", status_code=403)
    values = {"email": email, "frame_id": frame_id, "width": width, "height": height,
              "output_profile": output_profile}
    if password != password_confirm:
        return templates.TemplateResponse(
            request, "setup.html", {"error": "Passwords do not match.", "setup_nonce": setup_nonce,
                                     "values": values}, status_code=400
        )
    if output_profile not in ("gray16", "jpeg"):
        return templates.TemplateResponse(
            request, "setup.html", {"error": "Select a supported output format.", "setup_nonce": setup_nonce,
                                     "values": values}, status_code=400
        )
    formats = (cfg.FORMAT_G16Z, cfg.FORMAT_G16P) if output_profile == "gray16" else (cfg.FORMAT_JPEG,)
    try:
        configured, token = cfg.initialize_site(
            request.app.state.data_root,
            email=email,
            password=password,
            frame_id=frame_id,
            width=width,
            height=height,
            format_codes=formats,
        )
    except cfg.ConfigError as exc:
        return templates.TemplateResponse(
            request, "setup.html", {"error": str(exc), "setup_nonce": setup_nonce,
                                     "values": values}, status_code=400
        )
    request.app.state.config = configured
    request.session.pop("setup_nonce", None)
    user = configured.user(email)
    request.session["user"] = user.email if user else email.strip().lower()
    response = templates.TemplateResponse(request, "setup_complete.html", {
        "user": user,
        "frame_id": frame_id.strip().lower(),
        "frame_token": token,
    })
    response.headers["Cache-Control"] = "no-store"
    response.headers["Pragma"] = "no-cache"
    return response


@router.post("/logout")
def logout(request: Request) -> Response:
    request.session.clear()
    return RedirectResponse("/login", status_code=303)


def _time_left_label(ends_at: Optional[datetime], *, now: datetime) -> str:
    if ends_at is None or ends_at <= now:
        return ""
    seconds = int((ends_at - now).total_seconds())
    days, remainder = divmod(seconds, 86400)
    hours, remainder = divmod(remainder, 3600)
    minutes = remainder // 60
    if days:
        return f"{days}d {hours}h"
    if hours:
        return f"{hours}h {minutes}m"
    return f"{minutes}m"


@router.get("/photos", response_class=HTMLResponse)
def gallery(request: Request, device_id: str) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    now = datetime.now(timezone.utc)
    items = []
    for meta in request.app.state.index.all(device_id):
        if not meta.get("permanent", False) and meta.get("served_at"):
            continue
        fresh = store.is_fresh(meta, now=now, window_days=frame.fresh_window_days)
        uploaded_at = store._parse_iso(meta.get("uploaded_at"))
        expires_at = store._parse_iso(meta.get("expires_at"))
        fresh_until = (
            uploaded_at + timedelta(days=frame.fresh_window_days)
            if fresh and uploaded_at is not None else None
        )
        items.append({
            **meta,
            "in_rotation": bool(meta.get("permanent")) and not store.is_expired(meta, at=now),
            "is_featured": bool(meta.get("expires_at")) or fresh,
            "fresh": fresh,
            "fresh_in": _time_left_label(fresh_until, now=now),
            "expires_in": _time_left_label(expires_at, now=now),
            "expired": store.is_expired(meta, at=now),
            "exposure": "",
            "knobs_adjusted": False,
            "knobs_summary": "",
        })
    items.sort(key=lambda item: item.get("uploaded_at", ""), reverse=True)
    by_id = {item["id"]: item for item in items}
    queue = [{"id": item, "caption": by_id[item].get("caption", "")}
             for item in store.read_queue(device_id) if item in by_id]
    return templates.TemplateResponse(request, "gallery.html", {
        "user": user, "device_id": device_id, "items": items, "queue": queue,
        "displays_per_day": store.estimate_displays_per_day([item.get("last_shown_at") for item in items]),
    })


@router.get("/thumb")
def thumb(request: Request, device_id: str, image_id: str) -> Response:
    user = _require_user(request)
    if isinstance(user, Response) or _frame_for_user(request, user, device_id) is None:
        return user if isinstance(user, Response) else Response("Forbidden", status_code=403)
    data = bs.download_blob(store.DEVICE_CONTAINER,
                            f"{store.image_prefix(device_id)}/{image_id}/thumb.png")
    return Response(data, media_type="image/png") if data is not None else Response(status_code=404)


@router.get("/upload", response_class=HTMLResponse)
def upload_form(request: Request, device_id: str) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    return templates.TemplateResponse(request, "upload.html", _upload_context(user, frame))


@router.post("/preview-base")
def preview_base(request: Request, device_id: str = Form(...), file: UploadFile = File(...)) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    try:
        with Image.open(io.BytesIO(file.file.read())) as image:
            _validate_decoded_image(image)
            image.load()
            preview = gray16.full_base(image, transform=frame.image_transform or {})
        output = io.BytesIO()
        preview.save(output, format="PNG")
        return Response(output.getvalue(), media_type="image/png", headers={"Cache-Control": "no-store"})
    except Exception as exc:
        return Response(f"Could not process image: {exc}", status_code=400)


@router.post("/upload")
def upload_submit(
    request: Request,
    device_id: str = Form(...),
    file: UploadFile = File(...),
    permanent: Optional[str] = Form(None),
    ttl_hours: Optional[str] = Form(None),
    caption: str = Form(""),
    knob_values: str = Form(""),
    crop_values: str = Form(""),
    resampler: str = Form(""),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    raw = file.file.read()
    try:
        parsed_knobs = json.loads(knob_values) if knob_values else {}
        parsed_crop = json.loads(crop_values) if crop_values else {}
        knob_values_clean = knobs.parse_values(parsed_knobs if isinstance(parsed_knobs, dict) else {})
        crop = parsed_crop if isinstance(parsed_crop, dict) else {}
        with Image.open(io.BytesIO(raw)) as source:
            _validate_decoded_image(source)
            source.load()
            source_image = source.copy()
            variants = []
            preview = None
            for width, height, code, profile_key in frame.variant_requirements():
                payload = transport.encode_variant(
                    source_image, width=width, height=height, format_code=code,
                    transform=frame.image_transform, crop=crop,
                    knobs=knob_values_clean, resampler=resampler,
                    jpeg_quality=frame.jpeg_quality,
                )
                extension = {1: "jpg", 2: "g16p", 3: "g16z"}[code]
                variants.append((width, height, code, profile_key, extension, payload))
            _, preview = gray16.encode_jpeg(source_image, width=frame.width, height=frame.height,
                                            transform=frame.image_transform, crop=crop,
                                            resampler=resampler, quality=frame.jpeg_quality)
    except Exception as exc:
        return templates.TemplateResponse(request, "upload.html", {
            **_upload_context(user, frame), "error": f"Could not process image: {exc}"
        }, status_code=400)

    image_id = _mint_id()
    source_suffix = Path(file.filename or "source.bin").suffix.lower()
    if not source_suffix or len(source_suffix) > 10:
        source_suffix = ".bin"
    image_folder = f"{store.image_prefix(device_id)}/{image_id}"
    with cfg.data_transaction_lock():
        bs.upload_blob(store.DEVICE_CONTAINER, f"{image_folder}/source{source_suffix}", raw)
        encoded_variants = []
        for width, height, code, profile_key, extension, payload in variants:
            blob_name = f"transport-{width}x{height}-{code}-{profile_key}.{extension}"
            bs.upload_blob(store.DEVICE_CONTAINER, f"{image_folder}/{blob_name}", payload,
                       store.MEDIA_TYPES[code])
            encoded_variants.append({
                "width": width, "height": height, "format_code": code,
                "profile_key": profile_key,
                "blob_name": blob_name, "content_length": len(payload),
                "content_crc32": f"{zlib.crc32(payload) & 0xffffffff:08x}",
            })
        thumb = _make_thumbnail(preview)
        bs.upload_blob(store.DEVICE_CONTAINER, f"{image_folder}/thumb.png", thumb, "image/png")
        expires_at = None
        try:
            if ttl_hours and float(ttl_hours) > 0:
                expires_at = (datetime.now(timezone.utc) + timedelta(hours=float(ttl_hours))).isoformat()
        except ValueError:
            pass
        request.app.state.index.put(device_id, {
            "id": image_id, "source_name": f"source{source_suffix}", "caption": caption.strip(),
            "permanent": bool(permanent), "expires_at": expires_at, "last_shown_at": None,
            "served_at": None, "uploaded_at": store.now_iso(), "uploader": user.email,
            "knobs": knob_values_clean, "crop": crop, "resampler": resampler,
            "variants": encoded_variants,
        })
        store.queue_unshift(device_id, image_id)
    return RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@router.post("/photos/delete")
def photos_delete(request: Request, device_id: str = Form(...), image_id: str = Form(...)) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    if _frame_for_user(request, user, device_id) and store.is_valid_id(image_id):
        request.app.state.index.delete(device_id, image_id)
    return RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@router.post("/photos/show-next")
def photos_show_next(request: Request, device_id: str = Form(...), image_id: str = Form(...)) -> Response:
    user = _require_user(request)
    if not isinstance(user, Response) and _frame_for_user(request, user, device_id) and store.is_valid_id(image_id):
        store.queue_unshift(device_id, image_id)
    return user if isinstance(user, Response) else RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@router.post("/photos/queue-remove")
def queue_remove(request: Request, device_id: str = Form(...), image_id: str = Form(...)) -> Response:
    user = _require_user(request)
    if not isinstance(user, Response) and _frame_for_user(request, user, device_id) and store.is_valid_id(image_id):
        store.queue_remove(device_id, image_id)
    return user if isinstance(user, Response) else RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@router.post("/photos/queue-clear")
def queue_clear(request: Request, device_id: str = Form(...)) -> Response:
    user = _require_user(request)
    if not isinstance(user, Response) and _frame_for_user(request, user, device_id):
        store.write_queue(device_id, [])
    return user if isinstance(user, Response) else RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@router.get("/settings", response_class=HTMLResponse)
def settings_form(request: Request, device_id: str) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    return _render_settings(request, user, frame)


def _render_settings(
    request: Request,
    user: cfg.User,
    frame: cfg.Frame,
    *,
    frame_token: str | None = None,
    token_error: str | None = None,
    profile_error: str | None = None,
    profile_values: dict | None = None,
    export_error: str | None = None,
    remove_error: str | None = None,
    status_code: int = 200,
) -> Response:
    settings = store.read_settings(frame.device_id)
    token_nonce = secrets.token_urlsafe(32)
    request.session["token_nonce"] = token_nonce
    token_digest = hashlib.sha256(frame.token.encode("utf-8")).hexdigest()
    context = {"user": user, "device_id": frame.frame_id,
               "display_name": frame.display_name,
               "min_temp_min_spacing": cfg.MIN_TEMP_MIN_SPACING,
               "min_fresh_window_days": cfg.MIN_FRESH_WINDOW_DAYS,
               "min_max_temp_share_pct": cfg.MIN_MAX_TEMP_SHARE_PCT,
               "max_max_temp_share_pct": cfg.MAX_MAX_TEMP_SHARE_PCT,
               "token_fingerprint": f"{token_digest[:8]}...{token_digest[-4:]}",
               "token_nonce": token_nonce,
               "management_nonce": _new_control_nonce(request, "device_manage"),
               "edit_nonce": _new_control_nonce(request, "device_edit"),
               "export_nonce": _new_control_nonce(request, "device_export"),
               "frame_token": frame_token,
               "token_error": token_error,
               "profile_error": profile_error,
               "export_error": export_error,
               "remove_error": remove_error,
               "profile_values": profile_values or {
                   "display_name": frame.display_name,
                   "width": frame.width,
                   "height": frame.height,
                   "format_codes": ",".join(str(code) for code in frame.format_codes),
               }}
    for key in ("temp_min_spacing", "fresh_window_days", "max_temp_share_pct"):
        context[key] = int(settings.get(key, getattr(frame, key)))
        context[f"{key}_default"] = getattr(frame, key)
        context[f"{key}_overridden"] = key in settings
    response = templates.TemplateResponse(request, "settings.html", context, status_code=status_code)
    if frame_token:
        response.headers["Cache-Control"] = "no-store"
        response.headers["Pragma"] = "no-cache"
    return response


@router.post("/settings/token/rotate", response_class=HTMLResponse)
def settings_token_rotate(
    request: Request,
    device_id: str = Form(...),
    password: str = Form(...),
    management_nonce: str | None = Form(None),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    if not _consume_control_nonce(request, "device_manage", management_nonce):
        return Response("Invalid token rotation session", status_code=403)
    error = _reauthenticate(request, user, password, "token-rotate")
    if error:
        return _render_settings(request, user, frame, token_error=error, status_code=403)
    configured, token = cfg.rotate_frame_token(request.app.state.data_root, device_id)
    request.app.state.config = configured
    return _render_settings(request, configured.user(user.email) or user,
                            configured.device(device_id) or frame, frame_token=token)


@router.post("/devices/remove")
def device_remove(
    request: Request,
    device_id: str = Form(...),
    password: str = Form(...),
    confirm: str = Form(""),
    management_nonce: str | None = Form(None),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    if confirm != device_id:
        return _render_settings(request, user, frame,
                                remove_error="Enter the device ID to confirm removal.", status_code=400)
    if not _consume_control_nonce(request, "device_manage", management_nonce):
        return Response("Invalid device removal session", status_code=403)
    error = _reauthenticate(request, user, password, "device-remove")
    if error:
        return _render_settings(request, user, frame, remove_error=error, status_code=403)
    root = Path(request.app.state.data_root)
    request.app.state.config = cfg.remove_frame(root, device_id, remove_namespace=True)
    request.app.state.index.rebuild()
    return RedirectResponse("/", status_code=303)


@router.post("/devices/export")
def device_export(
    request: Request,
    device_id: str = Form(...),
    password: str = Form(...),
    export_nonce: str | None = Form(None),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    if not _consume_control_nonce(request, "device_export", export_nonce):
        return Response("Invalid device export session", status_code=403)
    error = _reauthenticate(request, user, password, "device-export")
    if error:
        return _render_settings(request, user, frame, export_error=error, status_code=403)
    payload = archive_ops.export_device(request.app.state.data_root, device_id, user.email)
    return Response(payload, media_type="application/zip", headers={
        "Content-Disposition": f'attachment; filename="photoframe-device-{device_id}.zip"',
        "Cache-Control": "no-store",
    })


@router.post("/devices/import", response_class=HTMLResponse)
def device_import(
    request: Request,
    file: UploadFile = File(...),
    password: str = Form(...),
    confirm: str = Form(""),
    expected_device_id: str = Form(""),
    import_nonce: str | None = Form(None),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    if not _consume_control_nonce(request, "device_import", import_nonce):
        return Response("Invalid device import session", status_code=403)
    error = _reauthenticate(request, user, password, "device-import")
    if error:
        return _render_device_add(request, user, import_error=error, status_code=403)
    try:
        payload = _read_archive_upload(file)
        bundle_device_id = archive_ops.inspect_device_archive(payload)
        existing = request.app.state.config.device(bundle_device_id)
        required_confirmation = bundle_device_id if existing is not None else "IMPORT"
        if confirm != required_confirmation:
            if existing is not None:
                message = (f"Device {bundle_device_id} already exists. Enter its exact device ID "
                           "to confirm replacement.")
            else:
                message = "Enter IMPORT to confirm adding this device archive."
            return _render_device_add(request, user, import_error=message, status_code=400)
        configured, device_id = archive_ops.import_device(
            request.app.state.data_root, payload, user.email,
            expected_device_id=expected_device_id or None,
        )
    except archive_ops.ArchiveError as exc:
        return _render_device_add(request, user, import_error=str(exc), status_code=400)
    request.app.state.config = configured
    request.app.state.index.rebuild()
    return RedirectResponse(f"/settings?device_id={device_id}", status_code=303)


@router.get("/site-admin", response_class=HTMLResponse)
def site_admin(request: Request) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    if not _is_site_admin(request, user):
        return Response("Forbidden", status_code=403)
    return _render_site_admin(request, user)


def _render_site_admin(
    request: Request,
    user: cfg.User,
    *,
    export_error: str | None = None,
    import_error: str | None = None,
    status_code: int = 200,
) -> Response:
    return templates.TemplateResponse(request, "site_admin.html", {
        "user": user,
        "site_import_nonce": _new_control_nonce(request, "site_import"),
        "site_export_nonce": _new_control_nonce(request, "site_export"),
        "export_error": export_error,
        "import_error": import_error,
    }, status_code=status_code)


@router.post("/site/export")
def site_export(
    request: Request,
    password: str = Form(...),
    site_export_nonce: str | None = Form(None),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    if not _is_site_admin(request, user):
        return Response("Forbidden", status_code=403)
    if not _consume_control_nonce(request, "site_export", site_export_nonce):
        return Response("Invalid site export session", status_code=403)
    error = _reauthenticate(request, user, password, "site-export")
    if error:
        return _render_site_admin(request, user, export_error=error, status_code=403)
    payload = archive_ops.export_site(request.app.state.data_root)
    return Response(payload, media_type="application/zip", headers={
        "Content-Disposition": 'attachment; filename="photoframe-site.zip"',
        "Cache-Control": "no-store",
    })


@router.post("/site/import", response_class=HTMLResponse)
def site_import(
    request: Request,
    file: UploadFile = File(...),
    password: str = Form(...),
    confirm: str = Form(""),
    site_import_nonce: str | None = Form(None),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    if not _is_site_admin(request, user):
        return Response("Forbidden", status_code=403)
    if not _consume_control_nonce(request, "site_import", site_import_nonce):
        return Response("Invalid site import session", status_code=403)
    if confirm != "REPLACE SITE":
        return _render_site_admin(
            request, user, import_error="Enter REPLACE SITE to confirm full replacement.",
            status_code=400,
        )
    error = _reauthenticate(request, user, password, "site-import")
    if error:
        return _render_site_admin(request, user, import_error=error, status_code=403)
    request.app.state.restart_required = True
    try:
        archive_ops.import_site(request.app.state.data_root, _read_archive_upload(file))
    except archive_ops.ArchiveError as exc:
        request.app.state.restart_required = False
        return _render_site_admin(request, user, import_error=str(exc), status_code=400)
    return HTMLResponse(
        "<h1>Site import complete</h1><p>Restart the photoframe service now. "
        "The imported administrator credentials apply after restart.</p>",
        headers={"Cache-Control": "no-store"},
    )


@router.post("/settings/token/reveal", response_class=HTMLResponse)
def settings_token_reveal(
    request: Request,
    device_id: str = Form(...),
    password: str = Form(...),
    token_nonce: Optional[str] = Form(None),
) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    frame = _frame_for_user(request, user, device_id)
    if frame is None:
        return Response("Forbidden", status_code=403)
    expected_nonce = request.session.get("token_nonce")
    if not expected_nonce or not token_nonce or not secrets.compare_digest(str(expected_nonce), token_nonce):
        return Response("Invalid token reveal session", status_code=403)
    if _authentication_retry_after(request, user.email, "token-reveal"):
        return _render_settings(
            request, user, frame, token_error="Too many attempts. Please wait and try again.", status_code=429
        )
    if not cfg.verify_password(password, user.password_hash):
        _record_authentication_failure(request, user.email, "token-reveal")
        return _render_settings(request, user, frame, token_error="Invalid password.", status_code=403)
    _record_authentication_success(request, user.email, "token-reveal")
    request.session.pop("token_nonce", None)
    return _render_settings(request, user, frame, frame_token=frame.token)


@router.post("/settings")
def settings_submit(request: Request, device_id: str = Form(...), temp_min_spacing: str = Form(""),
                    fresh_window_days: str = Form(""), max_temp_share_pct: str = Form(""),
                    reset: str = Form("")) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    if _frame_for_user(request, user, device_id) is None:
        return Response("Forbidden", status_code=403)
    settings = {} if reset else store.read_settings(device_id)
    if not reset:
        for key, raw, low, high in (
            ("temp_min_spacing", temp_min_spacing, 2, None),
            ("fresh_window_days", fresh_window_days, 0, None),
            ("max_temp_share_pct", max_temp_share_pct, 1, 100),
        ):
            try:
                value = max(low, int(raw))
                settings[key] = min(high, value) if high else value
            except ValueError:
                pass
    store.write_settings(device_id, settings)
    return RedirectResponse(f"/settings?device_id={device_id}", status_code=303)


def _upload_context(user: cfg.User, frame: cfg.Frame) -> dict:
    return {
        "user": user, "device_id": frame.frame_id, "image_format": frame.image_format,
        "knobs": knobs.to_client(include_panel_only=frame.image_format != "jpeg"),
        "device_aspect": frame.width / frame.height,
        "panel_response": [] if frame.image_format == "jpeg" else list(gray16.PANEL_RESPONSE_E1003_GC16_V32),
        "resamplers": gray16.resampler_choices(), "resampler_default": gray16.DEFAULT_RESAMPLER,
    }


def _mint_id() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S") + "-" + secrets.token_hex(4)


def _make_thumbnail(image: Image.Image) -> bytes:
    thumb = image.convert("L")
    thumb.thumbnail((THUMB_MAX, THUMB_MAX), Image.Resampling.LANCZOS)
    output = io.BytesIO()
    thumb.save(output, format="PNG")
    return output.getvalue()


def _apply_device_edit(
    application,
    device_id: str,
    display_name: str,
    width: int,
    height: int,
    format_codes: tuple[int, ...],
) -> bool:
    with cfg.data_transaction_lock():
        return _apply_device_edit_locked(
            application, device_id, display_name, width, height, format_codes
        )


def _apply_device_edit_locked(
    application,
    device_id: str,
    display_name: str,
    width: int,
    height: int,
    format_codes: tuple[int, ...],
) -> bool:
    current = application.state.config.device(device_id)
    if current is None:
        return False
    profile_changed = (current.width, current.height, current.format_codes) != (
        width, height, format_codes
    )
    root = Path(application.state.data_root)
    live = root / "devices" / device_id
    staged = root / "devices" / f".{device_id}.backfill-{secrets.token_hex(4)}"
    backup = root / "devices" / f".{device_id}.backup-{secrets.token_hex(4)}"
    try:
        if profile_changed:
            if live.exists():
                shutil.copytree(live, staged)
            else:
                (staged / "images").mkdir(parents=True)
                (staged / "state").mkdir(parents=True)
            target = cfg.Frame(
                frame_id=device_id, token=current.token, width=width, height=height,
                format_codes=format_codes, revoked=current.revoked,
                image_transform=current.image_transform, jpeg_quality=current.jpeg_quality,
                temp_min_spacing=current.temp_min_spacing,
                fresh_window_days=current.fresh_window_days,
                max_temp_share_pct=current.max_temp_share_pct, display_name=display_name,
            )
            for meta in application.state.index.all(device_id):
                image_dir = staged / "images" / str(meta["id"])
                source_path = image_dir / str(meta["source_name"])
                with Image.open(source_path) as source:
                    _validate_decoded_image(source)
                    source.load()
                    source_image = source.copy()
                variants = []
                for old in image_dir.glob("transport-*"):
                    old.unlink()
                for variant_width, variant_height, code, profile_key in target.variant_requirements():
                    payload = transport.encode_variant(
                        source_image, width=variant_width, height=variant_height,
                        format_code=code, transform=target.image_transform,
                        crop=meta.get("crop") or {}, knobs=meta.get("knobs") or {},
                        resampler=str(meta.get("resampler") or ""),
                        jpeg_quality=target.jpeg_quality,
                    )
                    extension = {1: "jpg", 2: "g16p", 3: "g16z"}[code]
                    blob_name = (f"transport-{variant_width}x{variant_height}-{code}-"
                                 f"{profile_key}.{extension}")
                    (image_dir / blob_name).write_bytes(payload)
                    variants.append({
                        "width": variant_width, "height": variant_height, "format_code": code,
                        "profile_key": profile_key, "blob_name": blob_name,
                        "content_length": len(payload),
                        "content_crc32": f"{zlib.crc32(payload) & 0xffffffff:08x}",
                    })
                updated = dict(meta)
                updated["variants"] = variants
                (image_dir / store.SIDECAR_NAME).write_text(
                    json.dumps(updated, separators=(",", ":"), sort_keys=True), encoding="utf-8"
                )
        namespace = None
        if profile_changed:
            namespace = {
                "operation": "replace",
                "staged": staged.relative_to(root).as_posix(),
                "target": live.relative_to(root).as_posix(),
                "backup": backup.relative_to(root).as_posix(),
            }
        application.state.config = cfg.update_frame(
            root, device_id=device_id, display_name=display_name,
            width=width, height=height, format_codes=format_codes, namespace=namespace,
        )
        application.state.index.rebuild()
        return True
    except Exception:
        logger.exception("Device profile update failed for %s", device_id)
        shutil.rmtree(staged, ignore_errors=True)
        application.state.index.rebuild()
        return False