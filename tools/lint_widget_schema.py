#!/usr/bin/env python3
"""Widget schema parity lint.

Every config knob advertised by a widget's MCP ``<prefix>_describe`` hook must
actually be read by that widget's parser. The describe hook hand-lists field
names as string literals (an accepted DRY trade-off); this lint is the cheap
insurance that an advertised-but-unparsed knob is caught at CI time instead of
silently misleading an LLM into emitting config the firmware ignores.

Rule (per widget .cpp under src/app/widgets/):
  * describe names = quoted strings after ``add("`` and ``o["name"] = "`` inside
    the file.
  * parse keys     = every ``btn["…"]`` occurrence in the file.
  * FAIL if any describe name is not a parse key (and not allow-listed).
  * WARN only for parse keys that are not described (incompleteness is info).

Shared button fields handled outside the widget parser are allow-listed so they
are never flagged.
"""

import re
import sys
from pathlib import Path

WIDGETS_DIR = Path(__file__).resolve().parent.parent / "src" / "app" / "widgets"

# Shared / non-widget-specific button fields consumed outside the widget parser
# (pad_config.cpp). Kept small and only for cross-cutting keys.
ALLOWLIST = {
    "label_top", "label_center", "label_bottom",
    "label_top_style", "label_center_style", "label_bottom_style",
    "bg_color", "fg_color", "border_color", "border_width", "corner_radius",
    "icon", "icon_id", "btn_state", "tap", "long_press",
    "widget_data_binding", "widget_data_binding_2",
    "widget_data_binding_3", "widget_data_binding_4",
}

# describe names: add("name", ...), addmax/addrange("name", ...), and
# o["name"] = "name"
RE_ADD = re.compile(r'add(?:max|range)?\(\s*"([^"]+)"')
RE_ONAME = re.compile(r'o\["name"\]\s*=\s*"([^"]+)"')
# parse keys: btn["key"] — the button config object. Widgets read button config
# via `btn[...]`; other `obj[...]` accesses (e.g. table row/column payloads) are
# not button config and are intentionally excluded.
RE_PARSE = re.compile(r'btn\["([a-z0-9_]+)"\]')


def scan_file(path: Path):
    text = path.read_text(encoding="utf-8")
    describe_names = set(RE_ADD.findall(text)) | set(RE_ONAME.findall(text))
    parse_keys = set(RE_PARSE.findall(text))
    return describe_names, parse_keys


def main() -> int:
    files = sorted(WIDGETS_DIR.glob("*.cpp"))
    if not files:
        print(f"lint_widget_schema: no widget files in {WIDGETS_DIR}", file=sys.stderr)
        return 1

    failures = []
    for path in files:
        describe_names, parse_keys = scan_file(path)
        if not describe_names:
            continue  # widget has no describe hook (or none yet)

        undescribed_unparsed = describe_names - parse_keys - ALLOWLIST
        if undescribed_unparsed:
            failures.append((path.name, sorted(undescribed_unparsed)))

        # Warn-only: parsed but not described (informational).
        only_parsed = parse_keys - describe_names - ALLOWLIST
        if only_parsed:
            print(f"  warn  {path.name}: parsed but not described: "
                  f"{', '.join(sorted(only_parsed))}")

    if failures:
        print("\nWidget schema parity FAILED — advertised knobs the parser never reads:")
        for name, fields in failures:
            print(f"  FAIL  {name}: described but no btn[\"…\"] parse key: "
                  f"{', '.join(fields)}")
        print("\nFix: remove the field from <prefix>_describe, or read it in parse() "
              "via btn[\"<field>\"].")
        return 1

    print("Widget schema parity OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
