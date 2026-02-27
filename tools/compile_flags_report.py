#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple


RE_MACRO_NAME = re.compile(r"\b[A-Z_][A-Z0-9_]*\b")


@dataclass(frozen=True)
class PreprocessorUsage:
    macro: str
    file: str  # repo-relative posix path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def rel_posix(path: Path, root: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def parse_boards_from_config_sh(config_sh: Path) -> List[str]:
    # Keys are used as board names.
    # Example: ["esp32-nodisplay"]="esp32:esp32:esp32"
    # Requirement: only include boards that are not commented out.
    boards: List[str] = []
    for raw_line in read_text(config_sh).splitlines():
        line = raw_line.lstrip()
        if not line or line.startswith("#"):
            continue
        match = re.search(r"\[\"(?P<name>[^\"]+)\"\]\s*=", line)
        if match:
            boards.append(match.group("name"))
    # Preserve order of appearance, remove duplicates.
    seen: Set[str] = set()
    ordered: List[str] = []
    for b in boards:
        if b not in seen:
            seen.add(b)
            ordered.append(b)
    return ordered


def parse_boards_from_config_files(config_sh: Path, config_project_sh: Optional[Path]) -> List[str]:
    boards = parse_boards_from_config_sh(config_sh)
    if config_project_sh and config_project_sh.exists():
        project_boards = parse_boards_from_config_sh(config_project_sh)
        if not project_boards:
            raise SystemExit(
                f"{config_project_sh} exists but no boards were found. "
                "Ensure it declares FQBN_TARGETS entries like [\"board\"]=\"fqbn\", "
                "or remove/rename the file."
            )
        return project_boards
    return boards


def strip_line_comment(value: str) -> str:
    # Remove // comments (naive but works for this repo’s header style)
    if "//" in value:
        return value.split("//", 1)[0].rstrip()
    return value.rstrip()


def detect_include_guard_macros(lines: List[str]) -> Set[str]:
    # Common pattern:
    #   #ifndef FOO_H
    #   #define FOO_H
    #
    guard: Set[str] = set()
    for i in range(min(10, len(lines) - 1)):
        m1 = re.match(r"^\s*#\s*ifndef\s+([A-Z_][A-Z0-9_]*)\s*$", lines[i])
        if not m1:
            continue
        m2 = re.match(r"^\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\b", lines[i + 1])
        if m2 and m2.group(1) == m1.group(1):
            guard.add(m1.group(1))
            break
    return guard


def parse_all_defines(header: Path) -> Dict[str, str]:
    lines = read_text(header).splitlines()
    include_guards = detect_include_guard_macros(lines)

    defines: Dict[str, str] = {}
    for line in lines:
        m = re.match(r"^\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\b(.*)$", line)
        if not m:
            continue
        name = m.group(1)
        if name in include_guards:
            continue
        value = strip_line_comment(m.group(2)).strip()
        if value == "":
            value = "1"
        defines[name] = value
    return defines


def parse_board_config_defaults(board_config_h: Path) -> Tuple[Dict[str, str], Dict[str, str]]:
    """Return (defaults, unconditional_defines) from board_config.h.

    Defaults are extracted from patterns:
      #ifndef NAME
      #define NAME <value>
      #endif

    Unconditional defines include macro constants such as DISPLAY_DRIVER_TFT_ESPI.
    """

    lines = read_text(board_config_h).splitlines()
    include_guards = detect_include_guard_macros(lines)

    defaults: Dict[str, str] = {}
    unconditional: Dict[str, str] = {}

    stack: List[Tuple[str, Optional[str]]] = []  # (kind, guarded_name)

    for line in lines:
        if re.match(r"^\s*#\s*endif\b", line):
            if stack:
                stack.pop()
            continue

        m_ifndef = re.match(r"^\s*#\s*ifndef\s+([A-Z_][A-Z0-9_]*)\s*$", line)
        if m_ifndef:
            stack.append(("ifndef", m_ifndef.group(1)))
            continue

        # Other conditionals just affect nesting depth.
        if re.match(r"^\s*#\s*(if|ifdef|elif)\b", line):
            stack.append(("if", None))
            continue

        m_def = re.match(r"^\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\b(.*)$", line)
        if not m_def:
            continue

        name = m_def.group(1)
        if name in include_guards:
            continue

        value = strip_line_comment(m_def.group(2)).strip()
        if value == "":
            value = "1"

        if stack and stack[-1][0] == "ifndef" and stack[-1][1] == name:
            defaults[name] = value
        else:
            unconditional[name] = value

    return defaults, unconditional


def parse_board_config_descriptions(board_config_h: Path) -> Dict[str, str]:
    """Extract short human-readable descriptions for macros from board_config.h.

    Heuristic: a contiguous comment block immediately preceding a '#ifndef MACRO'
    is treated as the macro's description.
    """

    lines = read_text(board_config_h).splitlines()
    include_guards = detect_include_guard_macros(lines)

    descriptions: Dict[str, str] = {}

    pending: List[str] = []
    in_block_comment = False

    def flush_pending() -> None:
        nonlocal pending
        pending = []

    def normalize_comment_lines(text_lines: List[str]) -> List[str]:
        cleaned: List[str] = []
        for t in text_lines:
            t = re.sub(r"\s+", " ", t.strip())
            if not t:
                continue
            # Drop visual separators like '=====' or '-----'
            if re.fullmatch(r"[=\-_*]{3,}", t):
                continue
            cleaned.append(t)

        return cleaned

    def pick_description(text_lines: List[str]) -> str:
        cleaned = normalize_comment_lines(text_lines)
        if not cleaned:
            return ""

        # Use only the last meaningful line to avoid pulling in section headers.
        s = cleaned[-1].strip()
        return (s[:157].rstrip() + "...") if len(s) > 160 else s

    for raw in lines:
        line = raw.rstrip("\n")

        # Handle block comments /* ... */
        if in_block_comment:
            end_idx = line.find("*/")
            if end_idx != -1:
                content = line[:end_idx]
                content = content.lstrip(" *\t")
                if content.strip():
                    pending.append(content)
                in_block_comment = False
                # Anything after */ is treated as non-comment
                tail = line[end_idx + 2 :].strip()
                if tail:
                    flush_pending()
                continue
            else:
                content = line.lstrip(" *\t")
                if content.strip():
                    pending.append(content)
                continue

        stripped = line.lstrip()

        if stripped.startswith("/*"):
            in_block_comment = True
            after = stripped[2:]
            # Inline /* ... */ on one line
            end_idx = after.find("*/")
            if end_idx != -1:
                content = after[:end_idx].lstrip(" *\t")
                if content.strip():
                    pending.append(content)
                in_block_comment = False
            else:
                content = after.lstrip(" *\t")
                if content.strip():
                    pending.append(content)
            continue

        if stripped.startswith("//"):
            content = stripped[2:].strip()
            if content:
                pending.append(content)
            continue

        # Blank lines separate comment blocks, but keep them as part of the pending block
        # until we hit a real code line.
        if stripped == "":
            # keep pending
            continue

        m_ifndef = re.match(r"^\s*#\s*ifndef\s+([A-Z_][A-Z0-9_]*)\s*$", line)
        if m_ifndef:
            name = m_ifndef.group(1)
            if name not in include_guards:
                desc = pick_description(pending)
                if desc:
                    descriptions[name] = desc
            flush_pending()
            continue

        # Any other non-comment code line resets the pending block.
        flush_pending()

    return descriptions


def parse_define_descriptions(header: Path) -> Dict[str, str]:
    """Extract short descriptions for macros from a header that uses '#define NAME ...'.

    Heuristic: a contiguous comment block immediately preceding a '#define NAME'
    is treated as the macro's description.
    """

    lines = read_text(header).splitlines()
    include_guards = detect_include_guard_macros(lines)

    descriptions: Dict[str, str] = {}

    pending: List[str] = []
    in_block_comment = False

    def flush_pending() -> None:
        nonlocal pending
        pending = []

    def normalize_comment_lines(text_lines: List[str]) -> List[str]:
        cleaned: List[str] = []
        for t in text_lines:
            t = re.sub(r"\s+", " ", t.strip())
            if not t:
                continue
            if re.fullmatch(r"[=\-_*]{3,}", t):
                continue
            cleaned.append(t)
        return cleaned

    def pick_description(text_lines: List[str]) -> str:
        cleaned = normalize_comment_lines(text_lines)
        if not cleaned:
            return ""
        # Prefer last meaningful line to keep descriptions short.
        s = cleaned[-1].strip()
        return (s[:157].rstrip() + "...") if len(s) > 160 else s

    for raw in lines:
        line = raw.rstrip("\n")

        if in_block_comment:
            end_idx = line.find("*/")
            if end_idx != -1:
                content = line[:end_idx]
                content = content.lstrip(" *\t")
                if content.strip():
                    pending.append(content)
                in_block_comment = False
                tail = line[end_idx + 2 :].strip()
                if tail:
                    flush_pending()
                continue
            content = line.lstrip(" *\t")
            if content.strip():
                pending.append(content)
            continue

        stripped = line.lstrip()
        if stripped.startswith("/*"):
            in_block_comment = True
            after = stripped[2:]
            end_idx = after.find("*/")
            if end_idx != -1:
                content = after[:end_idx].lstrip(" *\t")
                if content.strip():
                    pending.append(content)
                in_block_comment = False
            else:
                content = after.lstrip(" *\t")
                if content.strip():
                    pending.append(content)
            continue

        if stripped.startswith("//"):
            content = stripped[2:].strip()
            if content:
                pending.append(content)
            continue

        if stripped == "":
            continue

        m_def = re.match(r"^\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\b", line)
        if m_def:
            name = m_def.group(1)
            if name not in include_guards:
                desc = pick_description(pending)
                if desc:
                    descriptions[name] = desc
            flush_pending()
            continue

        flush_pending()

    return descriptions


def _iter_git_tracked_files(root: Path, under: Path) -> Optional[List[Path]]:
    """Return a list of git-tracked files under 'under', or None if unavailable."""

    git_dir = root / ".git"
    if not git_dir.exists():
        return None

    under_rel = rel_posix(under, root)
    try:
        p = subprocess.run(
            ["git", "-C", str(root), "ls-files", "-z", "--", under_rel],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except OSError:
        return None

    if p.returncode != 0:
        return None

    out = p.stdout
    if not out:
        return []

    files: List[Path] = []
    for raw in out.split(b"\x00"):
        if not raw:
            continue
        rel = raw.decode("utf-8", errors="replace")
        files.append(root / rel)
    return files


def iter_source_files(src_root: Path, root: Path) -> Iterable[Path]:
    ex_dirs = {
        "vendor",  # driver-scoped vendored code
        "__pycache__",
    }

    # Prefer git-tracked files so local-only generated/ignored files don't affect output.
    candidates = _iter_git_tracked_files(root, src_root)
    if candidates is None:
        candidates = list(src_root.rglob("*"))

    for path in candidates:
        if not path.is_file():
            continue
        if any(part in ex_dirs for part in path.parts):
            continue
        if path.suffix.lower() not in {".h", ".hpp", ".c", ".cpp", ".ino"}:
            continue
        yield path


def scan_preprocessor_macro_usage(src_root: Path, root: Path) -> Tuple[Dict[str, Set[str]], Dict[str, Set[str]]]:
    """Return (macro->files, selector_lhs->possible_rhs_values)."""

    macro_to_files: Dict[str, Set[str]] = {}
    selector_values: Dict[str, Set[str]] = {}

    re_directive = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b(.*)$")

    for file_path in iter_source_files(src_root, root):
        rel = rel_posix(file_path, root)
        for line in read_text(file_path).splitlines():
            m = re_directive.match(line)
            if not m:
                continue
            kind = m.group(1)
            expr = m.group(2)

            if kind in {"ifdef", "ifndef"}:
                mname = re.search(r"\b([A-Z_][A-Z0-9_]*)\b", expr)
                if mname:
                    name = mname.group(1)
                    macro_to_files.setdefault(name, set()).add(rel)
                continue

            # #if / #elif: include defined(MACRO)
            for dm in re.finditer(r"defined\s*\(\s*([A-Z_][A-Z0-9_]*)\s*\)", expr):
                name = dm.group(1)
                macro_to_files.setdefault(name, set()).add(rel)
            for dm in re.finditer(r"defined\s+([A-Z_][A-Z0-9_]*)\b", expr):
                name = dm.group(1)
                macro_to_files.setdefault(name, set()).add(rel)

            # Selector patterns: FOO == BAR
            for sm in re.finditer(r"\b([A-Z_][A-Z0-9_]*)\b\s*==\s*\b([A-Z_][A-Z0-9_]*)\b", expr):
                lhs, rhs = sm.group(1), sm.group(2)
                selector_values.setdefault(lhs, set()).add(rhs)

            # Generic identifiers (we’ll filter later against known project macros)
            for ident in RE_MACRO_NAME.findall(expr):
                if ident == "defined":
                    continue
                macro_to_files.setdefault(ident, set()).add(rel)

    return macro_to_files, selector_values


def parse_bool(value: Optional[str]) -> Optional[bool]:
    if value is None:
        return None
    v = value.strip().lower()
    if v in {"true", "1"}:
        return True
    if v in {"false", "0"}:
        return False
    return None


def is_lvgl_config_macro(name: str, lv_conf_defines: Set[str]) -> bool:
    # Per requirement: exclude macros defined in lv_conf.h.
    return name in lv_conf_defines


def compute_effective_macros_for_board(
    defaults: Dict[str, str],
    board_overrides: Dict[str, str],
) -> Dict[str, str]:
    # Overrides win; then defaults; then other defines are ignored for effective.
    out = dict(defaults)
    out.update(board_overrides)
    return out


def format_md_table(headers: List[str], rows: List[List[str]]) -> str:
    if not headers:
        return ""
    out = []
    out.append("| " + " | ".join(headers) + " |")
    out.append("| " + " | ".join(["---"] * len(headers)) + " |")
    for r in rows:
        out.append("| " + " | ".join(r) + " |")
    return "\n".join(out) + "\n"


def replace_marked_section(document: str, marker: str, replacement: str) -> str:
    begin = f"<!-- BEGIN COMPILE_FLAG_REPORT:{marker} -->"
    end = f"<!-- END COMPILE_FLAG_REPORT:{marker} -->"

    if begin not in document or end not in document:
        raise SystemExit(
            f"Missing markers for section '{marker}'. Expected both {begin!r} and {end!r}."
        )

    pre, rest = document.split(begin, 1)
    _, post = rest.split(end, 1)

    if not replacement.endswith("\n"):
        replacement += "\n"

    return pre + begin + "\n" + replacement + end + post


def cmd_build(args: argparse.Namespace) -> None:
    root = repo_root()
    config_sh = root / "config.sh"
    config_project_sh = root / "config.project.sh"
    board = args.board

    boards = parse_boards_from_config_files(config_sh, config_project_sh)
    if board not in boards:
        raise SystemExit(
            f"Unknown board '{board}'. Expected one of: " + ", ".join(boards)
        )

    board_config_h = root / "src" / "app" / "board_config.h"
    lv_conf_h = root / "src" / "app" / "lv_conf.h"

    defaults, _unconditional = parse_board_config_defaults(board_config_h)
    lv_conf_defines = set(parse_all_defines(lv_conf_h).keys())

    overrides_path = root / "src" / "boards" / board / "board_overrides.h"
    overrides = parse_all_defines(overrides_path) if overrides_path.exists() else {}

    # Remove LVGL-config macros (defined in lv_conf.h) from override consideration.
    overrides = {k: v for (k, v) in overrides.items() if k not in lv_conf_defines}

    effective = compute_effective_macros_for_board(defaults, overrides)

    # Key selectors: show these for build logs, but mark as n/a when the feature is off.
    key_selectors = ["DISPLAY_DRIVER", "TOUCH_DRIVER"]

    active_features = sorted(
        [
            name
            for name, val in effective.items()
            if name.startswith("HAS_") and parse_bool(val) is True and name not in lv_conf_defines
        ]
    )

    has_display = parse_bool(effective.get("HAS_DISPLAY")) is True
    has_touch = parse_bool(effective.get("HAS_TOUCH")) is True

    print("Compile-time flags summary")
    print(f"Board: {board}")

    if active_features:
        print("Active features:")
        for name in active_features:
            print(f"  - {name}")
    else:
        print("Active features: (none)")

    print("Key selectors:")
    for sel in key_selectors:
        if sel in lv_conf_defines:
            continue
        if sel == "DISPLAY_DRIVER" and not has_display:
            print(f"  - {sel}: (n/a; HAS_DISPLAY=false)")
            continue
        if sel == "TOUCH_DRIVER" and not has_touch:
            print(f"  - {sel}: (n/a; HAS_TOUCH=false)")
            continue

        val = effective.get(sel)
        print(f"  - {sel}: {val if val is not None else '(undefined)'}")


def flag_group_by_type(name: str, selectors: Set[str]) -> str:
    if name.startswith("HAS_"):
        return "Features (HAS_*)"
    if name in selectors or name.endswith("_DRIVER"):
        return "Selectors (*_DRIVER)"

    # Treat common electrical IO / pin-like macros as "Pins" even if they don't end in _PIN.
    # Examples in this repo: TFT_MOSI, TFT_SCLK, TOUCH_I2C_SDA, LCD_QSPI_D0, XPT2046_IRQ, LCD_BL_PIN.
    pin_suffix_tokens = {
        "PIN",
        "GPIO",
        "SDA",
        "SCL",
        "MISO",
        "MOSI",
        "SCK",
        "SCLK",
        "CLK",
        "PCLK",
        "CS",
        "DC",
        "RST",
        "RESET",
        "IRQ",
        "INT",
        "BL",
        "TE",
        "D0",
        "D1",
        "D2",
        "D3",
    }
    last_token = name.rsplit("_", 1)[-1]
    is_indexed_pin_token = bool(re.match(r"^(SDA|SCL|D)\d+$", last_token))
    if name.endswith("_PIN") or name.endswith("_GPIO") or last_token in pin_suffix_tokens or is_indexed_pin_token:
        return "Hardware (Pins)"
    if name in {"DISPLAY_WIDTH", "DISPLAY_HEIGHT", "DISPLAY_ROTATION"}:
        return "Hardware (Geometry)"
    if (
        "BUFFER" in name
        or "TIMEOUT" in name
        or "PERIOD" in name
        or "FREQ" in name
        or name.endswith("_HZ")
        or "MAX_" in name
        or "MIN_" in name
        or "SIZE" in name
        or "HEADROOM" in name
        or "ATTEMPT" in name
    ):
        return "Limits & Tuning"
    return "Other"


def merge_descriptions_prefer_first(
    base: Dict[str, str],
    other: Dict[str, str],
) -> Dict[str, str]:
    out = dict(base)
    for k, v in other.items():
        if k not in out and v:
            out[k] = v
    return out


def cmd_md(args: argparse.Namespace) -> None:
    root = repo_root()

    config_sh = root / "config.sh"
    config_project_sh = root / "config.project.sh"
    board_config_h = root / "src" / "app" / "board_config.h"
    lv_conf_h = root / "src" / "app" / "lv_conf.h"
    src_root = root / "src"

    boards = parse_boards_from_config_files(config_sh, config_project_sh)
    defaults, _unconditional = parse_board_config_defaults(board_config_h)
    descriptions = parse_board_config_descriptions(board_config_h)

    lv_conf_defines = set(parse_all_defines(lv_conf_h).keys())

    # Gather per-board overrides (excluding lv_conf macros).
    board_overrides: Dict[str, Dict[str, str]] = {}
    for b in boards:
        path = root / "src" / "boards" / b / "board_overrides.h"
        ov = parse_all_defines(path) if path.exists() else {}
        ov = {k: v for (k, v) in ov.items() if not is_lvgl_config_macro(k, lv_conf_defines)}
        board_overrides[b] = ov

        # Pull per-define descriptions from board overrides too.
        if path.exists():
            override_desc = parse_define_descriptions(path)
            override_desc = {
                k: v for (k, v) in override_desc.items() if not is_lvgl_config_macro(k, lv_conf_defines)
            }
            descriptions = merge_descriptions_prefer_first(descriptions, override_desc)

    # Discover preprocessor usage across src/**
    macro_usage_all, selector_values_all = scan_preprocessor_macro_usage(src_root, root)

    # Project macros: defaults + any override defines across boards.
    project_macros: Set[str] = set(defaults.keys())
    for ov in board_overrides.values():
        project_macros.update(ov.keys())
    project_macros = {m for m in project_macros if not is_lvgl_config_macro(m, lv_conf_defines)}

    # Only report usage for macros we own.
    macro_to_files: Dict[str, Set[str]] = {
        m: files for (m, files) in macro_usage_all.items() if m in project_macros
    }

    # Selectors are LHS used in comparisons that we own.
    selectors = {
        lhs
        for lhs in selector_values_all.keys()
        if lhs in project_macros and not is_lvgl_config_macro(lhs, lv_conf_defines)
    }
    # Ensure key selectors are included.
    for key in ("DISPLAY_DRIVER", "TOUCH_DRIVER"):
        if key in project_macros:
            selectors.add(key)

    feature_flags = sorted([m for m in project_macros if m.startswith("HAS_")])

    selectors_sorted = sorted(selectors)

    other_macros = sorted([m for m in project_macros if not m.startswith("HAS_") and m not in selectors])

    flags_full = feature_flags + selectors_sorted + other_macros

    # Build matrices.
    feature_headers = ["board-name"] + feature_flags
    feature_rows: List[List[str]] = []

    selector_headers = ["board-name"] + selectors_sorted
    selector_rows: List[List[str]] = []

    for b in boards:
        eff = compute_effective_macros_for_board(defaults, board_overrides[b])

        has_display = parse_bool(eff.get("HAS_DISPLAY")) is True
        has_touch = parse_bool(eff.get("HAS_TOUCH")) is True

        row_f = [b]
        for f in feature_flags:
            v = parse_bool(eff.get(f))
            if v is True:
                row_f.append("✅")
            elif v is False:
                row_f.append("")
            else:
                row_f.append("?")
        feature_rows.append(row_f)

        row_s = [b]
        for s in selectors_sorted:
            if s == "DISPLAY_DRIVER" and not has_display:
                row_s.append("—")
                continue
            if s == "TOUCH_DRIVER" and not has_touch:
                row_s.append("—")
                continue
            row_s.append(eff.get(s, ""))
        selector_rows.append(row_s)

    # Full list section.
    # Requirement: keep this section compact; overrides/usage are covered elsewhere in the doc.
    lines: List[str] = []
    lines.append(f"Total flags: {len(flags_full)}")
    lines.append("")

    grouped: Dict[str, List[str]] = {}
    for name in flags_full:
        grouped.setdefault(flag_group_by_type(name, selectors), []).append(name)

    group_order = [
        "Features (HAS_*)",
        "Selectors (*_DRIVER)",
        "Hardware (Geometry)",
        "Hardware (Pins)",
        "Limits & Tuning",
        "Other",
    ]

    for group in group_order:
        names = grouped.get(group, [])
        if not names:
            continue
        lines.append(f"### {group}")
        lines.append("")
        for name in sorted(names):
            default = defaults.get(name)
            default_str = "(no default)" if default is None else default
            desc = descriptions.get(name)
            extra = ""
            if group == "Selectors (*_DRIVER)":
                values = sorted(selector_values_all.get(name, set()))
                if values:
                    extra = f" (values: {', '.join(values)})"
            if not desc:
                raise SystemExit(
                    f"Missing description for flag '{name}'. Add a short comment line immediately above its "
                    f"'#ifndef {name}' in src/app/board_config.h, or above its '#define {name}' in a board_overrides.h."
                )
            lines.append(f"- **{name}** default: `{default_str}`{extra} — {desc}")
        lines.append("")

    flags_md = "\n".join(lines).rstrip() + "\n"

    feature_md = format_md_table(feature_headers, feature_rows)
    selector_md = format_md_table(selector_headers, selector_rows)

    # Usage-only map (more compact than full list, but handy)
    usage_lines: List[str] = []
    for name in flags_full:
        used_in = sorted(macro_to_files.get(name, set()))
        if not used_in:
            continue
        usage_lines.append(f"- **{name}**")
        for fp in used_in:
            usage_lines.append(f"  - {fp}")
    usage_md = ("\n".join(usage_lines) + "\n") if usage_lines else "(no usage found)\n"

    out_path = (root / args.out).resolve() if not Path(args.out).is_absolute() else Path(args.out)
    doc = read_text(out_path)

    doc = replace_marked_section(doc, "FLAGS", flags_md)
    doc = replace_marked_section(doc, "MATRIX_FEATURES", feature_md)
    doc = replace_marked_section(doc, "MATRIX_SELECTORS", selector_md)
    doc = replace_marked_section(doc, "USAGE", usage_md)

    out_path.write_text(doc, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Discover project compile-time flags, where they are used, and report per-board effective settings.",
    )

    sub = parser.add_subparsers(dest="cmd", required=True)

    p_md = sub.add_parser("md", help="Update the docs markdown template sections")
    p_md.add_argument(
        "--out",
        required=True,
        help="Path to the markdown template (with BEGIN/END markers) to update, e.g. docs/compile-time-flags.md",
    )
    p_md.set_defaults(func=cmd_md)

    p_build = sub.add_parser("build", help="Print active flags for a board (for build logs)")
    p_build.add_argument("--board", required=True, help="Board name (must exist in config.sh FQBN_TARGETS)")
    p_build.set_defaults(func=cmd_build)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
