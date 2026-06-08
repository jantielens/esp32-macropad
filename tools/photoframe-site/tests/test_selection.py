#!/usr/bin/env python3
"""Unit tests for the pure two-bucket selection core in store.py.

Standalone (no pytest needed): run `python3 tests/test_selection.py`. Exercises
temp_slot_spacing, _lru_id, and bucket_schedule_pick -- the pure functions that
decide what /api/next serves -- without any Azure/blob I/O.
"""

from __future__ import annotations

import os
import sys
from datetime import datetime, timedelta, timezone

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import store  # noqa: E402

NOW = datetime(2026, 6, 8, 12, 0, 0, tzinfo=timezone.utc)


def _t(minutes: int) -> datetime:
    """A timestamp `minutes` after NOW (larger = more recently shown)."""
    return NOW + timedelta(minutes=minutes)


# --- temp_slot_spacing --------------------------------------------------------

def test_spacing_single_temp_matches_knob():
    # 1 temp photo, n=3 -> temp slot every 3 displays (T,P,P).
    assert store.temp_slot_spacing(3, 1) == 3
    assert store.temp_slot_spacing(4, 1) == 4
    assert store.temp_slot_spacing(8, 1) == 8


def test_spacing_multiple_temps_share_the_slot():
    # 2 temps at n=3 -> ceil(3/2)=2 -> T1,P,T2,P (every other display is a temp).
    assert store.temp_slot_spacing(3, 2) == 2
    # 4 temps at n=8 -> ceil(8/4)=2.
    assert store.temp_slot_spacing(8, 4) == 2


def test_spacing_floored_at_two():
    # Alternation caps the bucket at 50%: never less than 2 even for n<2 or big k.
    assert store.temp_slot_spacing(1, 1) == 2
    assert store.temp_slot_spacing(2, 1) == 2
    assert store.temp_slot_spacing(3, 10) == 2


# --- _lru_id ------------------------------------------------------------------

def test_lru_prefers_never_shown():
    items = [("a", _t(5)), ("b", None), ("c", _t(1))]
    assert store._lru_id(items) == "b"


def test_lru_picks_oldest_when_all_shown():
    items = [("a", _t(5)), ("b", _t(2)), ("c", _t(9))]
    assert store._lru_id(items) == "b"


def test_lru_empty_is_none():
    assert store._lru_id([]) is None


# --- bucket_schedule_pick: fallbacks ------------------------------------------

def test_pick_empty_both_returns_none():
    chosen, source, cd = store.bucket_schedule_pick([], [], temp_countdown=0, n=4)
    assert chosen is None and source is None and cd == 0


def test_pick_only_permanent_rotates_and_resets_countdown():
    perm = [("P0", _t(5)), ("P1", _t(1))]
    chosen, source, cd = store.bucket_schedule_pick(perm, [], temp_countdown=3, n=4)
    assert chosen == "P1" and source == "permanent"
    assert cd == 0  # reset so a freshly added temp is immediately due


def test_pick_only_temporary_rotates_without_separator():
    temp = [("T0", _t(5)), ("T1", None)]
    chosen, source, cd = store.bucket_schedule_pick([], temp, temp_countdown=2, n=4)
    assert chosen == "T1" and source == "temporary"
    assert cd == 2  # countdown untouched when there are no permanents


# --- bucket_schedule_pick: cadence --------------------------------------------

def test_pick_temp_when_due_sets_spacing_countdown():
    perm = [("P0", None)]
    temp = [("T0", None)]
    chosen, source, cd = store.bucket_schedule_pick(perm, temp, temp_countdown=0, n=3)
    assert chosen == "T0" and source == "temporary"
    assert cd == store.temp_slot_spacing(3, 1) - 1 == 2


def test_pick_permanent_while_counting_down():
    perm = [("P0", None)]
    temp = [("T0", _t(1))]
    chosen, source, cd = store.bucket_schedule_pick(perm, temp, temp_countdown=2, n=3)
    assert chosen == "P0" and source == "permanent"
    assert cd == 1


def _run_sequence(perm_ids, temp_ids, *, n, draws):
    """Drive the scheduler like the simulator does; return the P/T source string."""
    last_shown = {i: None for i in perm_ids + temp_ids}
    temp_set = set(temp_ids)
    countdown = 0
    clock = NOW
    out = []
    for _ in range(draws):
        perm = [(i, last_shown[i]) for i in perm_ids]
        temp = [(i, last_shown[i]) for i in temp_ids]
        chosen, _source, countdown = store.bucket_schedule_pick(
            perm, temp, temp_countdown=countdown, n=n
        )
        out.append((chosen, "T" if chosen in temp_set else "P"))
        last_shown[chosen] = clock
        clock = clock + timedelta(minutes=3)
    return out


def test_sequence_single_temp_is_t_then_n_minus_one_p():
    # n=3, 1 temp -> T,P,P,T,P,P...
    seq = _run_sequence(["P0", "P1", "P2", "P3"], ["T0"], n=3, draws=9)
    pattern = "".join(s for _id, s in seq)
    assert pattern == "TPPTPPTPP"


def test_sequence_two_temps_round_robin_the_slot():
    # n=3, 2 temps -> ceil(3/2)=2 -> T1,P,T2,P,T1,P,... alternating temp identity.
    seq = _run_sequence(["P0", "P1", "P2", "P3"], ["T0", "T1"], n=3, draws=8)
    pattern = "".join(s for _id, s in seq)
    assert pattern == "TPTPTPTP"
    temp_hits = [cid for cid, s in seq if s == "T"]
    # The two temporaries alternate fairly (LRU within the bucket).
    assert temp_hits == ["T0", "T1", "T0", "T1"]


def test_sequence_n_two_is_pure_alternation():
    # n=2, 1 temp -> spacing 2 -> T,P,T,P (50% cap, never two temps in a row).
    seq = _run_sequence(["P0", "P1"], ["T0"], n=2, draws=6)
    pattern = "".join(s for _id, s in seq)
    assert pattern == "TPTPTP"


def test_temp_share_is_pool_independent():
    # The defining property: a temp photo's share depends on n, not pool size.
    def temp_share(pool: int) -> float:
        perm_ids = [f"P{i}" for i in range(pool)]
        seq = _run_sequence(perm_ids, ["T0"], n=4, draws=pool * 8)
        hits = sum(1 for _id, s in seq if s == "T")
        return hits / len(seq)

    small = temp_share(20)
    large = temp_share(500)
    # Both ~1/4 = 25%, independent of the 25x pool difference.
    assert abs(small - 0.25) < 0.02
    assert abs(large - 0.25) < 0.02
    assert abs(small - large) < 0.02


def _collect_tests():
    return [(name, obj) for name, obj in sorted(globals().items())
            if name.startswith("test_") and callable(obj)]


def main() -> int:
    failures = 0
    for name, fn in _collect_tests():
        try:
            fn()
            print(f"ok   {name}")
        except AssertionError as exc:
            failures += 1
            print(f"FAIL {name}: {exc}")
        except Exception as exc:  # noqa: BLE001
            failures += 1
            print(f"ERROR {name}: {exc!r}")
    total = len(_collect_tests())
    print(f"\n{total - failures}/{total} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
