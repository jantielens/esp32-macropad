#!/usr/bin/env python3
"""Offline simulator for the photoframe two-bucket selection.

Runs the *same* pure scheduler that /api/v1/next uses (store.bucket_schedule_pick)
over a synthetic gallery, advancing each photo's last_shown_at and the temp-slot
countdown in memory between draws. Lets you watch how permanent and temporary
photos interleave -- and confirm a temporary photo's share is pool-independent --
without deploying or touching persistent site data.

The scheduler is deterministic (least-recently-shown within each bucket + a
countdown), so runs are reproducible without a random seed.

Examples:

    # One temporary photo among 20 permanents, knob n=4 (temp ~1 in 4)
    python3 simulate_selection.py --perm 20 --temp 1 --n 4

    # Two temporaries share the temp slot (T1,P,T2,P,...) at n=3
    python3 simulate_selection.py --perm 20 --temp 2 --n 3 --show-sequence 16

    # Fresh lifecycle: a new permanent photo is featured for 7 days, then graduates
    python3 simulate_selection.py --fresh --perm 1000 --fresh-count 1 --window-days 7

    # Bulk upload of 50 fresh photos, capped at 25% of displays
    python3 simulate_selection.py --fresh --perm 1000 --fresh-count 50 --max-share 25
"""

from __future__ import annotations

import argparse
import os
import sys
from datetime import datetime, timedelta, timezone

sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))

import store  # noqa: E402

NOW = datetime(2026, 1, 1, 12, 0, 0, tzinfo=timezone.utc)


def build_ids(perm: int, temp: int) -> tuple[list[str], list[str]]:
    """Synthetic permanent and temporary id lists (all start never-shown)."""
    perm_ids = [f"P{i:03d}" for i in range(perm)]
    temp_ids = [f"T{i:02d}" for i in range(temp)]
    return perm_ids, temp_ids


def simulate(
    perm_ids: list[str],
    temp_ids: list[str],
    *,
    n: int,
    draws: int,
    floor: int = 2,
    advance_h: float = 0.05,
) -> dict:
    """Run ``draws`` selections through the pure scheduler.

    Returns counts per id, the chosen-source sequence ("P"/"T"), and the temp set.
    ``advance_h`` is the per-draw clock step (0.05h = 3 min, a typical cycle).
    ``floor`` caps the featured bucket's combined share (see share_pct_to_floor);
    here ``temp_ids`` stands in for the whole featured bucket (temporary + fresh).
    """
    last_shown: dict[str, datetime | None] = {i: None for i in perm_ids + temp_ids}
    counts: dict[str, int] = {i: 0 for i in perm_ids + temp_ids}
    temp_set = set(temp_ids)
    countdown = 0
    clock = NOW
    sequence: list[str] = []

    for _ in range(draws):
        perm = [(i, last_shown[i]) for i in perm_ids]
        temp = [(i, last_shown[i]) for i in temp_ids]
        chosen, source, countdown = store.bucket_schedule_pick(
            perm, temp, temp_countdown=countdown, n=n, floor=floor
        )
        if chosen is None:
            break
        counts[chosen] += 1
        last_shown[chosen] = clock
        sequence.append("T" if chosen in temp_set else "P")
        clock = clock + timedelta(hours=advance_h)

    return {"counts": counts, "sequence": sequence, "temp_set": temp_set}


def _bar(frac: float, width: int = 30) -> str:
    filled = int(round(frac * width))
    return "#" * filled + "." * (width - filled)


def report(result: dict, *, n: int, show_sequence: int, floor: int = 2) -> None:
    counts = result["counts"]
    temp_set = result["temp_set"]
    total = sum(counts.values()) or 1
    perm_ids = [i for i in counts if i not in temp_set]
    temp_ids = [i for i in counts if i in temp_set]

    print(f"\nDraws: {total}   knob n={n}   permanents={len(perm_ids)}   featured={len(temp_ids)}\n")

    if show_sequence and result["sequence"]:
        head = "".join(result["sequence"][:show_sequence])
        print(f"First {min(show_sequence, len(result['sequence']))} displays: {head}\n")

    if temp_ids:
        temp_total = sum(counts[i] for i in temp_ids)
        spacing = store.temp_slot_spacing(n, len(temp_ids), floor)
        print("Featured photos (temporary + fresh):")
        for image_id in sorted(temp_ids):
            frac = counts[image_id] / total
            print(f"  {image_id}  {counts[image_id]:6d}  {frac*100:5.1f}%  {_bar(frac)}")
        print(
            f"  bucket share: {temp_total/total*100:.1f}%   "
            f"temp slot every {spacing} displays   "
            f"target per-photo <= 1/{n} = {100.0/n:.1f}%\n"
        )

    if perm_ids:
        perm_shares = [counts[i] / total for i in perm_ids]
        lo, hi = min(perm_shares), max(perm_shares)
        mean = sum(perm_shares) / len(perm_shares)
        print("Permanent photos:")
        print(
            f"  mean {mean*100:.3f}%   spread [{lo*100:.3f}% .. {hi*100:.3f}%]   "
            f"(even rotation -> tight spread)\n"
        )


def simulate_fresh(
    pool: int,
    n_fresh: int,
    *,
    window_days: int,
    n: int,
    floor: int,
    days: int,
    per_day: int,
) -> dict:
    """Advance wall-clock so newly uploaded photos are featured, then graduate.

    Models the fresh lifecycle end to end: ``pool`` aged permanents (uploaded long
    ago, never fresh) plus ``n_fresh`` photos uploaded at t=0. Fresh membership in
    the featured bucket is *derived* from uploaded_at via store.is_fresh, so a
    photo leaves the bucket automatically once it ages past ``window_days`` -- no
    graduation event. Displays ``per_day`` images for ``days`` days.

    Returns per-day fresh-bucket hit counts and the window length in displays.
    """
    aged = (NOW - timedelta(days=max(window_days * 4, 100))).isoformat()
    photos: dict[str, dict] = {}
    for i in range(pool):
        photos[f"P{i:04d}"] = {"permanent": True, "expires_at": None,
                               "uploaded_at": aged, "last_shown_at": None}
    for j in range(n_fresh):
        photos[f"F{j:02d}"] = {"permanent": True, "expires_at": None,
                               "uploaded_at": NOW.isoformat(), "last_shown_at": None}

    step_h = 24.0 / max(1, per_day)
    countdown = 0
    clock = NOW
    fresh_hits = [0] * days
    for _ in range(days * per_day):
        now = clock
        perm: list = []
        temp: list = []
        for pid, meta in photos.items():
            shown = store._parse_iso(meta["last_shown_at"])
            if meta.get("expires_at") or store.is_fresh(meta, now=now, window_days=window_days):
                temp.append((pid, shown))
            else:
                perm.append((pid, shown))
        chosen, _source, countdown = store.bucket_schedule_pick(
            perm, temp, temp_countdown=countdown, n=n, floor=floor
        )
        photos[chosen]["last_shown_at"] = now.isoformat()
        day = int((now - NOW).total_seconds() // 86400)
        if chosen.startswith("F") and 0 <= day < days:
            fresh_hits[day] += 1
        clock = clock + timedelta(hours=step_h)

    return {"fresh_hits": fresh_hits, "per_day": per_day, "window_days": window_days,
            "pool": pool, "n_fresh": n_fresh}


def report_fresh(result: dict, *, n: int, max_share: int) -> None:
    fresh_hits = result["fresh_hits"]
    per_day = result["per_day"]
    window = result["window_days"]
    pool = result["pool"]
    n_fresh = result["n_fresh"]
    days = len(fresh_hits)

    print(
        f"\nFresh lifecycle: {pool} aged permanents + {n_fresh} uploaded at t=0\n"
        f"window={window}d   knob n={n}   max-share={max_share}%   "
        f"displays/day={per_day}\n"
    )
    print("Day  fresh shows   share")
    for d, hits in enumerate(fresh_hits):
        frac = hits / per_day if per_day else 0.0
        if d < window:
            tag = "  <- featured (fresh window)"
        elif d == window:
            tag = "  <- graduated to normal rotation"
        else:
            tag = ""
        print(f"{d:3d}  {hits:10d}   {frac*100:5.1f}%  {_bar(frac)}{tag}")

    win_draws = window * per_day
    in_window = sum(fresh_hits[:window])
    after = sum(fresh_hits[window:])
    after_draws = max(0, (days - window) * per_day)
    baseline = 1.0 / (pool + n_fresh)
    print(
        f"\nDuring window:    {in_window:5d} / {win_draws} displays = "
        f"{(in_window/win_draws*100 if win_draws else 0):.1f}% featured\n"
        f"After graduation: {after:5d} / {after_draws} displays = "
        f"{(after/after_draws*100 if after_draws else 0):.2f}% "
        f"(normal odds ~{baseline*100:.3f}% each)\n"
        f"Without the fresh boost a new photo would draw ~{baseline*100:.3f}% "
        f"of displays from day 0 (lost in a pool of {pool + n_fresh}).\n"
    )


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--perm", type=int, default=20, help="number of permanent photos")
    p.add_argument("--temp", type=int, default=1,
                   help="number of featured photos (temporary and/or fresh)")
    p.add_argument("--n", type=int, default=4, help="temp_min_spacing knob (>=2)")
    p.add_argument("--max-share", type=int, default=50, dest="max_share",
                   help="max featured-bucket share %% (caps share at 1/ceil(100/pct))")
    p.add_argument("--draws", type=int, default=600, help="number of displays to simulate")
    p.add_argument("--show-sequence", type=int, default=24, dest="show_sequence",
                   help="print the first N displays as a P/T string (0 to hide)")
    p.add_argument("--fresh", action="store_true",
                   help="time-axis mode: feature newly uploaded photos for the fresh "
                        "window, then watch them graduate to normal rotation")
    p.add_argument("--fresh-count", type=int, default=1, dest="fresh_count",
                   help="[--fresh] number of photos uploaded at t=0 (e.g. a bulk upload)")
    p.add_argument("--window-days", type=int, default=7, dest="window_days",
                   help="[--fresh] fresh_window_days knob")
    p.add_argument("--days", type=int, default=14,
                   help="[--fresh] number of days to simulate")
    p.add_argument("--per-day", type=int, default=24, dest="per_day",
                   help="[--fresh] displays per day (the device's poll cadence)")
    args = p.parse_args()

    n = max(2, args.n)
    floor = store.share_pct_to_floor(args.max_share)

    if args.fresh:
        result = simulate_fresh(
            args.perm, max(0, args.fresh_count),
            window_days=max(0, args.window_days), n=n, floor=floor,
            days=max(1, args.days), per_day=max(1, args.per_day),
        )
        report_fresh(result, n=n, max_share=args.max_share)
        return 0

    perm_ids, temp_ids = build_ids(args.perm, args.temp)
    result = simulate(perm_ids, temp_ids, n=n, draws=args.draws, floor=floor)
    report(result, n=n, show_sequence=args.show_sequence, floor=floor)
    return 0


if __name__ == "__main__":
    sys.exit(main())
