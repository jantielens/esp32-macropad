#!/usr/bin/env python3
"""Unit tests for the login throttle, SECRET_KEY fail-fast, and response headers.

Standalone (no pytest needed): run `python3 tests/test_security.py`. The throttle
test drives a fake monotonic clock so lockout/expiry are deterministic; the
fail-fast test imports the app in a subprocess with a controlled environment; the
header test uses Starlette's TestClient and is skipped if httpx is unavailable.
"""

from __future__ import annotations

import os
import subprocess
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

# The app module reads COOKIE_SECURE / SECRET_KEY at import time; pin a dev-safe
# environment before importing so the import itself never fails fast here.
os.environ["COOKIE_SECURE"] = "0"
os.environ.pop("SECRET_KEY", None)

import app  # noqa: E402


class _FakeClock:
    """Replacement for time.monotonic with a manually advanced value."""

    def __init__(self) -> None:
        self.t = 1000.0

    def __call__(self) -> float:
        return self.t

    def advance(self, seconds: float) -> None:
        self.t += seconds


def _throttle_with_clock() -> tuple[app._LoginThrottle, _FakeClock]:
    """A fresh throttle wired to a fake clock (patches app.time.monotonic)."""
    clock = _FakeClock()
    app.time.monotonic = clock  # module-global patch; restored per-test by caller
    return app._LoginThrottle(), clock


# --- Login throttle: allowance + lockout --------------------------------------

def test_free_attempts_are_not_locked():
    real = app.time.monotonic
    try:
        t, _clock = _throttle_with_clock()
        key = "1.1.1.1"
        for _ in range(app._LOGIN_FREE_ATTEMPTS):
            assert t.retry_after(key) == 0
            t.record_failure(key)
        # Still at the allowance boundary: not yet locked.
        assert t.retry_after(key) == 0
    finally:
        app.time.monotonic = real


def test_lockout_engages_after_free_attempts():
    real = app.time.monotonic
    try:
        t, _clock = _throttle_with_clock()
        key = "2.2.2.2"
        for _ in range(app._LOGIN_FREE_ATTEMPTS + 1):
            t.record_failure(key)
        # First lockout is the base duration (retry_after rounds up by 1s).
        wait = t.retry_after(key)
        assert wait > 0
        assert wait <= int(app._LOGIN_BASE_LOCK_S) + 1
    finally:
        app.time.monotonic = real


def test_lockout_backoff_is_exponential():
    real = app.time.monotonic
    try:
        t, clock = _throttle_with_clock()
        key = "3.3.3.3"
        for _ in range(app._LOGIN_FREE_ATTEMPTS):
            t.record_failure(key)
        # 1st lockout
        t.record_failure(key)
        first = t.retry_after(key)
        clock.advance(first + 1)  # wait out the first lockout
        # 2nd lockout should be ~2x the first (exponential), still within the cap.
        t.record_failure(key)
        second = t.retry_after(key)
        assert second >= first
        assert second <= int(app._LOGIN_MAX_LOCK_S) + 1
    finally:
        app.time.monotonic = real


def test_lockout_expires_after_waiting():
    real = app.time.monotonic
    try:
        t, clock = _throttle_with_clock()
        key = "4.4.4.4"
        for _ in range(app._LOGIN_FREE_ATTEMPTS + 1):
            t.record_failure(key)
        assert t.retry_after(key) > 0
        clock.advance(app._LOGIN_MAX_LOCK_S + 1)  # wait past any lockout
        assert t.retry_after(key) == 0
    finally:
        app.time.monotonic = real


def test_success_clears_record():
    real = app.time.monotonic
    try:
        t, _clock = _throttle_with_clock()
        key = "5.5.5.5"
        for _ in range(app._LOGIN_FREE_ATTEMPTS + 1):
            t.record_failure(key)
        assert t.retry_after(key) > 0
        t.record_success(key)
        assert t.retry_after(key) == 0
    finally:
        app.time.monotonic = real


def test_clients_are_tracked_independently():
    real = app.time.monotonic
    try:
        t, _clock = _throttle_with_clock()
        attacker, victim = "6.6.6.6", "7.7.7.7"
        for _ in range(app._LOGIN_FREE_ATTEMPTS + 1):
            t.record_failure(attacker)
        assert t.retry_after(attacker) > 0
        assert t.retry_after(victim) == 0  # an unrelated client is unaffected
    finally:
        app.time.monotonic = real


def test_idle_record_is_pruned():
    real = app.time.monotonic
    try:
        t, clock = _throttle_with_clock()
        key = "8.8.8.8"
        t.record_failure(key)
        clock.advance(app._LOGIN_RESET_S + 1)
        # A later failure from any client prunes stale records; the old count is
        # forgotten, so this client starts fresh (still within the allowance).
        t.record_failure("9.9.9.9")
        assert t.retry_after(key) == 0
    finally:
        app.time.monotonic = real


# --- _client_key: X-Forwarded-For handling ------------------------------------

def test_client_key_uses_first_forwarded_hop():
    class _Req:
        def __init__(self, headers, client=None):
            self.headers = headers
            self.client = client

    req = _Req({"x-forwarded-for": "203.0.113.7, 10.0.0.1, 10.0.0.2"})
    assert app._client_key(req) == "203.0.113.7"


def test_client_key_falls_back_to_peer():
    class _Client:
        host = "198.51.100.4"

    class _Req:
        headers: dict = {}
        client = _Client()

    assert app._client_key(_Req()) == "198.51.100.4"


# --- SECRET_KEY fail-fast (subprocess: import must raise in production) --------

def test_secret_key_required_in_production():
    env = dict(os.environ)
    env["COOKIE_SECURE"] = "1"
    env.pop("SECRET_KEY", None)
    code = (
        "import app; "
        "raise SystemExit('FAIL: import should have raised RuntimeError')"
    )
    proc = subprocess.run(
        [sys.executable, "-c", code],
        cwd=os.path.abspath(os.path.join(os.path.dirname(__file__), "..")),
        env=env,
        capture_output=True,
        text=True,
    )
    assert proc.returncode != 0
    assert "SECRET_KEY is required" in proc.stderr, proc.stderr


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
