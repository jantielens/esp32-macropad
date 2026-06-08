"""Photoframe site: FastAPI app serving both the human web UI and the device API.

Single pure-Python runtime (PRD Sections 5-6). Human users authenticate against
``CONFIG_JSON`` accounts via a signed session cookie; devices authenticate per request
with a pull key. Images are converted to calibrated G16P at upload time and served
verbatim from a single-copy, blob-authoritative store.
"""

from __future__ import annotations

import io
import json
import logging
import os
import secrets
import threading
import time
from datetime import datetime, timedelta, timezone
from typing import Optional

from fastapi import FastAPI, File, Form, Request, UploadFile
from fastapi.responses import HTMLResponse, RedirectResponse, Response
from fastapi.templating import Jinja2Templates
from PIL import Image
from starlette.middleware.sessions import SessionMiddleware

import blobstore as bs
import config as cfg
import gray16
import knobs
import store

TEMPLATES_DIR = os.path.join(os.path.dirname(__file__), "templates")
THUMB_MAX = 360  # gallery thumbnail longest edge

logger = logging.getLogger("photoframe")
if not logging.getLogger().handlers:
    # Stand-alone / local runs have no root handler; make our INFO logs visible.
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
logger.setLevel(logging.INFO)

templates = Jinja2Templates(directory=TEMPLATES_DIR)

app = FastAPI(title="E1003 Photoframe Site")
# Mark the session cookie Secure only when served over HTTPS (Azure App Service,
# behind any TLS proxy). Leave it off for LAN/HTTP so login works there too.
# Set COOKIE_SECURE=1 (or true/yes) in production app settings.
_cookie_secure = os.environ.get("COOKIE_SECURE", "").strip().lower() in ("1", "true", "yes")

# SECRET_KEY signs the session cookie. In production (COOKIE_SECURE set, i.e.
# served over HTTPS) it MUST be provided and stable: a per-process random key
# would silently invalidate every session on each restart and break multi-worker
# setups, so fail fast instead of degrading. For local HTTP dev, fall back to an
# ephemeral key for convenience.
_secret_key = os.environ.get("SECRET_KEY")
if not _secret_key:
    if _cookie_secure:
        raise RuntimeError(
            "SECRET_KEY is required in production (COOKIE_SECURE is set). "
            "Set a stable, random SECRET_KEY app setting (e.g. `python3 -c "
            "'import secrets; print(secrets.token_hex(32))'`)."
        )
    _secret_key = secrets.token_hex(32)
    logger.warning(
        "SECRET_KEY not set; using an ephemeral dev key (sessions reset on restart)."
    )

app.add_middleware(
    SessionMiddleware,
    secret_key=_secret_key,
    https_only=_cookie_secure,
    same_site="lax",
)


@app.middleware("http")
async def _security_headers(request: Request, call_next):
    """Attach defense-in-depth response headers to every response.

    The UI relies on inline <script>/<style> and inline event handlers plus the
    jsdelivr CDN, so the CSP permits 'unsafe-inline' and that origin; it still
    restricts other external sources and (via frame-ancestors) blocks framing.
    HSTS is only sent over HTTPS (COOKIE_SECURE) to avoid pinning LAN/HTTP dev.
    """
    response = await call_next(request)
    response.headers.setdefault("X-Content-Type-Options", "nosniff")
    response.headers.setdefault("X-Frame-Options", "DENY")
    response.headers.setdefault("Referrer-Policy", "no-referrer")
    response.headers.setdefault(
        "Content-Security-Policy",
        "default-src 'self'; "
        "img-src 'self' data: blob:; "
        "style-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net; "
        "script-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net; "
        "connect-src 'self'; "
        "frame-ancestors 'none'; "
        "base-uri 'self'; "
        "form-action 'self'",
    )
    if _cookie_secure:
        response.headers.setdefault(
            "Strict-Transport-Security", "max-age=31536000; includeSubDomains"
        )
    return response


# --- Login throttle (in-process brute-force guard) ----------------------------
# A small per-client-IP backoff so online password guessing is slowed without an
# external service. State is in-memory, which fits the single-worker App Service
# deployment (one process): it resets on restart and is best-effort, not a
# distributed limiter. Failures past a free allowance trigger an exponential
# lockout; a successful login clears the client's record.

_LOGIN_FREE_ATTEMPTS = 5       # failures allowed before lockout kicks in
_LOGIN_BASE_LOCK_S = 5.0       # first lockout duration (seconds)
_LOGIN_MAX_LOCK_S = 15 * 60.0  # cap each lockout at 15 minutes
_LOGIN_RESET_S = 15 * 60.0     # forget a client's failures after this idle gap


class _LoginThrottle:
    """In-memory exponential-backoff lockout keyed by client identifier."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        # key -> [fail_count, locked_until_monotonic, last_seen_monotonic]
        self._state: dict[str, list[float]] = {}

    def _prune(self, now: float) -> None:
        stale = [k for k, (_c, _u, seen) in self._state.items()
                 if now - seen > _LOGIN_RESET_S]
        for k in stale:
            self._state.pop(k, None)

    def retry_after(self, key: str) -> int:
        """Seconds the client must wait, or 0 if a login attempt is allowed now."""
        now = time.monotonic()
        with self._lock:
            entry = self._state.get(key)
            if not entry:
                return 0
            _count, locked_until, _seen = entry
            remaining = locked_until - now
            return int(remaining) + 1 if remaining > 0 else 0

    def record_failure(self, key: str) -> None:
        now = time.monotonic()
        with self._lock:
            self._prune(now)
            count, _until, _seen = self._state.get(key, [0.0, 0.0, now])
            count += 1
            over = int(count) - _LOGIN_FREE_ATTEMPTS
            if over > 0:
                lock = min(_LOGIN_MAX_LOCK_S, _LOGIN_BASE_LOCK_S * (2 ** (over - 1)))
                locked_until = now + lock
            else:
                locked_until = 0.0
            self._state[key] = [count, locked_until, now]

    def record_success(self, key: str) -> None:
        with self._lock:
            self._state.pop(key, None)


_login_throttle = _LoginThrottle()


def _client_key(request: Request) -> str:
    """Best-effort client identifier for throttling (first X-Forwarded-For hop)."""
    # Behind Azure App Service the real client IP is the first entry in
    # X-Forwarded-For; request.client is the platform proxy.
    xff = request.headers.get("x-forwarded-for", "")
    if xff:
        return xff.split(",")[0].strip()
    client = request.client
    return client.host if client else "unknown"


# --- Auth helpers -------------------------------------------------------------


def _current_user(request: Request, config: cfg.Config) -> Optional[cfg.User]:
    email = request.session.get("user")
    if not email:
        return None
    return config.user(str(email))


def _require_user(request: Request, config: cfg.Config) -> cfg.User:
    user = _current_user(request, config)
    if user is None:
        raise _Redirect("/login")
    return user


def _require_device(user: cfg.User, config: cfg.Config, device_id: str) -> cfg.Device:
    device = config.device(device_id)
    if device is None or not cfg.user_can_access(user, device_id):
        raise _Forbidden()
    return device


class _Redirect(Exception):
    def __init__(self, location: str) -> None:
        self.location = location


class _Forbidden(Exception):
    pass


@app.exception_handler(_Redirect)
async def _handle_redirect(_request: Request, exc: _Redirect) -> Response:
    return RedirectResponse(exc.location, status_code=303)


@app.exception_handler(_Forbidden)
async def _handle_forbidden(_request: Request, _exc: _Forbidden) -> Response:
    return Response("Forbidden", status_code=403)


# --- Human UI: auth -----------------------------------------------------------


@app.get("/", response_class=HTMLResponse)
def index(request: Request) -> Response:
    config = cfg.load_config()
    user = _current_user(request, config)
    if user is None:
        return RedirectResponse("/login", status_code=303)
    devices = []
    for device_id in user.devices:
        device = config.device(device_id)
        if device is None:
            devices.append({"id": device_id, "missing": True})
            continue
        params = _resolve_selection_params(device)
        devices.append({
            "id": device_id,
            "format": device.image_format,
            "format_label": "JPEG" if device.image_format == cfg.FORMAT_JPEG else "G16Z",
            "resolution": f"{device.width}\u00d7{device.height}",
            "jpeg_quality": device.jpeg_quality if device.image_format == cfg.FORMAT_JPEG else None,
            "temp_min_spacing": params["temp_min_spacing"],
            "temp_min_spacing_overridden": params["temp_min_spacing_overridden"],
            "fresh_window_days": params["fresh_window_days"],
            "fresh_window_days_overridden": params["fresh_window_days_overridden"],
            "max_temp_share_pct": params["max_temp_share_pct"],
            "max_temp_share_pct_overridden": params["max_temp_share_pct_overridden"],
        })
    return templates.TemplateResponse(
        request,
        "devices.html",
        {"request": request, "user": user, "devices": devices},
    )


@app.get("/login", response_class=HTMLResponse)
def login_form(request: Request, error: Optional[str] = None) -> Response:
    return templates.TemplateResponse(
        request, "login.html", {"request": request, "error": error}
    )


@app.post("/login")
def login_submit(
    request: Request,
    email: str = Form(...),
    password: str = Form(...),
) -> Response:
    key = _client_key(request)
    if _login_throttle.retry_after(key) > 0:
        # Locked out after repeated failures; reject without touching the config
        # or running the (deliberately slow) PBKDF2 verification.
        return RedirectResponse(
            "/login?error=Too+many+attempts.+Please+wait+and+try+again.",
            status_code=303,
        )
    config = cfg.load_config()
    user = config.user(email.strip().lower())
    if user is None or not cfg.verify_password(password, user.password_hash):
        _login_throttle.record_failure(key)
        return RedirectResponse("/login?error=Invalid+credentials", status_code=303)
    _login_throttle.record_success(key)
    request.session["user"] = user.email
    return RedirectResponse("/", status_code=303)


@app.post("/logout")
def logout(request: Request) -> Response:
    request.session.clear()
    return RedirectResponse("/login", status_code=303)


# --- Human UI: gallery + upload ----------------------------------------------


@app.get("/photos", response_class=HTMLResponse)
def gallery(request: Request, device_id: str) -> Response:
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)

    items = []
    params = _resolve_selection_params(device)
    fresh_window_days = params["fresh_window_days"]
    n = params["temp_min_spacing"]
    floor = store.share_pct_to_floor(params["max_temp_share_pct"])
    now = datetime.now(timezone.utc)
    knob_defaults = knobs.defaults()
    for image_id in store.list_image_ids(device.container_sas_url):
        meta = store.read_meta(device.container_sas_url, image_id) or {}
        # Hide one-shots that have already been served: their blob lingers only
        # until the next poll reaps it (deferred delete keeps the serve redirect
        # valid), but to the user the image is spent and should be gone.
        if not meta.get("permanent", False) and meta.get("served_at"):
            continue
        permanent = bool(meta.get("permanent", False))
        expires_at = meta.get("expires_at")
        expires_in, expired = _expiry_label(expires_at)
        fresh_in, fresh = _fresh_label(meta, window_days=fresh_window_days)
        # Rotation bucket, mirroring select_next: only un-expired permanents
        # rotate; those with an expiry or still fresh are in the featured bucket.
        in_rotation = permanent and not store.is_expired(meta, at=now)
        is_featured = in_rotation and (bool(expires_at) or fresh)
        # Tone adjustments are kept off the card face (too technical): the full
        # values go in a hover tooltip, and a single "Adjusted" badge shows only
        # when the photo was tweaked away from the pipeline defaults.
        saved_knobs = meta.get("knobs") or {}
        knobs_adjusted = any(
            kid in saved_knobs and abs(float(saved_knobs[kid]) - dv) > 1e-9
            for kid, dv in knob_defaults.items())
        knobs_summary = " · ".join(
            f"{kid}={float(saved_knobs[kid]):.2f}"
            for kid in knob_defaults if kid in saved_knobs)
        items.append({
            "id": image_id,
            "caption": meta.get("caption", ""),
            "permanent": permanent,
            "expires_at": expires_at,
            "expires_in": expires_in,
            "expired": expired,
            "fresh": fresh,
            "fresh_in": fresh_in,
            "last_shown_at": meta.get("last_shown_at"),
            "uploaded_at": meta.get("uploaded_at"),
            "knobs_adjusted": knobs_adjusted,
            "knobs_summary": knobs_summary,
            "in_rotation": in_rotation,
            "is_featured": is_featured,
        })

    items.sort(key=lambda i: i.get("uploaded_at") or "", reverse=True)

    # Per-photo exposure hint: how often each photo is expected to show, given the
    # whole gallery + config. Share is closed-form (expected_share); cadence is
    # inferred from recent last_shown_at history (the device's poll rate).
    perm_count = sum(1 for i in items if i["in_rotation"] and not i["is_featured"])
    featured_count = sum(1 for i in items if i["is_featured"])
    displays_per_day = store.estimate_displays_per_day(
        [i["last_shown_at"] for i in items], now=now)
    for item in items:
        if not item["permanent"]:
            item["exposure"] = "Shown once, then removed"
        elif not item["in_rotation"]:
            item["exposure"] = "Not shown (expired)"
        else:
            share = store.expected_share(
                is_featured=item["is_featured"], perm_count=perm_count,
                featured_count=featured_count, n=n, floor=floor)
            item["exposure"] = store.frequency_label(share, displays_per_day)


    # "What's next" strip: the queue in serve order (front = next), limited to ids
    # that still have a backing image. Captions are reused from the items above.
    by_id = {i["id"]: i for i in items}
    queue = [
        {"id": qid, "caption": by_id[qid]["caption"]}
        for qid in store.read_queue(device.container_sas_url)
        if qid in by_id
    ]

    return templates.TemplateResponse(
        request,
        "gallery.html",
        {"request": request, "user": user, "device_id": device_id,
         "items": items, "queue": queue,
         "displays_per_day": round(displays_per_day, 1)},
    )


@app.get("/settings", response_class=HTMLResponse)
def settings_form(request: Request, device_id: str) -> Response:
    """Per-device config page (rotation cadence and other tunables).

    Kept off the gallery so the gallery stays focused on images. Reachable from
    the Config button on the devices page.
    """
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)

    return templates.TemplateResponse(
        request,
        "settings.html",
        {"request": request, "user": user, "device_id": device_id,
         "min_temp_min_spacing": cfg.MIN_TEMP_MIN_SPACING,
         "min_fresh_window_days": cfg.MIN_FRESH_WINDOW_DAYS,
         "min_max_temp_share_pct": cfg.MIN_MAX_TEMP_SHARE_PCT,
         "max_max_temp_share_pct": cfg.MAX_MAX_TEMP_SHARE_PCT,
         **_resolve_selection_params(device)},
    )


@app.get("/thumb")
def thumb(request: Request, device_id: str, image_id: str) -> Response:
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)
    if not store.is_valid_id(image_id):
        return Response("Bad id", status_code=400)
    data = bs.download_blob(device.container_sas_url, store.thumb_name(image_id))
    if data is None:
        return Response("Not found", status_code=404)
    return Response(data, media_type="image/png", headers={"Cache-Control": "private, max-age=300"})


@app.get("/upload", response_class=HTMLResponse)
def upload_form(request: Request, device_id: str) -> Response:
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)
    return templates.TemplateResponse(
        request,
        "upload.html",
        _upload_context(request, user, device),
    )


@app.post("/preview-base")
def preview_base(
    request: Request,
    device_id: str = Form(...),
    file: UploadFile = File(...),
) -> Response:
    """Return the small grayscale 'base' PNG for the live upload preview.

    The WHOLE oriented image at identity tone with auto-stretch (no crop, no tone
    curve, no dither); the browser pans/zooms a device-aspect window over it and
    applies the tone knobs on top. Fetched once per image. Advisory, not
    authoritative.
    """
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)

    raw = file.file.read()
    try:
        with Image.open(io.BytesIO(raw)) as src:
            src.load()
            base = gray16.full_base(
                src,
                transform=device.image_transform,
            )
    except Exception as exc:  # noqa: BLE001 - surface decode failure to the client
        return Response(f"Could not process image: {exc}", status_code=400)

    buffer = io.BytesIO()
    base.save(buffer, format="PNG")
    return Response(
        buffer.getvalue(),
        media_type="image/png",
        headers={"Cache-Control": "no-store"},
    )


@app.post("/upload")
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
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)

    # Knob values arrive as a single JSON object so adding a knob needs no route
    # change. Unknown/invalid values fall back to registry defaults.
    try:
        knob_raw = json.loads(knob_values) if knob_values else {}
        if not isinstance(knob_raw, dict):
            knob_raw = {}
    except ValueError:
        knob_raw = {}
    knob_vals = knobs.parse_values(knob_raw)

    # Resampler is a categorical (non-knob) server-side choice; normalize unknown
    # values to the default so meta always records the filter actually used.
    resampler = resampler.strip().lower()
    if resampler not in gray16.RESAMPLERS:
        resampler = gray16.DEFAULT_RESAMPLER

    crop = _parse_json_object(crop_values)
    raw = file.file.read()
    is_jpeg = device.image_format == cfg.FORMAT_JPEG
    try:
        with Image.open(io.BytesIO(raw)) as src:
            src.load()
            if is_jpeg:
                # Resize-only profile: reuse the panel-agnostic tone curve, but no
                # E1003 calibration or dither -- the device library dithers on-panel.
                logger.info(
                    "upload device=%s src=%dx%d transform=%s crop=%s format=jpeg "
                    "quality=%d gamma=%.3f highlights=%.3f brightness=%.3f contrast=%.3f "
                    "midtone=%.3f resampler=%s",
                    device_id, src.width, src.height, device.image_transform,
                    gray16.describe_crop(crop, src.width, src.height),
                    device.jpeg_quality, knob_vals["gamma"], knob_vals["highlights"],
                    knob_vals["brightness"], knob_vals["contrast"], knob_vals["midtone"],
                    resampler,
                )
                image_bytes, preview = gray16.encode_jpeg(
                    src,
                    width=device.width,
                    height=device.height,
                    transform=device.image_transform,
                    crop=crop,
                    resampler=resampler,
                    quality=device.jpeg_quality,
                    gamma=knob_vals["gamma"],
                    highlights=knob_vals["highlights"],
                    brightness=knob_vals["brightness"],
                    contrast=knob_vals["contrast"],
                    midtone=knob_vals["midtone"],
                )
            else:
                logger.info(
                    "upload device=%s src=%dx%d transform=%s crop=%s gamma=%.3f highlights=%.3f "
                    "brightness=%.3f contrast=%.3f midtone=%.3f calibration=%.0f resampler=%s",
                    device_id, src.width, src.height, device.image_transform,
                    gray16.describe_crop(crop, src.width, src.height),
                    knob_vals["gamma"], knob_vals["highlights"],
                    knob_vals["brightness"], knob_vals["contrast"], knob_vals["midtone"],
                    knob_vals["panel_calibration"], resampler,
                )
                g16p_bytes, preview = gray16.encode_g16p(
                    src,
                    width=device.width,
                    height=device.height,
                    transform=device.image_transform,
                    crop=crop,
                    gamma=knob_vals["gamma"],
                    highlights=knob_vals["highlights"],
                    brightness=knob_vals["brightness"],
                    contrast=knob_vals["contrast"],
                    midtone=knob_vals["midtone"],
                    panel_calibration=knob_vals["panel_calibration"],
                    resampler=resampler,
                )
                image_bytes = gray16.wrap_g16z(g16p_bytes)
    except Exception as exc:  # noqa: BLE001 - surface decode/encode failure to user
        return templates.TemplateResponse(
            request,
            "upload.html",
            _upload_context(request, user, device, error=f"Could not process image: {exc}"),
            status_code=400,
        )

    thumb_png = _make_thumbnail(preview, simulate=not is_jpeg)

    is_permanent = bool(permanent)
    # permanent and expiry are independent axes: a permanent image stays in
    # rotation until its (optional) expiry, then is auto-removed.
    expires_at = None
    if ttl_hours:
        try:
            hours = float(ttl_hours)
            if hours > 0:
                expires_at = (datetime.now(timezone.utc) + timedelta(hours=hours)).isoformat()
        except ValueError:
            expires_at = None

    image_id = _mint_id()
    meta = {
        "id": image_id,
        "format": device.image_format,
        "permanent": is_permanent,
        "expires_at": expires_at,
        "last_shown_at": None,
        "caption": caption.strip(),
        "uploaded_at": store.now_iso(),
        "uploader": user.email,
        # JPEG reuses the tone knobs but not the panel-calibration toggle.
        "knobs": {k: v for k, v in knob_vals.items() if k != "panel_calibration"} if is_jpeg else knob_vals,
        "crop": crop or {},
        "resampler": resampler,
    }
    store.store_image(device.container_sas_url, image_id, image_bytes, thumb_png, meta)
    return RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@app.post("/photos/delete")
def photos_delete(
    request: Request,
    device_id: str = Form(...),
    image_id: str = Form(...),
) -> Response:
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)
    if store.is_valid_id(image_id):
        store.delete_image(device.container_sas_url, image_id)
    return RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@app.post("/photos/show-next")
def photos_show_next(
    request: Request,
    device_id: str = Form(...),
    image_id: str = Form(...),
) -> Response:
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)
    if store.is_valid_id(image_id):
        store.queue_unshift(device.container_sas_url, image_id)
    return RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@app.post("/photos/queue-remove")
def photos_queue_remove(
    request: Request,
    device_id: str = Form(...),
    image_id: str = Form(...),
) -> Response:
    """Drop one image from the 'what's next' queue (the image itself is kept)."""
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)
    if store.is_valid_id(image_id):
        store.queue_remove(device.container_sas_url, image_id)
    return RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@app.post("/photos/queue-clear")
def photos_queue_clear(
    request: Request,
    device_id: str = Form(...),
) -> Response:
    """Empty the 'what's next' queue (images are kept; rotation still applies)."""
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)
    store.write_queue(device.container_sas_url, [])
    return RedirectResponse(f"/photos?device_id={device_id}", status_code=303)


@app.post("/settings")
def photos_settings(
    request: Request,
    device_id: str = Form(...),
    temp_min_spacing: str = Form(""),
    fresh_window_days: str = Form(""),
    max_temp_share_pct: str = Form(""),
    reset: str = Form(""),
) -> Response:
    """Save (or reset) the per-device selection overrides.

    Persists to disposable blob soft-state (store.write_settings) so owners can
    tune the rotation from the device config page without editing CONFIG_JSON.
    ``reset`` clears every override, falling back to the device's configured
    defaults. Blank or invalid fields are ignored (the current value is kept).
    """
    config = cfg.load_config()
    user = _require_user(request, config)
    device = _require_device(user, config, device_id)
    sas = device.container_sas_url
    settings = store.read_settings(sas)
    if reset:
        for key in ("temp_min_spacing", "fresh_window_days", "max_temp_share_pct"):
            settings.pop(key, None)
    else:
        _apply_int_setting(settings, "temp_min_spacing", temp_min_spacing,
                           minimum=cfg.MIN_TEMP_MIN_SPACING)
        _apply_int_setting(settings, "fresh_window_days", fresh_window_days,
                           minimum=cfg.MIN_FRESH_WINDOW_DAYS)
        _apply_int_setting(settings, "max_temp_share_pct", max_temp_share_pct,
                           minimum=cfg.MIN_MAX_TEMP_SHARE_PCT,
                           maximum=cfg.MAX_MAX_TEMP_SHARE_PCT)
    store.write_settings(sas, settings)
    return RedirectResponse(f"/settings?device_id={device_id}", status_code=303)


# --- Device API ---------------------------------------------------------------


@app.get("/api/next")
def api_next(device_id: str, key: str, proxy: int = 0) -> Response:
    config = cfg.load_config()
    device = config.device(device_id)
    if device is None or not cfg.verify_device_key(key, device.api_key):
        return Response("Unauthorized", status_code=401)

    sas = device.container_sas_url
    params = _resolve_selection_params(device)
    image_id = store.select_next(
        sas,
        temp_min_spacing=params["temp_min_spacing"],
        fresh_window_days=params["fresh_window_days"],
        max_temp_share_pct=params["max_temp_share_pct"],
    )
    if image_id is None:
        return Response(status_code=204)

    meta = store.read_meta(sas, image_id)
    if meta is None:
        # Raced with a delete; tell the device to retry rather than serving garbage.
        store.queue_remove(sas, image_id)
        return Response(status_code=204)

    store.mark_served(sas, image_id, meta)

    image_format = meta.get("format", "g16z")
    blob_name = store.image_name(image_id, store.meta_image_ext(meta))
    blob_url = bs.build_blob_url(sas, blob_name)

    # Delivery is controlled per device by `serve_mode` (config knob), not the
    # image format. `redirect` (default) 302s the device straight to the blob's
    # SAS URL so it pulls the payload from Azure Blob Storage instead of routing
    # it through this single-core app (which would copy bytes Blob -> app ->
    # device, doubling transfer time). Use `inline` for clients that cannot
    # follow HTTP redirects -- e.g. the InkplateLibrary image loader
    # (downloadFile/downloadFileHTTPS) defaults to HTTPC_DISABLE_FOLLOW_REDIRECTS,
    # so a 302 would be decoded as the (non-image) redirect body and fail.
    # `?proxy=1` forces the inline path for debugging regardless of serve_mode.
    redirect_ok = device.serve_mode == cfg.SERVE_REDIRECT

    # The container SAS the device receives is scoped to its own container, so
    # redirecting exposes no other device's data.
    if not proxy and redirect_ok:
        return RedirectResponse(blob_url, status_code=302)

    data = bs.download_blob(sas, blob_name)
    if data is None:
        return Response(status_code=204)
    if image_format == "jpeg":
        media_type, header_format = "image/jpeg", "jpeg"
    else:
        media_type, header_format = "application/octet-stream", "g16p"
    return Response(
        data,
        media_type=media_type,
        headers={"X-Image-Format": header_format, "Cache-Control": "no-store"},
    )


@app.get("/healthz")
def healthz() -> Response:
    return Response("ok", media_type="text/plain")


# --- Internal helpers ---------------------------------------------------------


def _mint_id() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")
    return f"{stamp}-{secrets.token_hex(4)}"


def _resolve_int_override(
    settings: dict, key: str, default: int, *, minimum: int, maximum: Optional[int] = None
) -> tuple[int, bool]:
    """Effective value for one selection knob: clamped UI override, else default.

    Returns (value, overridden). An absent or unparseable override falls back to
    the device's configured default.
    """
    raw = settings.get(key)
    if raw is None:
        return default, False
    try:
        value = int(raw)
    except (TypeError, ValueError):
        return default, False
    value = max(minimum, value)
    if maximum is not None:
        value = min(maximum, value)
    return value, True


def _resolve_selection_params(device: cfg.Device) -> dict:
    """Effective selection knobs (UI overrides over config defaults) as a context dict.

    Overrides live in disposable blob soft-state (store.read_settings) so an owner
    can tune the rotation from the config page without editing CONFIG_JSON. The
    returned dict carries each knob's effective value, its device default, and an
    ``*_overridden`` flag for the template, plus the values select_next consumes.
    """
    settings = store.read_settings(device.container_sas_url)
    spacing, sp_ov = _resolve_int_override(
        settings, "temp_min_spacing", device.temp_min_spacing,
        minimum=cfg.MIN_TEMP_MIN_SPACING)
    window, fw_ov = _resolve_int_override(
        settings, "fresh_window_days", device.fresh_window_days,
        minimum=cfg.MIN_FRESH_WINDOW_DAYS)
    share, ms_ov = _resolve_int_override(
        settings, "max_temp_share_pct", device.max_temp_share_pct,
        minimum=cfg.MIN_MAX_TEMP_SHARE_PCT, maximum=cfg.MAX_MAX_TEMP_SHARE_PCT)
    return {
        "temp_min_spacing": spacing,
        "temp_min_spacing_default": device.temp_min_spacing,
        "temp_min_spacing_overridden": sp_ov,
        "fresh_window_days": window,
        "fresh_window_days_default": device.fresh_window_days,
        "fresh_window_days_overridden": fw_ov,
        "max_temp_share_pct": share,
        "max_temp_share_pct_default": device.max_temp_share_pct,
        "max_temp_share_pct_overridden": ms_ov,
    }


def _apply_int_setting(
    settings: dict, key: str, raw: str, *, minimum: int, maximum: Optional[int] = None
) -> None:
    """Set settings[key] from a clamped form value; ignore blank/invalid input."""
    try:
        value = int(raw)
    except (TypeError, ValueError):
        return  # blank or non-numeric: keep the current setting
    value = max(minimum, value)
    if maximum is not None:
        value = min(maximum, value)
    settings[key] = value


def _expiry_label(expires_at: Optional[str]) -> tuple[Optional[str], bool]:
    """Human 'time left' string and expired flag for an ISO expiry.

    Returns (label, expired). label is a short 'Nd Nh' / 'Nh Nm' / 'Nm' string
    while time remains, or None when there is no expiry or it has already passed
    (callers render an explicit 'Expired' badge for the latter).
    """
    if not expires_at:
        return None, False
    try:
        dt = datetime.fromisoformat(expires_at)
    except ValueError:
        return None, False
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    secs = int((dt - datetime.now(timezone.utc)).total_seconds())
    if secs <= 0:
        return None, True
    days, rem = divmod(secs, 86400)
    hours, rem = divmod(rem, 3600)
    minutes = rem // 60
    if days:
        return f"{days}d {hours}h", False
    if hours:
        return f"{hours}h {minutes}m", False
    return f"{minutes}m", False


def _fresh_label(meta: dict, *, window_days: int) -> tuple[Optional[str], bool]:
    """Human 'fresh time left' string and fresh flag for a permanent photo.

    A newly uploaded permanent photo is featured for window_days after upload
    (see store.is_fresh) before graduating into normal rotation. Returns
    (label, fresh): label is a short 'Nd Nh' / 'Nh Nm' / 'Nm' string of time left
    in the window, or (None, False) when the photo is not fresh.
    """
    now = datetime.now(timezone.utc)
    if not store.is_fresh(meta, now=now, window_days=window_days):
        return None, False
    uploaded = store._parse_iso(meta.get("uploaded_at"))
    if uploaded is None:  # is_fresh already validated this, but stay defensive
        return None, False
    secs = max(0, int((uploaded + timedelta(days=window_days) - now).total_seconds()))
    days, rem = divmod(secs, 86400)
    hours, rem = divmod(rem, 3600)
    minutes = rem // 60
    if days:
        return f"{days}d {hours}h", True
    if hours:
        return f"{hours}h {minutes}m", True
    return f"{minutes}m", True


def _parse_json_object(raw: str) -> dict:
    """Parse a form-supplied JSON object, returning {} on anything unexpected."""
    if not raw:
        return {}
    try:
        value = json.loads(raw)
    except ValueError:
        return {}
    return value if isinstance(value, dict) else {}


def _make_thumbnail(preview: Image.Image, *, simulate: bool = True) -> bytes:
    # For the calibrated g16z path, lighten to match the on-screen "simulated"
    # preview (and the real panel) rather than the darker device-faithful encode.
    # The resize-only jpeg path applies no panel curve, so the fitted grayscale
    # image is already display-faithful -- thumbnail it as-is.
    img = gray16.simulate_display(preview) if simulate else preview.copy()
    img.thumbnail((THUMB_MAX, THUMB_MAX), Image.Resampling.LANCZOS)
    buffer = io.BytesIO()
    img.save(buffer, format="PNG")
    return buffer.getvalue()


def _upload_context(request: Request, user, device, **extra) -> dict:
    """Template context for upload.html, adapted to the device's output format.

    The resize-only jpeg profile reuses the panel-agnostic tone knobs but hides
    the E1003 panel-calibration toggle and panel response, so its preview shows
    the tone curve without panel simulation (the device does its own dither). The
    resampler choice always applies (it governs the resize filter).
    """
    is_jpeg = device.image_format == cfg.FORMAT_JPEG
    context = {
        "request": request,
        "user": user,
        "device_id": device.device_id,
        "image_format": device.image_format,
        "knobs": knobs.to_client(include_panel_only=not is_jpeg),
        "device_aspect": device.width / device.height,
        "panel_response": [] if is_jpeg else list(gray16.PANEL_RESPONSE_E1003_GC16_V32),
        "resamplers": gray16.resampler_choices(),
        "resampler_default": gray16.DEFAULT_RESAMPLER,
    }
    context.update(extra)
    return context
