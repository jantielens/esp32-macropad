#!/usr/bin/env python3
"""Read package fields from a native extension source descriptor."""

import re
import sys
from pathlib import Path


if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[1] != "--title"):
    raise SystemExit("usage: extension_package_name.py [--title] <extension-source.cpp>")

show_title = len(sys.argv) == 3
source = Path(sys.argv[-1])
text = source.read_text(encoding="ascii")
match = re.search(
    r'native_extension_descriptor\s*=\s*\{.*?NATIVE_EXTENSION_TARGET_ABI\s*,\s*'
    r'"([a-z0-9-]+)"\s*,\s*"([0-9]+\.[0-9]+\.[0-9]+)"\s*,\s*"([^"]+)"',
    text,
    re.DOTALL,
)
if not match:
    raise SystemExit(f"missing or unsupported native extension descriptor: {source}")

if show_title:
    print(match.group(3))
else:
    print(f"{match.group(1)}@{match.group(2)}.elf")
