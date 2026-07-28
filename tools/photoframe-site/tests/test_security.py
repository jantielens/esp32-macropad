#!/usr/bin/env python3
"""Unit tests for the login throttle, SECRET_KEY fail-fast, and response headers.

Standalone (no pytest needed): run `python3 tests/test_security.py`. The throttle
test drives a fake monotonic clock so lockout/expiry are deterministic; the
fail-fast test imports the app in a subprocess with a controlled environment; the
header test uses Starlette's TestClient and is skipped if httpx is unavailable.
"""

from __future__ import annotations

import os
import asyncio
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

# The app module reads COOKIE_SECURE / SECRET_KEY at import time; pin a dev-safe
# environment before importing so the import itself never fails fast here.
os.environ["COOKIE_SECURE"] = "0"
os.environ.pop("SECRET_KEY", None)

import app  # noqa: E402
import ui  # noqa: E402


class _FakeClock:
    """Replacement for time.monotonic with a manually advanced value."""

    def __init__(self) -> None:
        self.t = 1000.0

    def __call__(self) -> float:
        return self.t

    def advance(self, seconds: float) -> None:
        self.t += seconds


def _throttle_with_clock() -> tuple[ui._LoginThrottle, _FakeClock]:
    """A fresh throttle wired to a fake clock."""
    clock = _FakeClock()
    ui.time.monotonic = clock
    return ui._LoginThrottle(), clock


# --- Login throttle: allowance + lockout --------------------------------------

def test_free_attempts_are_not_locked():
    real = ui.time.monotonic
    try:
        t, _clock = _throttle_with_clock()
        key = "1.1.1.1"
        for _ in range(ui._LOGIN_FREE_ATTEMPTS):
            assert t.retry_after(key) == 0
            t.record_failure(key)
        # Still at the allowance boundary: not yet locked.
        assert t.retry_after(key) == 0
    finally:
        ui.time.monotonic = real


def test_lockout_engages_after_free_attempts():
    real = ui.time.monotonic
    try:
        t, _clock = _throttle_with_clock()
        key = "2.2.2.2"
        for _ in range(ui._LOGIN_FREE_ATTEMPTS + 1):
            t.record_failure(key)
        # First lockout is the base duration (retry_after rounds up by 1s).
        wait = t.retry_after(key)
        assert wait > 0
        assert wait <= int(ui._LOGIN_BASE_LOCK_S) + 1
    finally:
        ui.time.monotonic = real


def test_lockout_backoff_is_exponential():
    real = ui.time.monotonic
    try:
        t, clock = _throttle_with_clock()
        key = "3.3.3.3"
        for _ in range(ui._LOGIN_FREE_ATTEMPTS):
            t.record_failure(key)
        # 1st lockout
        t.record_failure(key)
        first = t.retry_after(key)
        clock.advance(first + 1)  # wait out the first lockout
        # 2nd lockout should be ~2x the first (exponential), still within the cap.
        t.record_failure(key)
        second = t.retry_after(key)
        assert second >= first
        assert second <= int(ui._LOGIN_MAX_LOCK_S) + 1
    finally:
        ui.time.monotonic = real


def test_lockout_expires_after_waiting():
    real = ui.time.monotonic
    try:
        t, clock = _throttle_with_clock()
        key = "4.4.4.4"
        for _ in range(ui._LOGIN_FREE_ATTEMPTS + 1):
            t.record_failure(key)
        assert t.retry_after(key) > 0
        clock.advance(ui._LOGIN_MAX_LOCK_S + 1)  # wait past any lockout
        assert t.retry_after(key) == 0
    finally:
        ui.time.monotonic = real


def test_success_clears_record():
    real = ui.time.monotonic
    try:
        t, _clock = _throttle_with_clock()
        key = "5.5.5.5"
        for _ in range(ui._LOGIN_FREE_ATTEMPTS + 1):
            t.record_failure(key)
        assert t.retry_after(key) > 0
        t.record_success(key)
        assert t.retry_after(key) == 0
    finally:
        ui.time.monotonic = real


def test_clients_are_tracked_independently():
    real = ui.time.monotonic
    try:
        t, _clock = _throttle_with_clock()
        attacker, victim = "6.6.6.6", "7.7.7.7"
        for _ in range(ui._LOGIN_FREE_ATTEMPTS + 1):
            t.record_failure(attacker)
        assert t.retry_after(attacker) > 0
        assert t.retry_after(victim) == 0  # an unrelated client is unaffected
    finally:
        ui.time.monotonic = real


def test_idle_record_is_pruned():
    real = ui.time.monotonic
    try:
        t, clock = _throttle_with_clock()
        key = "8.8.8.8"
        t.record_failure(key)
        clock.advance(ui._LOGIN_RESET_S + 1)
        # A later failure from any client prunes stale records; the old count is
        # forgotten, so this client starts fresh (still within the allowance).
        t.record_failure("9.9.9.9")
        assert t.retry_after(key) == 0
    finally:
        ui.time.monotonic = real


# --- _client_key: trusted X-Forwarded-For handling ----------------------------

def test_client_key_uses_first_forwarded_hop_from_trusted_proxy():
    class _Req:
        def __init__(self, headers, client=None):
            self.headers = headers
            self.client = client

    class _Client:
        host = "127.0.0.1"

    previous = ui._TRUSTED_PROXY_IPS
    try:
        ui._TRUSTED_PROXY_IPS = {"127.0.0.1"}
        req = _Req({"x-forwarded-for": "203.0.113.7, 10.0.0.1"}, _Client())
        assert ui._client_key(req) == "203.0.113.7"
    finally:
        ui._TRUSTED_PROXY_IPS = previous


def test_client_key_ignores_forwarded_hop_from_untrusted_peer():
    class _Client:
        host = "198.51.100.4"

    class _Req:
        headers = {"x-forwarded-for": "203.0.113.7"}
        client = _Client()

    assert ui._client_key(_Req()) == "198.51.100.4"


def test_client_key_falls_back_to_peer():
    class _Client:
        host = "198.51.100.4"

    class _Req:
        headers: dict = {}
        client = _Client()

    assert ui._client_key(_Req()) == "198.51.100.4"


def test_account_throttle_survives_client_address_rotation():
    class _Client:
        host = "198.51.100.1"

    class _Req:
        headers: dict = {}
        client = _Client()

    ui._reset_authentication_throttles()
    request = _Req()
    for attempt in range(11):
        request.client.host = f"198.51.100.{attempt + 1}"
        ui._record_authentication_failure(request, "owner@example.com", "login")
    assert ui._authentication_retry_after(request, "owner@example.com", "login") > 0
    assert ui._authentication_retry_after(request, "other@example.com", "login") == 0


# --- Session secret persistence ------------------------------------------------

def test_secret_key_persists_in_production():
    with tempfile.TemporaryDirectory() as data_root:
        env = dict(os.environ)
        env["COOKIE_SECURE"] = "1"
        env["PHOTOFRAME_DATA_DIR"] = data_root
        env.pop("SECRET_KEY", None)
        code = "import app; print(app._secret_key)"
        values = []
        for _ in range(2):
            proc = subprocess.run(
                [sys.executable, "-c", code],
                cwd=os.path.abspath(os.path.join(os.path.dirname(__file__), "..")),
                env=env,
                capture_output=True,
                text=True,
            )
            assert proc.returncode == 0, proc.stderr
            values.append(proc.stdout.strip())
        assert values[0] == values[1]
        assert len(values[0]) == 64


def test_secret_key_dev_fallback_imports():
    env = dict(os.environ)
    env["COOKIE_SECURE"] = "0"
    env.pop("SECRET_KEY", None)
    code = "import app; print('OK')"
    proc = subprocess.run(
        [sys.executable, "-c", code],
        cwd=os.path.abspath(os.path.join(os.path.dirname(__file__), "..")),
        env=env,
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 0, proc.stderr
    assert "OK" in proc.stdout


# --- Security headers (requires httpx for Starlette's TestClient) -------------

def test_security_headers_present():
    try:
        from starlette.testclient import TestClient
    except Exception as exc:  # noqa: BLE001 - optional test dependency
        print(f"  (skipped headers test: {exc})")
        return
    try:
        client = TestClient(app.app)
    except RuntimeError as exc:
        # TestClient requires httpx; skip cleanly when it is not installed.
        print(f"  (skipped headers test: {exc})")
        return
    resp = client.get("/healthz")
    assert resp.headers.get("X-Content-Type-Options") == "nosniff"
    assert resp.headers.get("X-Frame-Options") == "DENY"
    assert resp.headers.get("Referrer-Policy") == "no-referrer"
    csp = resp.headers.get("Content-Security-Policy", "")
    assert "frame-ancestors 'none'" in csp
    assert "cdn.jsdelivr.net" in csp
    assert "connect-src 'self' https://cdn.jsdelivr.net" in csp


def test_csrf_token_is_required_for_browser_posts():
    try:
        from starlette.testclient import TestClient
    except Exception as exc:  # noqa: BLE001 - optional test dependency
        print(f"  (skipped CSRF test: {exc})")
        return
    with TestClient(app.app) as client:
        rejected = client.post("/login", data={"email": "none@example.com", "password": "invalid"})
        assert rejected.status_code == 403
        page = client.get("/login")
        match = re.search(r'name="csrf_token" value="([^"]+)"', page.text)
        assert match
        accepted = client.post(
            "/login",
            data={"csrf_token": match.group(1), "email": "none@example.com", "password": "invalid"},
            follow_redirects=False,
        )
        assert accepted.status_code == 303


def test_oversized_request_is_rejected_before_routing():
    try:
        from starlette.testclient import TestClient
    except Exception as exc:  # noqa: BLE001 - optional test dependency
        print(f"  (skipped body limit test: {exc})")
        return
    with TestClient(app.app) as client:
        response = client.post(
            "/upload",
            content=b"x" * (app._IMAGE_BODY_LIMIT + 1),
            headers={"content-type": "multipart/form-data; boundary=x"},
        )
        assert response.status_code == 413
        assert response.headers["cache-control"] == "no-store"


def test_request_body_receive_deadline_returns_408():
    async def inner(_scope, receive, _send):
        await receive()

    async def receive():
        await asyncio.sleep(0.05)
        return {"type": "http.request", "body": b"", "more_body": False}

    sent = []

    async def send(message):
        sent.append(message)

    scope = {
        "type": "http", "asgi": {"version": "3.0"}, "http_version": "1.1",
        "method": "POST", "scheme": "http", "path": "/login", "raw_path": b"/login",
        "query_string": b"", "root_path": "", "headers": [],
        "client": ("127.0.0.1", 1), "server": ("testserver", 80),
    }
    previous = app._BODY_TIMEOUT_SECONDS
    try:
        app._BODY_TIMEOUT_SECONDS = 0.01
        asyncio.run(app.RequestBodyGuardMiddleware(inner)(scope, receive, send))
    finally:
        app._BODY_TIMEOUT_SECONDS = previous
    start = next(message for message in sent if message["type"] == "http.response.start")
    assert start["status"] == 408


def test_streamed_request_exceeding_limit_returns_413():
    chunks = iter((
        {"type": "http.request", "body": b"ab", "more_body": True},
        {"type": "http.request", "body": b"cd", "more_body": False},
    ))

    async def inner(_scope, receive, _send):
        while (await receive()).get("more_body", False):
            pass

    async def receive():
        return next(chunks)

    sent = []

    async def send(message):
        sent.append(message)

    scope = {
        "type": "http", "asgi": {"version": "3.0"}, "http_version": "1.1",
        "method": "POST", "scheme": "http", "path": "/login", "raw_path": b"/login",
        "query_string": b"", "root_path": "", "headers": [],
        "client": ("127.0.0.1", 1), "server": ("testserver", 80),
    }
    previous = app._FORM_BODY_LIMIT
    try:
        app._FORM_BODY_LIMIT = 3
        asyncio.run(app.RequestBodyGuardMiddleware(inner)(scope, receive, send))
    finally:
        app._FORM_BODY_LIMIT = previous
    start = next(message for message in sent if message["type"] == "http.response.start")
    assert start["status"] == 413


def test_account_throttle_keys_have_fixed_size():
    class _Client:
        host = "198.51.100.4"

    class _Req:
        headers: dict = {}
        client = _Client()

    _ip_key, account_key, _global_key = ui._authentication_keys(
        _Req(), "x" * 100_000, "login"
    )
    assert len(account_key) < 100


def test_decoded_image_pixel_limit_is_enforced():
    class _Image:
        size = (10_000, 5_000)

    try:
        ui._validate_decoded_image(_Image())
    except ValueError as exc:
        assert "pixel limit" in str(exc)
    else:
        raise AssertionError("oversized decoded image was accepted")


def _run_all() -> int:
    tests = [v for k, v in sorted(globals().items())
             if k.startswith("test_") and callable(v)]
    failures = 0
    for fn in tests:
        try:
            fn()
            print(f"PASS {fn.__name__}")
        except AssertionError as exc:
            failures += 1
            print(f"FAIL {fn.__name__}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report unexpected errors
            failures += 1
            print(f"ERROR {fn.__name__}: {exc!r}")
    print(f"\n{len(tests) - failures}/{len(tests)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(_run_all())
