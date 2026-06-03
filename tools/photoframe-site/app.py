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
app.add_middleware(
    SessionMiddleware,
    secret_key=os.environ.get("SECRET_KEY", secrets.token_hex(32)),
    https_only=_cookie_secure,
    same_site="lax",
)


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
    return templates.TemplateResponse(
        "devices.html",
        {"request": request, "user": user, "devices": list(user.devices)},
    )


@app.get("/login", response_class=HTMLResponse)
def login_form(request: Request, error: Optional[str] = None) -> Response:
    return templates.TemplateResponse(
        "login.html", {"request": request, "error": error}
    )


@app.post("/login")
def login_submit(
    request: Request,
    email: str = Form(...),
    password: str = Form(...),
) -> Response:
    config = cfg.load_config()
    user = config.user(email.strip().lower())
    if user is None or not cfg.verify_password(password, user.password_hash):
        return RedirectResponse("/login?error=Invalid+credentials", status_code=303)
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
    for image_id in store.list_image_ids(device.container_sas_url):
        meta = store.read_meta(device.container_sas_url, image_id) or {}
        # Hide one-shots that have already been served: their blob lingers only
        # until the next poll reaps it (deferred delete keeps the serve redirect
        # valid), but to the user the image is spent and should be gone.
        if not meta.get("permanent", False) and meta.get("served_at"):
            continue
        items.append({
            "id": image_id,
            "caption": meta.get("caption", ""),
            "permanent": bool(meta.get("permanent", False)),
            "expires_at": meta.get("expires_at"),
            "last_shown_at": meta.get("last_shown_at"),
            "uploaded_at": meta.get("uploaded_at"),
            "knobs": meta.get("knobs") or {},
        })
    items.sort(key=lambda i: i.get("uploaded_at") or "", reverse=True)

    # "What's next" strip: the queue in serve order (front = next), limited to ids
    # that still have a backing image. Captions are reused from the items above.
    by_id = {i["id"]: i for i in items}
    queue = [
        {"id": qid, "caption": by_id[qid]["caption"]}
        for qid in store.read_queue(device.container_sas_url)
        if qid in by_id
    ]

    return templates.TemplateResponse(
        "gallery.html",
        {"request": request, "user": user, "device_id": device_id,
         "items": items, "queue": queue},
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
        "upload.html",
        {
            "request": request,
            "user": user,
            "device_id": device_id,
            "knobs": knobs.to_client(),
            "device_aspect": device.width / device.height,
            "display_gamma": gray16.PREVIEW_DISPLAY_GAMMA,
            "resamplers": gray16.resampler_choices(),
            "resampler_default": gray16.DEFAULT_RESAMPLER,
        },
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
    try:
        with Image.open(io.BytesIO(raw)) as src:
            src.load()
            logger.info(
                "upload device=%s src=%dx%d transform=%s crop=%s gamma=%.3f highlights=%.3f resampler=%s",
                device_id, src.width, src.height, device.image_transform,
                gray16.describe_crop(crop, src.width, src.height),
                knob_vals["gamma"], knob_vals["highlights"], resampler,
            )
            g16p_bytes, preview = gray16.encode_g16p(
                src,
                width=device.width,
                height=device.height,
                transform=device.image_transform,
                crop=crop,
                gamma=knob_vals["gamma"],
                highlights=knob_vals["highlights"],
                resampler=resampler,
            )
    except Exception as exc:  # noqa: BLE001 - surface decode/encode failure to user
        return templates.TemplateResponse(
            "upload.html",
            {"request": request, "user": user, "device_id": device_id,
             "knobs": knobs.to_client(),
             "device_aspect": device.width / device.height,
             "display_gamma": gray16.PREVIEW_DISPLAY_GAMMA,
             "resamplers": gray16.resampler_choices(),
             "resampler_default": gray16.DEFAULT_RESAMPLER,
             "error": f"Could not process image: {exc}"},
            status_code=400,
        )

    thumb_png = _make_thumbnail(preview)

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
        "permanent": is_permanent,
        "expires_at": expires_at,
        "last_shown_at": None,
        "caption": caption.strip(),
        "uploaded_at": store.now_iso(),
        "uploader": user.email,
        "knobs": knob_vals,
        "crop": crop or {},
        "resampler": resampler,
    }
    store.store_image(device.container_sas_url, image_id, g16p_bytes, thumb_png, meta)
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


# --- Device API ---------------------------------------------------------------


@app.get("/api/next")
def api_next(device_id: str, key: str, proxy: int = 0) -> Response:
    config = cfg.load_config()
    device = config.device(device_id)
    if device is None or not cfg.verify_device_key(key, device.api_key):
        return Response("Unauthorized", status_code=401)

    sas = device.container_sas_url
    image_id = store.select_next(sas)
    if image_id is None:
        return Response(status_code=204)

    meta = store.read_meta(sas, image_id)
    if meta is None:
        # Raced with a delete; tell the device to retry rather than serving garbage.
        store.queue_remove(sas, image_id)
        return Response(status_code=204)

    store.mark_served(sas, image_id, meta)

    blob_url = bs.build_blob_url(sas, store.g16p_name(image_id))

    # Default path: redirect the device to the blob's own SAS URL so it pulls
    # the ~1.3 MB payload straight from Azure Blob Storage instead of routing it
    # through this single-core app (which would copy the bytes Blob -> app ->
    # device, doubling transfer time). The container SAS the device receives is
    # scoped to its own container, so this exposes no other device's data.
    # `?proxy=1` forces the legacy inline-bytes path for debugging or clients
    # that cannot follow redirects.
    if not proxy:
        return RedirectResponse(blob_url, status_code=302)

    data = bs.download_blob(sas, store.g16p_name(image_id))
    if data is None:
        return Response(status_code=204)
    return Response(
        data,
        media_type="application/octet-stream",
        headers={"X-Image-Format": "g16p", "Cache-Control": "no-store"},
    )


@app.get("/healthz")
def healthz() -> Response:
    return Response("ok", media_type="text/plain")


# --- Internal helpers ---------------------------------------------------------


def _mint_id() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")
    return f"{stamp}-{secrets.token_hex(4)}"


def _parse_json_object(raw: str) -> dict:
    """Parse a form-supplied JSON object, returning {} on anything unexpected."""
    if not raw:
        return {}
    try:
        value = json.loads(raw)
    except ValueError:
        return {}
    return value if isinstance(value, dict) else {}


def _make_thumbnail(preview: Image.Image) -> bytes:
    # Lighten to match the on-screen "simulated" preview (and the real panel),
    # rather than the darker device-faithful encode. Display-only correction.
    img = gray16.simulate_display(preview)
    img.thumbnail((THUMB_MAX, THUMB_MAX), Image.Resampling.LANCZOS)
    buffer = io.BytesIO()
    img.save(buffer, format="PNG")
    return buffer.getvalue()
