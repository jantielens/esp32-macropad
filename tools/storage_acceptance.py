#!/usr/bin/env python3
"""Exercise primary storage through existing portal APIs and emit JSON Lines."""

import argparse
import base64
import hashlib
import json
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib


ICON_ID = "storage_acceptance"
ICON_NAME = f"{ICON_ID}.png"


def emit(event, **fields):
    print(json.dumps({"event": event, "timestamp": time.time(), **fields}, sort_keys=True), flush=True)


def png_chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))


def deterministic_bytes(seed, length):
    output = bytearray()
    counter = 0
    while len(output) < length:
        output.extend(hashlib.sha256(seed + counter.to_bytes(8, "big")).digest())
        counter += 1
    return bytes(output[:length])


def deterministic_png(card_index, cycle):
    width = height = 320
    seed = f"storage-acceptance:{card_index}:{cycle}".encode("ascii")
    pixels = deterministic_bytes(seed, width * height * 4)
    raw = b"".join(b"\0" + pixels[row * width * 4:(row + 1) * width * 4]
                   for row in range(height))
    png = (b"\x89PNG\r\n\x1a\n" +
           png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
           png_chunk(b"IDAT", zlib.compress(raw, 6)) +
           png_chunk(b"IEND", b""))
    if not 384 * 1024 <= len(png) <= 480 * 1024:
        raise RuntimeError(f"deterministic PNG size out of range: {len(png)}")
    return png


class PortalClient:
    def __init__(self, host, username, password, timeout):
        if not host.startswith(("http://", "https://")):
            host = f"http://{host}"
        self.base_url = host.rstrip("/")
        self.timeout = timeout
        self.auth = None
        if username is not None or password is not None:
            if not username or password is None:
                raise ValueError("portal credentials require both --username and --password")
            token = base64.b64encode(f"{username}:{password}".encode("utf-8")).decode("ascii")
            self.auth = f"Basic {token}"

    def request(self, method, path, body=None, content_type=None):
        headers = {}
        if self.auth:
            headers["Authorization"] = self.auth
        if content_type:
            headers["Content-Type"] = content_type
        request = urllib.request.Request(self.base_url + path, data=body, headers=headers, method=method)
        started = time.monotonic()
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                return response.status, response.read(), time.monotonic() - started
        except urllib.error.HTTPError as error:
            return error.code, error.read(), time.monotonic() - started

    def health(self):
        status, body, elapsed = self.request("GET", "/api/health")
        if status != 200:
            raise RuntimeError(f"health request failed with HTTP {status}")
        return json.loads(body), elapsed

    def upload_icon(self, payload):
        return self.request("POST", f"/api/icons/install?id={ICON_ID}", payload, "image/png")

    def download_icon(self):
        return self.request("GET", f"/api/icons/file?name={urllib.parse.quote(ICON_NAME)}")

    def delete_icon(self):
        return self.request("DELETE", f"/api/icons/file?name={urllib.parse.quote(ICON_NAME)}")

    def play_sound(self, name):
        return self.request("POST", f"/api/sounds/play?name={urllib.parse.quote(name)}")


def run_cycle(client, card_index, cycle):
    payload = deterministic_png(card_index, cycle)
    expected_hash = hashlib.sha256(payload).hexdigest()
    upload_status, _, upload_elapsed = client.upload_icon(payload)
    if upload_status != 200:
        raise RuntimeError(f"upload failed with HTTP {upload_status}")
    download_status, downloaded, download_elapsed = client.download_icon()
    actual_hash = hashlib.sha256(downloaded).hexdigest()
    if download_status != 200 or downloaded != payload:
        raise RuntimeError(f"download verification failed with HTTP {download_status}")
    health, health_elapsed = client.health()
    emit("storage_cycle", card_index=card_index, cycle=cycle, seed=f"{card_index}:{cycle}",
         bytes_sent=len(payload), bytes_received=len(downloaded), expected_sha256=expected_hash,
         actual_sha256=actual_hash, upload_http_status=upload_status,
         download_http_status=download_status, upload_elapsed_seconds=upload_elapsed,
         download_elapsed_seconds=download_elapsed, health_elapsed_seconds=health_elapsed,
         fs_backend=health.get("fs_backend"), fs_mounted=health.get("fs_mounted"))
    return len(payload), upload_elapsed


def cleanup_icon(client):
    status, _, elapsed = client.delete_icon()
    if status not in (200, 404):
        raise RuntimeError(f"icon cleanup failed with HTTP {status}")
    emit("cleanup", icon=ICON_NAME, http_status=status, elapsed_seconds=elapsed)


def cards_mode(client, args):
    try:
        for cycle in range(args.cycles):
            run_cycle(client, args.card_index, cycle)
    finally:
        cleanup_icon(client)


def concurrent_mode(client, args):
    deadline = time.monotonic() + args.duration_minutes * 60
    next_cycle = next_health = next_audio = time.monotonic()
    cycle = 0
    try:
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_health:
                health, elapsed = client.health()
                emit("health", elapsed_seconds=elapsed, fs_backend=health.get("fs_backend"),
                     fs_mounted=health.get("fs_mounted"), reset_reason=health.get("reset_reason"),
                     heap_dma_internal_min=health.get("heap_dma_internal_min"))
                next_health += args.health_interval_seconds
            if now >= next_cycle:
                run_cycle(client, args.card_index, cycle)
                cycle += 1
                next_cycle += args.interval_seconds
            if now >= next_audio:
                status, _, elapsed = client.play_sound(args.audio_name)
                emit("audio_play", audio_name=args.audio_name, http_status=status, elapsed_seconds=elapsed)
                if status != 200:
                    raise RuntimeError(f"sound playback request failed with HTTP {status}")
                next_audio += args.audio_interval_seconds
            time.sleep(min(0.25, max(0.01, deadline - time.monotonic())))
    finally:
        cleanup_icon(client)


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="mode", required=True)
    cards = subparsers.add_parser("cards", help="Run deterministic icon verification cycles")
    add_connection_arguments(cards)
    cards.add_argument("--card-index", type=int, required=True)
    cards.add_argument("--cycles", type=int, required=True)
    concurrent = subparsers.add_parser("concurrent", help="Run icon, health, and audio traffic concurrently")
    add_connection_arguments(concurrent)
    concurrent.add_argument("--card-index", type=int, default=0)
    concurrent.add_argument("--duration-minutes", type=float, required=True)
    concurrent.add_argument("--interval-seconds", type=float, required=True)
    concurrent.add_argument("--health-interval-seconds", type=float, required=True)
    concurrent.add_argument("--audio-name", required=True)
    concurrent.add_argument("--audio-interval-seconds", type=float, required=True)
    return parser


def add_connection_arguments(parser):
    parser.add_argument("--host", required=True, help="Device host or base URL")
    parser.add_argument("--username", help="Optional portal Basic Auth username")
    parser.add_argument("--password", help="Optional portal Basic Auth password")
    parser.add_argument("--timeout", type=float, default=30.0, help="HTTP timeout in seconds")


def main():
    args = build_parser().parse_args()
    try:
        client = PortalClient(args.host, args.username, args.password, args.timeout)
        if args.mode == "cards":
            cards_mode(client, args)
        else:
            concurrent_mode(client, args)
    except (RuntimeError, ValueError, urllib.error.URLError, json.JSONDecodeError) as error:
        emit("failure", error=str(error))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())