#!/usr/bin/env python3
"""Compatibility wrapper for generating E1003 Gray16 bar test images."""

from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PREP_TOOL = SCRIPT_DIR / "epaper-prep-gray16.py"


def _load_prep_tool():
    spec = importlib.util.spec_from_file_location("epaper_prep_gray16", PREP_TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {PREP_TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    if len(sys.argv) > 2:
        print(f"Usage: {os.path.basename(sys.argv[0])} [container-sas-url]", file=sys.stderr)
        return 2
    container_sas_url = sys.argv[1] if len(sys.argv) == 2 else None
    return _load_prep_tool().generate_bars(container_sas_url)


if __name__ == "__main__":
    sys.exit(main())
