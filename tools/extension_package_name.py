#!/usr/bin/env python3
"""Print <id>@<package-semver>.elf from a native extension source descriptor."""

import re
import sys
from pathlib import Path


if len(sys.argv) != 2:
    raise SystemExit("usage: extension_package_name.py <extension-source.cpp>")

source = Path(sys.argv[1])
text = source.read_text(encoding="ascii")
match = re.search(
    r'native_extension_descriptor\s*=\s*\{.*?NATIVE_EXTENSION_TARGET_ABI\s*,\s*'
    r'"([a-z0-9-]+)"\s*,\s*"([0-9]+\.[0-9]+\.[0-9]+)"',
    text,
    re.DOTALL,
)
if not match:
    raise SystemExit(f"missing or unsupported native extension descriptor: {source}")

print(f"{match.group(1)}@{match.group(2)}.elf")
