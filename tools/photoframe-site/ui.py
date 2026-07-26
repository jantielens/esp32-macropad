"""Human management UI routes over the local photo library."""

from __future__ import annotations

import io
import hashlib
import json
import os
import secrets
import threading
import time
import zlib
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Optional

from fastapi import APIRouter, File, Form, Request, UploadFile
from fastapi.responses import HTMLResponse, RedirectResponse, Response
from fastapi.templating import Jinja2Templates
from PIL import Image

import blobstore as bs
import config as cfg
import gray16
import knobs
import store
import transport

router = APIRouter()
templates = Jinja2Templates(directory=Path(__file__).with_name("templates"))
THUMB_MAX = 360

_LOGIN_FREE_ATTEMPTS = 5
_LOGIN_BASE_LOCK_S = 5.0
_LOGIN_MAX_LOCK_S = 15 * 60.0
_LOGIN_RESET_S = 15 * 60.0


class _LoginThrottle:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._state: dict[str, list[float]] = {}

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
                if now - value[2] <= _LOGIN_RESET_S
            }
            count = self._state.get(key, [0.0, 0.0, now])[0] + 1
            over = int(count) - _LOGIN_FREE_ATTEMPTS
            lock_seconds = min(_LOGIN_MAX_LOCK_S, _LOGIN_BASE_LOCK_S * (2 ** (over - 1))) if over > 0 else 0
            self._state[key] = [count, now + lock_seconds, now]

    def record_success(self, key: str) -> None:
        with self._lock:
            self._state.pop(key, None)


_login_throttle = _LoginThrottle()


def _client_key(request: Request) -> str:
    forwarded = request.headers.get("x-forwarded-for", "")
    if forwarded:
        return forwarded.split(",", 1)[0].strip()
    return request.client.host if request.client else "unknown"


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
        settings = store.read_settings()
        devices.append({
            "id": frame_id,
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
        })
    return templates.TemplateResponse(request, "devices.html", {"user": user, "devices": devices})


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
    key = _client_key(request)
    if _login_throttle.retry_after(key):
        return RedirectResponse("/login?error=Too+many+attempts.+Please+wait+and+try+again.", status_code=303)
    user = request.app.state.config.user(email.strip())
    if user is None or not cfg.verify_password(password, user.password_hash):
        _login_throttle.record_failure(key)
        return RedirectResponse("/login?error=Invalid+credentials", status_code=303)
    _login_throttle.record_success(key)
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
    for meta in request.app.state.index.all():
        if not meta.get("permanent", False) and meta.get("served_at"):
            continue
        fresh = store.is_fresh(meta, now=now, window_days=frame.fresh_window_days)
        items.append({
            **meta,
            "in_rotation": bool(meta.get("permanent")) and not store.is_expired(meta, at=now),
            "is_featured": bool(meta.get("expires_at")) or fresh,
            "fresh": fresh,
            "fresh_in": "",
            "expires_in": "",
            "expired": store.is_expired(meta, at=now),
            "exposure": "",
            "knobs_adjusted": False,
            "knobs_summary": "",
        })
    items.sort(key=lambda item: item.get("uploaded_at", ""), reverse=True)
    by_id = {item["id"]: item for item in items}
    queue = [{"id": item, "caption": by_id[item].get("caption", "")} for item in store.read_queue() if item in by_id]
    return templates.TemplateResponse(request, "gallery.html", {
        "user": user, "device_id": device_id, "items": items, "queue": queue,
        "displays_per_day": store.estimate_displays_per_day([item.get("last_shown_at") for item in items]),
    })


@router.get("/thumb")
def thumb(request: Request, device_id: str, image_id: str) -> Response:
    user = _require_user(request)
    if isinstance(user, Response) or _frame_for_user(request, user, device_id) is None:
        return user if isinstance(user, Response) else Response("Forbidden", status_code=403)
    data = bs.download_blob(store.PHOTO_CONTAINER, f"{image_id}/thumb.png")
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
            source.load()
            source_image = source.copy()
            variants = []
            preview = None
            for width, height, code, profile_key in sorted(request.app.state.config.variant_requirements()):
                representative = next(
                    item for item in request.app.state.config.frames.values()
                    if item.width == width and item.height == height
                    and code in item.format_codes and item.variant_key(code) == profile_key
                )
                payload = transport.encode_variant(
                    source_image, width=width, height=height, format_code=code,
                    transform=representative.image_transform, crop=crop,
                    knobs=knob_values_clean, resampler=resampler,
                    jpeg_quality=representative.jpeg_quality,
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
    bs.upload_blob(store.PHOTO_CONTAINER, f"{image_id}/source{source_suffix}", raw)
    encoded_variants = []
    for width, height, code, profile_key, extension, payload in variants:
        blob_name = f"{image_id}/transport-{width}x{height}-{code}-{profile_key}.{extension}"
        bs.upload_blob(store.PHOTO_CONTAINER, blob_name, payload, store.MEDIA_TYPES[code])
        encoded_variants.append({
            "width": width, "height": height, "format_code": code,
            "profile_key": profile_key,
            "blob_name": blob_name, "content_length": len(payload),
            "content_crc32": f"{zlib.crc32(payload) & 0xffffffff:08x}",
        })
    thumb = _make_thumbnail(preview)
    bs.upload_blob(store.PHOTO_CONTAINER, f"{image_id}/thumb.png", thumb, "image/png")
    expires_at = None
    try:
        if ttl_hours and float(ttl_hours) > 0:
            expires_at = (datetime.now(timezone.utc) + timedelta(hours=float(ttl_hours))).isoformat()
    except ValueError:
        pass
    request.app.state.index.put({
        "id": image_id, "source_name": f"source{source_suffix}", "caption": caption.strip(),
        "permanent": bool(permanent), "expires_at": expires_at, "last_shown_at": None,
        "served_at": None, "uploaded_at": store.now_iso(), "uploader": user.email,
        "knobs": knob_values_clean, "crop": crop, "resampler": resampler,
        "variants": encoded_variants,
    })
    store.queue_unshift(image_id)
    return RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@router.post("/photos/delete")
def photos_delete(request: Request, device_id: str = Form(...), image_id: str = Form(...)) -> Response:
    user = _require_user(request)
    if isinstance(user, Response):
        return user
    if _frame_for_user(request, user, device_id) and store.is_valid_id(image_id):
        request.app.state.index.delete(image_id)
    return RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@router.post("/photos/show-next")
def photos_show_next(request: Request, device_id: str = Form(...), image_id: str = Form(...)) -> Response:
    user = _require_user(request)
    if not isinstance(user, Response) and _frame_for_user(request, user, device_id) and store.is_valid_id(image_id):
        store.queue_unshift(image_id)
    return user if isinstance(user, Response) else RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@router.post("/photos/queue-remove")
def queue_remove(request: Request, device_id: str = Form(...), image_id: str = Form(...)) -> Response:
    user = _require_user(request)
    if not isinstance(user, Response) and _frame_for_user(request, user, device_id) and store.is_valid_id(image_id):
        store.queue_remove(image_id)
    return user if isinstance(user, Response) else RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@router.post("/photos/queue-clear")
def queue_clear(request: Request, device_id: str = Form(...)) -> Response:
    user = _require_user(request)
    if not isinstance(user, Response) and _frame_for_user(request, user, device_id):
        store.write_queue([])
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
    status_code: int = 200,
) -> Response:
    settings = store.read_settings()
    token_nonce = secrets.token_urlsafe(32)
    request.session["token_nonce"] = token_nonce
    token_digest = hashlib.sha256(frame.token.encode("utf-8")).hexdigest()
    context = {"user": user, "device_id": frame.frame_id,
               "min_temp_min_spacing": cfg.MIN_TEMP_MIN_SPACING,
               "min_fresh_window_days": cfg.MIN_FRESH_WINDOW_DAYS,
               "min_max_temp_share_pct": cfg.MIN_MAX_TEMP_SHARE_PCT,
               "max_max_temp_share_pct": cfg.MAX_MAX_TEMP_SHARE_PCT,
               "token_fingerprint": f"{token_digest[:8]}...{token_digest[-4:]}",
               "token_nonce": token_nonce,
               "frame_token": frame_token,
               "token_error": token_error}
    for key in ("temp_min_spacing", "fresh_window_days", "max_temp_share_pct"):
        context[key] = int(settings.get(key, getattr(frame, key)))
        context[f"{key}_default"] = getattr(frame, key)
        context[f"{key}_overridden"] = key in settings
    response = templates.TemplateResponse(request, "settings.html", context, status_code=status_code)
    if frame_token:
        response.headers["Cache-Control"] = "no-store"
        response.headers["Pragma"] = "no-cache"
    return response


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
    peer = request.client.host if request.client else "unknown"
    throttle_key = f"token:{user.email}:{peer}"
    if _login_throttle.retry_after(throttle_key):
        return _render_settings(
            request, user, frame, token_error="Too many attempts. Please wait and try again.", status_code=429
        )
    if not cfg.verify_password(password, user.password_hash):
        _login_throttle.record_failure(throttle_key)
        return _render_settings(request, user, frame, token_error="Invalid password.", status_code=403)
    _login_throttle.record_success(throttle_key)
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
    settings = {} if reset else store.read_settings()
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
    store.write_settings(settings)
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