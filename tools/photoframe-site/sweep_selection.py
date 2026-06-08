#!/usr/bin/env python3
"""Sweep the two-bucket selection across permanent-pool sizes (20..1000).

The whole point of the bucket scheduler is that a temporary photo's on-screen
share depends only on the knob ``n`` and the number of temporary photos -- *not*
on how big the permanent pool is. This sweep proves that: it holds the temporary
count and ``n`` fixed while growing the permanent pool from 20 to 1000 and shows
the temporary per-photo share staying flat (a weight multiplier, by contrast,
would dilute as 1/pool).

Examples:

    python3 sweep_selection.py
    python3 sweep_selection.py --temp 2 --n 3 --pools 20 100 1000
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))

import store  # noqa: E402
from simulate_selection import build_ids, simulate  # noqa: E402

DEFAULT_POOLS = [20, 50, 100, 200, 500, 1000]


def run_pool(perm: int, temp: int, *, n: int, shows: int) -> dict:
    """Simulate one permanent-pool size; draws scale so coverage is comparable."""
    draws = max(perm, temp) * shows
    perm_ids, temp_ids = build_ids(perm, temp)
    result = simulate(perm_ids, temp_ids, n=n, draws=draws)
    counts = result["counts"]
    total = sum(counts.values()) or 1
    temp_set = result["temp_set"]

    temp_ids_only = [i for i in counts if i in temp_set]
    perm_ids_only = [i for i in counts if i not in temp_set]
    per_temp = (sum(counts[i] for i in temp_ids_only) / len(temp_ids_only) / total) if temp_ids_only else 0.0
    perm_shares = [counts[i] / total for i in perm_ids_only] or [0.0]

    return {
        "perm": perm,
        "draws": draws,
        "per_temp": per_temp,
        "perm_mean": sum(perm_shares) / len(perm_shares),
        "perm_lo": min(perm_shares),
        "perm_hi": max(perm_shares),
    }


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--pools", type=int, nargs="+", default=DEFAULT_POOLS,
                   help="permanent-pool sizes to sweep (default: 20 50 100 200 500 1000)")
    p.add_argument("--temp", type=int, default=1, help="number of temporary photos (fixed)")
    p.add_argument("--n", type=int, default=4, help="temp_min_spacing knob (>=2)")
    p.add_argument("--shows", type=int, default=8,
                   help="target shows per photo; draws = max(pool, temp) * shows (default 8)")
    args = p.parse_args()

    n = max(2, args.n)
    k = args.temp
    target = min(1.0 / n, 1.0 / (2 * k)) if k else 0.0
    spacing = store.temp_slot_spacing(n, k) if k else 0
    print(
        f"Sweep: temp photos={k}  knob n={n}  shows/photo={args.shows}\n"
        f"Temp slot every {spacing} displays -> target per-temp share "
        f"min(1/n, 1/2k) = {target*100:.1f}%  (should stay flat as pool grows)\n"
    )
    header = f"{'perm':>6} {'draws':>7} {'temp/photo':>11} {'perm_mean':>10} {'perm_spread':>20}"
    print(header)
    print("-" * len(header))

    for perm in sorted(args.pools):
        m = run_pool(perm, k, n=n, shows=args.shows)
        spread = f"[{m['perm_lo']*100:.3f}%..{m['perm_hi']*100:.3f}%]"
        print(
            f"{m['perm']:>6} {m['draws']:>7} {m['per_temp']*100:>10.2f}% "
            f"{m['perm_mean']*100:>9.3f}% {spread:>20}"
        )

    print(
        "\nReading the table:\n"
        "  temp/photo  = a temporary photo's display share -- should match the target\n"
        "                above at EVERY pool size (pool-independent: the key property).\n"
        "  perm_mean   = mean share of a permanent photo (tracks the remaining slots / pool).\n"
        "  perm_spread = min..max permanent share (tight = even rotation).\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
