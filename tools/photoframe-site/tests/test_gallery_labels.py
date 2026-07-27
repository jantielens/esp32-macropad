"""Gallery lifetime-label regression tests."""

import os
import sys
from datetime import datetime, timedelta, timezone

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from ui import _time_left_label


NOW = datetime(2026, 7, 27, 12, 0, tzinfo=timezone.utc)


def test_time_left_label_formats_days_hours_and_minutes():
    assert _time_left_label(NOW + timedelta(days=2, hours=3), now=NOW) == "2d 3h"
    assert _time_left_label(NOW + timedelta(hours=4, minutes=5), now=NOW) == "4h 5m"
    assert _time_left_label(NOW + timedelta(minutes=17), now=NOW) == "17m"


def test_time_left_label_is_empty_without_remaining_time():
    assert _time_left_label(None, now=NOW) == ""
    assert _time_left_label(NOW, now=NOW) == ""
    assert _time_left_label(NOW - timedelta(seconds=1), now=NOW) == ""
