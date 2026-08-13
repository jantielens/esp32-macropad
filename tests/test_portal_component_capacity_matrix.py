#!/usr/bin/env python3
"""Ensure every configured board fits its active portal registrations."""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parent.parent
APP = ROOT / "src/app"
AGGREGATORS = (APP / "portal_components.cpp", APP / "route_components.cpp")
REGISTRY_HEADER = APP / "component_registry.h"
REGISTRATION = re.compile(r"\bREGISTER_(?:NAV_)?COMPONENT(?:\s*\([^;]+\))?\s*\(")
INCLUDE = re.compile(r'^\s*#include\s+"([^"]+\.cpp)"')
DIRECTIVE = re.compile(r"^\s*#(if|ifdef|ifndef|elif|else|endif)\b\s*(.*)$")
DEFINE = re.compile(r"^#define\s+(\w+)(?:\s+(.*))?$")


def portal_limit() -> int:
    match = re.search(r"^\s*#define\s+MAX_PORTAL_COMPONENTS\s+(\d+)",
                      REGISTRY_HEADER.read_text(), re.MULTILINE)
    if not match:
        raise RuntimeError("could not read MAX_PORTAL_COMPONENTS")
    return int(match.group(1))


def configured_boards() -> list[str]:
    config = (ROOT / "config.sh").read_text()
    block = re.search(r"declare -A FQBN_TARGETS=\((.*?)^\)", config,
                      re.MULTILINE | re.DOTALL)
    if not block:
        raise RuntimeError("could not read FQBN_TARGETS")
    return re.findall(r'\["([^"]+)"\]', block.group(1))


def board_macros(board: str) -> dict[str, str]:
    overrides = ROOT / "src/boards" / board
    source = '#include "board_config.h"\n'
    result = subprocess.run(
        ["g++", "-dM", "-E", "-x", "c++", "-DBOARD_HAS_OVERRIDE",
         "-DHAS_PSRAM=1", "-I", str(overrides), "-I", str(APP), "-"],
        input=source, text=True, capture_output=True,
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip())
    macros: dict[str, str] = {}
    for line in result.stdout.splitlines():
        match = DEFINE.match(line)
        if match:
            macros[match.group(1)] = match.group(2) or "1"
    # Defined in mqtt_triggers.h rather than board_config.h, but it gates a
    # component aggregated by portal_components.cpp.
    macros["MQTT_TRIGGERS_ENABLED"] = "(HAS_MQTT && (HAS_DISPLAY || HAS_BUTTON))"
    return macros


def expression_is_true(expression: str, macros: dict[str, str]) -> bool:
    value = re.sub(r"defined\s*\(\s*([A-Za-z_]\w*)\s*\)",
                   lambda match: "1" if match.group(1) in macros else "0",
                   expression)
    for _ in range(16):
        updated = re.sub(
            r"\b[A-Za-z_]\w*\b",
            lambda match: match.group(0) if match.group(0) in {"true", "false"}
            else macros.get(match.group(0), "0"), value)
        if updated == value:
            break
        value = updated
    value = value.replace("true", "1").replace("false", "0")
    value = value.replace("&&", " and ").replace("||", " or ")
    value = re.sub(r"!(?!=)", " not ", value)
    if not re.fullmatch(r"[\s0-9()andor!not]+", value):
        raise RuntimeError(f"unsupported preprocessor expression: {expression!r}")
    try:
        return bool(eval(value, {"__builtins__": {}}, {}))
    except (SyntaxError, TypeError) as error:
        raise RuntimeError(
            f"unsupported preprocessor expression {expression!r} expanded to {value!r}") from error


def active_lines(path: pathlib.Path, macros: dict[str, str]):
    stack: list[tuple[bool, bool]] = []  # parent active, a preceding branch matched
    active = True
    for line in path.read_text().splitlines():
        directive = DIRECTIVE.match(line)
        if not directive:
            if active:
                yield line
            continue
        kind, arg = directive.groups()
        if kind == "if":
            condition = expression_is_true(arg, macros)
            stack.append((active, condition))
            active = active and condition
        elif kind == "ifdef":
            condition = arg.strip() in macros
            stack.append((active, condition))
            active = active and condition
        elif kind == "ifndef":
            condition = arg.strip() not in macros
            stack.append((active, condition))
            active = active and condition
        elif kind == "elif":
            parent, matched = stack[-1]
            condition = not matched and expression_is_true(arg, macros)
            stack[-1] = (parent, matched or condition)
            active = parent and condition
        elif kind == "else":
            parent, matched = stack[-1]
            condition = not matched
            stack[-1] = (parent, True)
            active = parent and condition
        else:
            parent, _ = stack.pop()
            active = parent
    if stack:
        raise RuntimeError(f"unclosed conditional in {path}")


def registrations_in(path: pathlib.Path, macros: dict[str, str]) -> int:
    return sum(len(REGISTRATION.findall(line)) for line in active_lines(path, macros))


def board_component_count(board: str) -> int:
    macros = board_macros(board)
    count = 0
    for aggregator in AGGREGATORS:
        for line in active_lines(aggregator, macros):
            include = INCLUDE.match(line)
            if not include:
                continue
            component = (APP / include.group(1)).resolve()
            if not component.is_file():
                raise RuntimeError(f"missing included component: {component}")
            count += registrations_in(component, macros)
    return count


def main() -> int:
    limit = portal_limit()
    print(f"=== Portal component capacity matrix (limit: {limit}) ===")
    failed = False
    for board in configured_boards():
        try:
            count = board_component_count(board)
        except RuntimeError as error:
            print(f"FAIL: {board}: {error}", file=sys.stderr)
            failed = True
            continue
        if count > limit:
            print(f"FAIL: {board} registers {count} portal components (limit: {limit})",
                  file=sys.stderr)
            failed = True
        else:
            print(f"PASS: {board} registers {count}/{limit} portal components")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())