#!/usr/bin/env python3
"""Latency benchmark for the photoframe ``/api/next`` device endpoint.

``/api/next`` is polled by every e-paper board on a cadence, so each second
shaved off the response compounds across the fleet. This harness drives the
endpoint repeatedly and reports where the time goes, combining two views:

  * Client-side wall clock -- connect (incl. TLS), TTFB (request sent ->
    response headers), and total (-> body fully read). This is what the device
    actually experiences.
  * Server-side breakdown -- parsed from the ``Server-Timing`` header the app
    emits per request (config / settings / select / meta / served / deliver /
    blob round-trips / total). This attributes the server's own time per phase
    without guessing.

Subtracting a local run (dev machine -> Azure Blob, long-haul handshakes) from
an Azure run (app co-located with Blob, intra-region round-trips) separates
algorithmic cost (number of round-trips) from network-distance cost.

Two request modes:

  * ``decision`` (default) -- does NOT follow the 302 redirect, so it times only
    the selection logic plus the serve-time writes (the part we control). This
    is the right signal for "how long does the server take to decide".
  * ``full`` -- adds ``?proxy=1`` so the server streams the image bytes inline,
    timing the entire server cycle including the app-side blob download.

Credentials are never passed on the command line by default: point ``--config``
at ``config.local.json`` (the same file the app loads) and the device id + pull
key are read from it. The key is masked in all output.

Examples:

    # Local app (started via ./run_local.sh), decision-only, 50 polls
    python3 bench_next.py --config config.local.json \
        --base-url http://127.0.0.1:8000 --n 50

    # Deployed Azure app, full image delivery path
    python3 bench_next.py --config config.local.json \
        --base-url https://app-esp32-photoframe-poc.azurewebsites.net \
        --mode full --n 30

    # Explicit credentials (no config file)
    python3 bench_next.py --base-url http://127.0.0.1:8000 \
        --device-id E1003-1 --key <api_key> --n 20
"""

from __future__ import annotations

import argparse
import http.client
import json
import statistics
import sys
import time
from typing import Optional
from urllib.parse import urlsplit


# --- Credentials --------------------------------------------------------------


def load_credentials(
    config_path: Optional[str], device_id: Optional[str], key: Optional[str]
) -> tuple[str, str]:
    """Resolve (device_id, api_key) from explicit args or a config.local.json."""
    if device_id and key:
        return device_id, key
    if not config_path:
        raise SystemExit(
            "Provide both --device-id and --key, or --config pointing at config.local.json"
        )
    with open(config_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    devices = data.get("devices") or {}
    if not devices:
        raise SystemExit(f"No devices in {config_path}")
    if device_id is None:
        device_id = next(iter(devices))
    entry = devices.get(device_id)
    if entry is None:
        raise SystemExit(f"Device '{device_id}' not found in {config_path}")
    api_key = key or entry.get("api_key")
    if not api_key:
        raise SystemExit(f"Device '{device_id}' has no api_key in {config_path}")
    return device_id, api_key


def _mask(secret: str) -> str:
    if len(secret) <= 6:
        return "***"
    return f"{secret[:3]}...{secret[-3:]}"


# --- One request --------------------------------------------------------------


class Result:
    __slots__ = ("status", "connect_ms", "ttfb_ms", "total_ms", "bytes", "server")

    def __init__(self) -> None:
        self.status: int = 0
        self.connect_ms: float = 0.0
        self.ttfb_ms: float = 0.0
        self.total_ms: float = 0.0
        self.bytes: int = 0
        self.server: dict[str, float] = {}


def parse_server_timing(header: Optional[str]) -> dict[str, float]:
    """Parse a ``Server-Timing`` header into ``{metric: duration_ms}``.

    Accepts entries like ``select;dur=210.4`` and ``blob;desc="n=9";dur=88.1``.
    Entries without a ``dur`` are ignored.
    """
    out: dict[str, float] = {}
    if not header:
        return out
    for entry in header.split(","):
        entry = entry.strip()
        if not entry:
            continue
        parts = entry.split(";")
        name = parts[0].strip()
        dur: Optional[float] = None
        for attr in parts[1:]:
            attr = attr.strip()
            if attr.startswith("dur="):
                try:
                    dur = float(attr[4:])
                except ValueError:
                    dur = None
        if name and dur is not None:
            out[name] = dur
    return out


def _new_connection(base_url: str, timeout: float) -> http.client.HTTPConnection:
    parts = urlsplit(base_url)
    host = parts.hostname or ""
    if parts.scheme == "https":
        port = parts.port or 443
        return http.client.HTTPSConnection(host, port, timeout=timeout)
    port = parts.port or 80
    return http.client.HTTPConnection(host, port, timeout=timeout)


def do_request(
    conn: http.client.HTTPConnection, path: str, timeout: float
) -> Result:
    """Issue one GET, timing connect / TTFB / total separately."""
    res = Result()

    t0 = time.perf_counter()
    conn.connect()  # explicit so connect (incl. TLS) is timed apart from TTFB
    res.connect_ms = (time.perf_counter() - t0) * 1000.0

    t_send = time.perf_counter()
    conn.request("GET", path)
    resp = conn.getresponse()  # blocks until response headers arrive
    res.ttfb_ms = (time.perf_counter() - t_send) * 1000.0

    res.status = resp.status
    res.server = parse_server_timing(resp.getheader("Server-Timing"))
    body = resp.read()  # drain so the connection can be reused / closed cleanly
    res.bytes = len(body)
    res.total_ms = (time.perf_counter() - t_send) * 1000.0
    return res


# --- Aggregation / reporting --------------------------------------------------


def _pct(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    rank = q * (len(ordered) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(ordered) - 1)
    frac = rank - lo
    return ordered[lo] + (ordered[hi] - ordered[lo]) * frac


def _row(label: str, values: list[float]) -> str:
    if not values:
        return f"  {label:<14} (no samples)"
    return (
        f"  {label:<14} "
        f"mean {statistics.fmean(values):8.1f}  "
        f"p50 {_pct(values, 0.50):8.1f}  "
        f"p90 {_pct(values, 0.90):8.1f}  "
        f"p99 {_pct(values, 0.99):8.1f}  "
        f"min {min(values):8.1f}  "
        f"max {max(values):8.1f}"
    )


def report(results: list[Result], *, base_url: str, mode: str, reuse: bool) -> None:
    ok = [r for r in results if r.status in (200, 204, 302)]
    print()
    print("=" * 78)
    print(f"  /api/next benchmark  -  {base_url}")
    print(f"  mode={mode}  samples={len(results)}  ok={len(ok)}  "
          f"connection={'keep-alive' if reuse else 'fresh-per-request'}")
    print("=" * 78)

    # Status distribution
    status_counts: dict[int, int] = {}
    for r in results:
        status_counts[r.status] = status_counts.get(r.status, 0) + 1
    dist = "  ".join(f"{s}:{c}" for s, c in sorted(status_counts.items()))
    print(f"  statuses: {dist}")
    if ok:
        print(f"  body bytes (last ok): {ok[-1].bytes}")

    print()
    print("  Client-side (ms)            mean       p50       p90       p99       min       max")
    print("  " + "-" * 76)
    print(_row("connect", [r.connect_ms for r in ok]))
    print(_row("ttfb", [r.ttfb_ms for r in ok]))
    print(_row("total", [r.total_ms for r in ok]))

    # Server-side phases, in a stable, meaningful order. Any extra keys present
    # in the header are appended after the known ones.
    known_order = [
        "config", "settings", "select", "meta", "served", "deliver", "blob", "total",
    ]
    phase_values: dict[str, list[float]] = {}
    for r in ok:
        for name, dur in r.server.items():
            phase_values.setdefault(name, []).append(dur)
    ordered_phases = [p for p in known_order if p in phase_values]
    ordered_phases += [p for p in phase_values if p not in known_order]

    if phase_values:
        print()
        print("  Server-Timing (ms)          mean       p50       p90       p99       min       max")
        print("  " + "-" * 76)
        for name in ordered_phases:
            print(_row(name, phase_values[name]))
    else:
        print()
        print("  (no Server-Timing header -- is this the instrumented app?)")

    print("=" * 78)
    print()


# --- Main ---------------------------------------------------------------------


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark the photoframe /api/next endpoint.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--base-url", required=True,
                        help="App base URL, e.g. http://127.0.0.1:8000")
    parser.add_argument("--config", default=None,
                        help="Path to config.local.json to read device id + key from")
    parser.add_argument("--device-id", default=None,
                        help="Device id (defaults to first device in --config)")
    parser.add_argument("--key", default=None,
                        help="Device api_key (overrides --config)")
    parser.add_argument("--n", type=int, default=30,
                        help="Number of measured requests (default 30)")
    parser.add_argument("--warmup", type=int, default=3,
                        help="Warmup requests excluded from stats (default 3)")
    parser.add_argument("--mode", choices=("decision", "full"), default="decision",
                        help="decision: don't follow 302 (selection logic only); "
                             "full: proxy=1 so the server streams the image inline")
    parser.add_argument("--reuse", action="store_true",
                        help="Reuse one keep-alive connection instead of a fresh "
                             "connection per request (isolates handshake cost)")
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="Per-request timeout in seconds (default 30)")
    args = parser.parse_args(argv)

    device_id, api_key = load_credentials(args.config, args.device_id, args.key)
    base_url = args.base_url.rstrip("/")

    path = f"/api/next?device_id={device_id}&key={api_key}"
    if args.mode == "full":
        path += "&proxy=1"

    print(f"Target : {base_url}/api/next")
    print(f"Device : {device_id}  key={_mask(api_key)}")
    print(f"Mode   : {args.mode}   warmup={args.warmup}  measured={args.n}  "
          f"connection={'keep-alive' if args.reuse else 'fresh-per-request'}")

    conn: Optional[http.client.HTTPConnection] = None
    if args.reuse:
        conn = _new_connection(base_url, args.timeout)

    def run_once() -> Result:
        nonlocal conn
        if args.reuse:
            assert conn is not None
            try:
                return do_request(conn, path, args.timeout)
            except http.client.HTTPException:
                # Server closed the keep-alive socket; reopen and retry once.
                conn.close()
                conn = _new_connection(base_url, args.timeout)
                return do_request(conn, path, args.timeout)
        fresh = _new_connection(base_url, args.timeout)
        try:
            return do_request(fresh, path, args.timeout)
        finally:
            fresh.close()

    # Warmup (e.g. cold App Service worker, JIT of code paths, DNS cache).
    for i in range(max(0, args.warmup)):
        try:
            run_once()
        except Exception as exc:  # noqa: BLE001 - warmup failures are informational
            print(f"  warmup {i + 1} failed: {exc}", file=sys.stderr)

    results: list[Result] = []
    first_err: Optional[str] = None
    for i in range(args.n):
        try:
            results.append(run_once())
        except Exception as exc:  # noqa: BLE001 - record and continue benchmarking
            if first_err is None:
                first_err = f"{type(exc).__name__}: {exc}"
            r = Result()
            r.status = -1
            results.append(r)

    if args.reuse and conn is not None:
        conn.close()

    report(results, base_url=base_url, mode=args.mode, reuse=args.reuse)

    if first_err:
        print(f"  note: at least one request errored ({first_err})", file=sys.stderr)

    ok = [r for r in results if r.status in (200, 204, 302)]
    if not ok:
        print("  ERROR: no successful responses -- check base URL, device id, and key.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
