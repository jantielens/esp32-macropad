#!/usr/bin/env python3
"""Offline simulator for the photoframe two-bucket selection.

Runs the *same* pure scheduler that /api/next uses (store.bucket_schedule_pick)
over a synthetic gallery, advancing each photo's last_shown_at and the temp-slot
countdown in memory between draws. Lets you watch how permanent and temporary
photos interleave -- and confirm a temporary photo's share is pool-independent --
without deploying or touching Azure.

The scheduler is deterministic (least-recently-shown within each bucket + a
countdown), so runs are reproducible without a random seed.

Examples:

    # One temporary photo among 20 permanents, knob n=4 (temp ~1 in 4)
    python3 simulate_selection.py --perm 20 --temp 1 --n 4

    # Two temporaries share the temp slot (T1,P,T2,P,...) at n=3
    python3 simulate_selection.py --perm 20 --temp 2 --n 3 --show-sequence 16
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
    advance_h: float = 0.05,
) -> dict:
    """Run ``draws`` selections through the pure scheduler.

    Returns counts per id, the chosen-source sequence ("P"/"T"), and the temp set.
    ``advance_h`` is the per-draw clock step (0.05h = 3 min, a typical cycle).
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
            perm, temp, temp_countdown=countdown, n=n
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


def report(result: dict, *, n: int, show_sequence: int) -> None:
    counts = result["counts"]
    temp_set = result["temp_set"]
    total = sum(counts.values()) or 1
    perm_ids = [i for i in counts if i not in temp_set]
    temp_ids = [i for i in counts if i in temp_set]

    print(f"\nDraws: {total}   knob n={n}   permanents={len(perm_ids)}   temporaries={len(temp_ids)}\n")

    if show_sequence and result["sequence"]:
        head = "".join(result["sequence"][:show_sequence])
        print(f"First {min(show_sequence, len(result['sequence']))} displays: {head}\n")

    if temp_ids:
        temp_total = sum(counts[i] for i in temp_ids)
        spacing = store.temp_slot_spacing(n, len(temp_ids))
        print("Temporary photos:")
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


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--perm", type=int, default=20, help="number of permanent photos")
    p.add_argument("--temp", type=int, default=1, help="number of temporary (expiring) photos")
    p.add_argument("--n", type=int, default=4, help="temp_min_spacing knob (>=2)")
    p.add_argument("--draws", type=int, default=600, help="number of displays to simulate")
    p.add_argument("--show-sequence", type=int, default=24, dest="show_sequence",
                   help="print the first N displays as a P/T string (0 to hide)")
    args = p.parse_args()

    n = max(2, args.n)
    perm_ids, temp_ids = build_ids(args.perm, args.temp)
    result = simulate(perm_ids, temp_ids, n=n, draws=args.draws)
    report(result, n=n, show_sequence=args.show_sequence)
    return 0


if __name__ == "__main__":
    sys.exit(main())
