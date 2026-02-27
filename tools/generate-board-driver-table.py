#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional, Tuple


RE_DEFINE = re.compile(r"^\s*#define\s+(?P<key>[A-Z0-9_]+)\s+(?P<value>.+?)\s*(?://.*)?$")
RE_DEFINE_FLAG = re.compile(r"^\s*#define\s+(?P<key>[A-Z0-9_]+)\s*(?://.*)?$")

RE_SELECTOR_COND = re.compile(
    r"^\s*#(?P<kind>if|elif)\s+"
    r"(?P<selector>DISPLAY_DRIVER|TOUCH_DRIVER)\s*==\s*(?P<token>[A-Z0-9_]+)\s*(?://.*)?$"
)
RE_CPP_INCLUDE = re.compile(r"^\s*#include\s+\"(?P<path>drivers/[^\"]+\.cpp)\"\s*(?://.*)?$")


def _strip_prefix(value: str, prefix: str) -> str:
    value = value.strip()
    if value.startswith(prefix):
        return value[len(prefix):]
    return value


def _infer_panel_from_path(include_path: str) -> Optional[str]:
    """Try to infer a panel/controller name from a driver include path.

    Examples:
    - drivers/arduino_gfx_st77916_driver.cpp -> ST77916
    - drivers/st7703_dsi_driver.cpp -> ST7703
    """

    stem = Path(include_path).stem
    if stem.endswith("_driver"):
        stem = stem[: -len("_driver")]

    # Look for common chip-like tokens, preferring longer/more specific matches.
    # This is intentionally heuristic and should fall back to None.
    tokens = re.split(r"[_\-/]", stem)
    candidates: list[str] = []
    for t in tokens:
        t = t.strip()
        if not t:
            continue
        # chip-ish: letters+digits, optionally with v2/v3 suffix, etc.
        if re.fullmatch(r"[a-z]*\d{3,}[a-z0-9]*", t):
            candidates.append(t)
            continue
        if re.fullmatch(r"[a-z]{2,}\d{2,}[a-z0-9]*", t):
            candidates.append(t)

    if not candidates:
        return None

    # Prefer the last candidate (often the specific controller/panel).
    chip = candidates[-1]
    return chip.upper()


def _parse_driver_translation_unit(path: Path, selector: str) -> Dict[str, str]:
    """Parse display_drivers.cpp / touch_drivers.cpp to discover selector tokens.

    Returns mapping: SELECTOR_TOKEN -> included driver cpp path (drivers/..../*_driver.cpp)
    """

    mapping: Dict[str, str] = {}
    current_token: Optional[str] = None

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m_cond = RE_SELECTOR_COND.match(line)
        if m_cond and m_cond.group("selector") == selector:
            current_token = m_cond.group("token").strip()
            continue

        m_inc = RE_CPP_INCLUDE.match(line)
        if m_inc and current_token:
            inc_path = m_inc.group("path").strip()

            # Prefer the actual driver implementation include.
            # Some selectors may include additional helper/vendor .cpp files.
            is_driver_cpp = inc_path.endswith("_driver.cpp")

            if current_token not in mapping:
                if is_driver_cpp:
                    mapping[current_token] = inc_path
            else:
                # Upgrade if we previously captured a non-driver include.
                if is_driver_cpp and not mapping[current_token].endswith("_driver.cpp"):
                    mapping[current_token] = inc_path

    return mapping


@dataclass(frozen=True)
class BoardInfo:
    name: str
    display_backend: str
    display_panel: str
    display_bus: str
    resolution: str
    rotation: str
    touch_backend: str
    touch_hw: str
    notes: str


def _strip_parens(value: str) -> str:
    value = value.strip()
    if value.startswith("(") and value.endswith(")"):
        return value[1:-1].strip()
    return value


def _read_defines(path: Path) -> Dict[str, str]:
    defines: Dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = RE_DEFINE.match(line)
        if m:
            key = m.group("key")
            value = m.group("value").strip()
            defines[key] = value
            continue

        m_flag = RE_DEFINE_FLAG.match(line)
        if m_flag:
            # Flag-only define (no explicit value) counts as enabled.
            defines[m_flag.group("key")] = "true"
    return defines


def _map_display_backend(token: str) -> str:
    token = token.strip()
    if not token or token == "?":
        return "?"
    return _strip_prefix(token, "DISPLAY_DRIVER_")


def _map_touch_backend(token: str) -> Tuple[str, str]:
    token = token.strip()
    if token in ("0", "false", "False"):
        return "none", "none"
    if not token or token == "?":
        return "?", "?"
    stripped = _strip_prefix(token, "TOUCH_DRIVER_")
    # Generic readability tweak: if token is something like CST816S_WIRE,
    # show the leading controller name while keeping the full token available.
    primary = stripped.split("_", 1)[0] if "_" in stripped else stripped
    return primary, primary


def _detect_bus(defines: Dict[str, str]) -> str:
    if "LCD_QSPI_CS" in defines or "LCD_QSPI_PCLK" in defines:
        return "QSPI"
    # ST7703 DSI uses MIPI-DSI, not SPI
    driver = defines.get("DISPLAY_DRIVER", "")
    if "ST7703_DSI" in driver or "ST7701_DSI" in driver:
        return "DSI"
    if any(k.startswith("TFT_") for k in defines.keys()) or "LCD_SCK_PIN" in defines:
        return "SPI"
    return "?"


def _detect_panel(
    defines: Dict[str, str],
    display_backend_token: str,
    display_driver_cpp_by_token: Dict[str, str],
    touch_backend_token: str,
) -> str:
    # If the board explicitly declares a panel/controller hint, prefer it.
    if "DISPLAY_PANEL" in defines:
        return _strip_parens(defines.get("DISPLAY_PANEL", "?")).strip("\"")

    # TFT_eSPI boards often declare a controller selection macro.
    if "DISPLAY_DRIVER_ILI9341_2" in defines:
        return "ILI9341"

    # If we can infer from the selected driver file, do so.
    cpp_path = display_driver_cpp_by_token.get(display_backend_token)
    if cpp_path:
        inferred = _infer_panel_from_path(cpp_path)
        if inferred and inferred not in ("TFT", "SPI", "ESP", "PANEL", "ARDUINO", "GFX"):
            # For general backends (tft_espi, arduino_gfx) this may be None.
            return inferred

    # Heuristic: if Arduino_GFX is used with AXS15231B touch, the common pairing is AXS15231B.
    if display_backend_token == "DISPLAY_DRIVER_ARDUINO_GFX" and touch_backend_token.startswith("TOUCH_DRIVER_AXS15231B"):
        return "AXS15231B"

    return "?"


def _build_notes(defines: Dict[str, str]) -> str:
    notes = []
    if defines.get("DISPLAY_INVERSION_ON") == "true":
        notes.append("inversion on")
    if defines.get("DISPLAY_INVERSION_OFF") == "true":
        notes.append("inversion off")
    if defines.get("DISPLAY_NEEDS_GAMMA_FIX") == "true":
        notes.append("gamma fix")
    return ", ".join(notes)


def parse_board_overrides(
    board_name: str,
    overrides_path: Path,
    display_driver_cpp_by_token: Dict[str, str],
    touch_driver_cpp_by_token: Dict[str, str],
) -> BoardInfo:
    defines = _read_defines(overrides_path)

    display_backend_token = defines.get("DISPLAY_DRIVER", "?")
    display_backend = _map_display_backend(display_backend_token)

    touch_enabled = defines.get("HAS_TOUCH")
    if touch_enabled in ("false", "False", "0"):
        touch_backend, touch_hw = "none", "none"
    else:
        touch_token = defines.get("TOUCH_DRIVER", "?")
        touch_backend, touch_hw = _map_touch_backend(touch_token)

    width = _strip_parens(defines.get("DISPLAY_WIDTH", "?"))
    height = _strip_parens(defines.get("DISPLAY_HEIGHT", "?"))
    rotation = _strip_parens(defines.get("DISPLAY_ROTATION", "?"))

    display_bus = _detect_bus(defines)
    display_panel = _detect_panel(
        defines,
        display_backend_token,
        display_driver_cpp_by_token,
        defines.get("TOUCH_DRIVER", "?"),
    )

    notes = _build_notes(defines)

    return BoardInfo(
        name=board_name,
        display_backend=display_backend,
        display_panel=display_panel,
        display_bus=display_bus,
        resolution=f"{width}×{height}",
        rotation=str(rotation),
        touch_backend=touch_backend,
        touch_hw=touch_hw,
        notes=notes,
    )


def generate_table(board_infos: list[BoardInfo]) -> str:
    header = "| Board | Display backend | Panel | Bus | Res | Rot | Touch backend | Notes |\n"
    sep = "|---|---|---|---:|---:|---:|---|---|\n"
    rows = []
    for b in sorted(board_infos, key=lambda x: x.name):
        notes = b.notes if b.notes else ""
        rows.append(
            f"| {b.name} | {b.display_backend} | {b.display_panel} | {b.display_bus} | {b.resolution} | {b.rotation} | {b.touch_backend} | {notes} |"
        )
    return header + sep + "\n".join(rows) + "\n"


def update_markdown_file(path: Path, table_md: str) -> None:
    start = "<!-- BOARD_DRIVER_TABLE_START -->"
    end = "<!-- BOARD_DRIVER_TABLE_END -->"
    text = path.read_text(encoding="utf-8", errors="replace")

    if start not in text or end not in text:
        raise SystemExit(
            f"{path} is missing BOARD_DRIVER_TABLE markers. "
            "Add them or run without an update flag."
        )

    before, rest = text.split(start, 1)
    _, after = rest.split(end, 1)

    replacement = (
        start
        + "\n\n"
        + table_md.strip()
        + "\n\n"
        + end
    )

    path.write_text(before + replacement + after, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Board → Drivers → HW markdown table")
    parser.add_argument(
        "--repo-root",
        default=None,
        help="Repository root (defaults to auto-detect based on this script location)",
    )
    parser.add_argument(
        "--update-file",
        default=None,
        help="Update the table between markers in the given markdown file (path relative to repo root)",
    )
    parser.add_argument(
        "--update-drivers-readme",
        action="store_true",
        help="Update src/app/drivers/README.md between markers",
    )
    args = parser.parse_args()

    repo_root = Path(args.repo_root) if args.repo_root else Path(__file__).resolve().parents[1]
    boards_dir = repo_root / "src" / "boards"

    display_tu = repo_root / "src" / "app" / "display_drivers.cpp"
    touch_tu = repo_root / "src" / "app" / "touch_drivers.cpp"

    display_driver_cpp_by_token: Dict[str, str] = {}
    touch_driver_cpp_by_token: Dict[str, str] = {}

    if display_tu.exists():
        display_driver_cpp_by_token = _parse_driver_translation_unit(display_tu, "DISPLAY_DRIVER")
    if touch_tu.exists():
        touch_driver_cpp_by_token = _parse_driver_translation_unit(touch_tu, "TOUCH_DRIVER")

    if not boards_dir.is_dir():
        raise SystemExit(f"Boards directory not found: {boards_dir}")

    board_infos: list[BoardInfo] = []

    for board_dir in sorted(p for p in boards_dir.iterdir() if p.is_dir()):
        overrides_path = board_dir / "board_overrides.h"
        if not overrides_path.exists():
            continue
        board_infos.append(
            parse_board_overrides(
                board_dir.name,
                overrides_path,
                display_driver_cpp_by_token,
                touch_driver_cpp_by_token,
            )
        )

    table_md = generate_table(board_infos)

    if args.update_drivers_readme and args.update_file:
        raise SystemExit("Use only one of --update-drivers-readme or --update-file")

    if args.update_drivers_readme:
        update_markdown_file(repo_root / "src" / "app" / "drivers" / "README.md", table_md)

    if args.update_file:
        update_markdown_file(repo_root / args.update_file, table_md)

    print(table_md, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
