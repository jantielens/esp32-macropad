#!/usr/bin/env python3
"""Build portal assets for one compiler-resolved firmware configuration."""

import argparse
import gzip
import json
import re
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from _render_html_template import render
from widget_manifest import expand_bundle, load_widgets, validate_editors


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "src/app"
WEB = APP / "web"
CHUNK = re.compile(r"#\s*\[chunk:([a-z0-9_-]+)(?:\s+(.+?))?\]\s*")
BLOCK = re.compile(r"\s*// @portal-(if (.+)|endif)\s*")

# Standalone assets must have an explicit owner. Bundle members inherit their
# chunk's gate; widget members inherit their manifest's native gate by default.
OWNERS = {
    "1": """shell welcome wifi setup device_name timezone network factory_reset
        ota_update manual_upload version_info sensor_data thresholds portal portal_all""",
    "!IS_EPAPER_FRAME": "mode",
    "ALARM_ENABLED": "alarm_schedule alarm_behavior portal_alarms",
    "HAS_DISPLAY": """pad_editor swipe_actions boot_actions system_screens button_defaults
        dialog_appearance timers brightness rotation screen_preview recipes recipe_catalog""",
    "HAS_DISPLAY && !SCREENSAVER_BACKLIGHT_ONLY": "screensaver",
    "HAS_IMAGE_LIBRARY": "image_library",
    "HAS_MQTT": "mqtt",
    "HAS_MQTT || HAS_HA_LIVE || HAS_DISPLAY || HAS_BUTTON": "ha_discovery",
    "HAS_MQTT && (HAS_DISPLAY || HAS_BUTTON)": "mqtt_triggers",
    "HAS_BUTTON": "hw_buttons",
    "HAS_MCP": "mcp",
    "HAS_NATIVE_EXTENSIONS": "extensions",
    "HAS_CAMERA && (HAS_DISPLAY || HAS_BUTTON)": "camera_motion",
    "HAS_BLE_HID || HAS_USB_HID": "hid",
    "HAS_BLE || HAS_BLE_HID": "bluetooth portal_bluetooth",
    "HAS_AUDIO": "volume feedback",
    "HAS_CAMERA": "camera portal_camera",
    "HAS_CAMERA && HAS_STORAGE_BROWSER": "camera_snapshots portal_camera_snapshots",
    "HAS_SOUND_PLAYER": "sounds music",
    "HAS_VOICE_NOTES": "voice_notes voice_notes_settings portal_voice_notes",
    "HAS_AUDIO_INPUT": "microphone portal_microphone",
    "HAS_MICROPHONE_STREAMING": "portal_live_microphone",
    "HAS_TTF_FONTS": "fonts portal_fonts",
    "HAS_STORAGE_BROWSER": "storage",
    "HAS_REMOTE_LOG": "logs portal_logs",
    "HAS_EXTERNAL_DATA": "external_data portal_external_data",
    "HAS_EPAPER_PANEL": "epaper_status epaper_image epaper_overlay epaper_vcom epaper_init",
    "HAS_LVGL_EPAPER": "epaper_refresh portal_epaper_refresh",
    "HAS_AUTO_SHUTDOWN": "power_savings portal_power_savings",
    "IS_SHUTTER_TESTER": "shutter shutter_tests shutter_sessions shutter_session_actions",
    "IS_COFFEE_SCALE": "scale brews brew_templates",
    "IS_DARKROOM_TIMER": "darkroom prints",
    "IS_VOICE_ASSISTANT": "voice",
}
ASSET_GATES = {name: gate for gate, names in OWNERS.items() for name in names.split()}


def stem(path):
    name = path.name.removesuffix(".fragment.html") if path.name.endswith(".fragment.html") else path.stem
    return name.replace("-", "_").replace(".", "_")


def bundle_entries(path):
    gate = "1"
    for raw in expand_bundle(path).splitlines():
        marker = CHUNK.fullmatch(raw.strip())
        if marker:
            gate = marker[2] or "1"
        else:
            name = raw.split("#", 1)[0].strip()
            if name:
                yield name, gate


def source_blocks(text, enabled):
    stack = [True]
    output = []
    for line in text.splitlines(keepends=True):
        marker = BLOCK.fullmatch(line.rstrip("\n"))
        if marker:
            if marker[2]:
                stack.append(enabled(marker[2]) and stack[-1])
            elif len(stack) == 1:
                raise ValueError("Unmatched @portal-endif")
            else:
                stack.pop()
        elif line.lstrip().startswith("// @portal-"):
            raise ValueError("Invalid portal feature block: " + line.strip())
        elif stack[-1]:
            output.append(line)
    if len(stack) != 1:
        raise ValueError("Unclosed @portal-if")
    return "".join(output)


def compiler_from_properties(path):
    properties = dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)
    recipe = properties["recipe.cpp.o.pattern"]
    includes = " ".join(shlex.quote("-I" + properties[key])
                        for key in ("build.core.path", "build.variant.path") if key in properties)
    recipe = recipe.replace("{includes}", includes)
    recipe = recipe.replace('"{source_file}"', "-").replace('"{object_file}"', "portal-probe.o")
    if re.search(r"\{[^}]+\}", recipe):
        raise ValueError("Unresolved Arduino compiler recipe: " + recipe)
    arguments = shlex.split(recipe)
    result = []
    skip = False
    for argument in arguments:
        if skip:
            skip = False
            continue
        if argument == "-o":
            skip = True
        elif argument in ("-c", "-MMD", "-MD", "-"):
            continue
        elif argument == "@" + properties.get("build.opt.path", ""):
            # Match the ESP32 prebuild hooks, not a previous build's response file.
            build_opt = APP / "build_opt.h"
            if build_opt.exists():
                result.append("@" + str(build_opt))
        elif argument == "@" + properties.get("file_opts.path", ""):
            # ESP32's prebuild hook clears this per-core compilation marker.
            continue
        else:
            result.append(argument)
    return result


def resolve_conditions(command, expressions):
    expressions = sorted(set(expressions))
    for expression in expressions:
        if not re.fullmatch(r"[A-Z0-9_!&|() \t]+", expression):
            raise ValueError("Invalid portal gate: " + expression)
    probe = f'#include "{APP / "board_config.h"}"\n'
    for index, expression in enumerate(expressions):
        identifiers = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", expression))
        for identifier in sorted(identifiers):
            probe += f'#ifndef {identifier}\n#error "Unknown portal gate: {identifier}"\n#endif\n'
        probe += f"#if {expression}\nPORTAL_SELECTED_{index}\n#endif\n"
    result = subprocess.run([*command, "-E", "-P", "-x", "c++", "-"],
                            input=probe, text=True, capture_output=True, check=True, cwd=ROOT)
    selected = set(re.findall(r"^PORTAL_SELECTED_(\d+)$", result.stdout, re.MULTILINE))
    return {expression: str(index) in selected for index, expression in enumerate(expressions)}


def discover():
    directories = [WEB, *sorted((APP / "device_classes").glob("*/web"))]
    sources = {}
    for directory in directories:
        for path in sorted(directory.iterdir()):
            if path.suffix not in (".html", ".js", ".css"):
                continue
            if path.name in sources:
                raise ValueError("Duplicate portal asset basename: " + path.name)
            sources[path.name] = path
    bundles = {path.name: list(bundle_entries(path.with_suffix(path.suffix + ".bundle")))
               for path in sources.values() if path.with_suffix(path.suffix + ".bundle").exists()}
    members = {name for entries in bundles.values() for name, _ in entries}
    for name in members:
        if name not in sources:
            raise ValueError("Missing bundled portal asset: " + name)
    for name, path in sources.items():
        if not name.startswith("_") and (name not in members or name in bundles) and stem(path) not in ASSET_GATES:
            raise ValueError("Portal asset needs an explicit feature owner: " + name)
    expressions = set(ASSET_GATES.values())
    expressions.update(gate for entries in bundles.values() for _, gate in entries)
    expressions.update(("HAS_AUTO_SHUTDOWN", "HAS_DISPLAY", "HAS_MICROPHONE_STREAMING", "HAS_BLE_HID"))
    expressions.update(widget["portal"]["condition"] for widget in load_widgets())
    for path in sources.values():
        if path.suffix == ".js":
            source_blocks(path.read_text(), lambda expression: True)
        expressions.update(match[1] for match in re.findall(r"// @portal-(if ([^\n]+)|endif)", path.read_text())
                           if match[1])
    return sources, bundles, members, expressions


def minify(text, kind):
    if kind == "js":
        return subprocess.run([str(ROOT / "tools/node_modules/.bin/terser"),
                               "--compress", "--mangle", "--format", "comments=false"],
                              input=text, text=True, capture_output=True, check=True).stdout.strip()
    import csscompressor
    return csscompressor.compress(text)


def generate(command, output, project, display):
    build_id = str(time.time_ns())
    widgets = load_widgets()
    validate_editors(widgets)
    sources, bundles, members, expressions = discover()
    decisions = resolve_conditions(command, expressions)
    enabled = decisions.__getitem__
    selected_widgets = [widget for widget in widgets if enabled(widget["portal"]["condition"])]
    version = (ROOT / "src/version.h").read_text()
    version = ".".join(re.search(rf"#define VERSION_{part}\s+(\d+)", version)[1]
                       for part in ("MAJOR", "MINOR", "PATCH"))
    arrays = []
    fragments = []
    sizes = {}
    for name, path in sorted(sources.items()):
        if name.startswith("_") or (name in members and name not in bundles) or not enabled(ASSET_GATES[stem(path)]):
            continue
        if path.suffix == ".html":
            content = render(path, WEB, project, display, version,
                             has_ble_hid=enabled("HAS_BLE_HID"),
                             has_binding_help=enabled("HAS_DISPLAY"),
                             widget_types={widget["type"] for widget in selected_widgets},
                             has_auto_shutdown=enabled("HAS_AUTO_SHUTDOWN"),
                             has_microphone_streaming=enabled("HAS_MICROPHONE_STREAMING"),
                             asset_build_id=build_id)
            symbol = stem(path) + ("_fragment" if name.endswith(".fragment.html") else "") + "_html_gz"
            if name.endswith(".fragment.html"):
                fragments.append((name.removesuffix(".fragment.html"), symbol))
        else:
            entries = bundles.get(name, [(name, "1")])
            content = "\n".join(source_blocks(sources[member].read_text(), enabled)
                                for member, gate in entries if enabled(gate))
            if path.suffix == ".js":
                subprocess.run(["node", "--check"], input=content, text=True, capture_output=True, check=True)
            content = minify(content, path.suffix[1:])
            symbol = stem(path) + "_" + path.suffix[1:] + "_gz"
        compressed = gzip.compress(content.encode(), compresslevel=9, mtime=0)
        sizes[symbol] = len(compressed)
        data = ",\n".join("  " + ", ".join(f"0x{byte:02x}" for byte in compressed[index:index + 12])
                          for index in range(0, len(compressed), 12))
        arrays.append(f"const uint8_t {symbol}[] PROGMEM = {{\n{data}\n}};\n"
                      f"const size_t {symbol}_len = sizeof({symbol});\n")
    guard = "\n".join(f'#if {"!(" + expression + ")" if on else "(" + expression + ")"}\n'
                      '#error "Portal assets do not match firmware configuration; regenerate assets"\n#endif'
                      for expression, on in sorted(decisions.items()))
    header = ('// Generated by tools/minify-web-assets.sh; do not edit.\n'
              '#ifndef WEB_ASSETS_H\n#define WEB_ASSETS_H\n#include <Arduino.h>\n'
              '#include "board_config.h"\n#include <project_branding.h>\n' + guard + "\n"
              + "\n".join(arrays) + "\n"
              + "struct FragmentAsset { const char* id; const uint8_t* data; size_t len; };\n"
              + "static const FragmentAsset fragment_assets[] = {\n"
              + "\n".join(f'  {{"{name}", {symbol}, sizeof({symbol})}},' for name, symbol in fragments)
              + "\n};\nstatic constexpr size_t fragment_assets_count = "
              "sizeof(fragment_assets) / sizeof(fragment_assets[0]);\n"
              "static inline const FragmentAsset* find_fragment_asset(const char* id) {\n"
              "  for (size_t i = 0; i < fragment_assets_count; ++i)\n"
              "    if (strcmp(fragment_assets[i].id, id) == 0) return &fragment_assets[i];\n"
              "  return nullptr;\n}\n#endif\n")
    remote_result = subprocess.run(["git", "config", "--get", "remote.origin.url"], cwd=ROOT,
                                   text=True, capture_output=True)
    if remote_result.returncode not in (0, 1):
        remote_result.check_returncode()
    remote = remote_result.stdout.strip()
    repo = re.search(r"github\.com[:/]([^/]+)/([^/]+?)(?:\.git)?$", remote)
    owner, repository = repo.groups() if repo else ("", "")
    if not repo:
        print("Repo slug not detected; GitHub Pages links may be unavailable", file=sys.stderr)
    branding = ("#ifndef PROJECT_BRANDING_H\n#define PROJECT_BRANDING_H\n"
                "#ifdef PROJECT_NAME\n#undef PROJECT_NAME\n#endif\n"
                "#ifdef PROJECT_DISPLAY_NAME\n#undef PROJECT_DISPLAY_NAME\n#endif\n"
                f"#define PROJECT_NAME {json.dumps(project)}\n"
                f"#define PROJECT_DISPLAY_NAME {json.dumps(display)}\n#endif\n")
    slug = ("#ifndef REPO_SLUG_CONFIG_H\n#define REPO_SLUG_CONFIG_H\n"
            f"#define REPO_OWNER {json.dumps(owner)}\n"
            f"#define REPO_NAME {json.dumps(repository)}\n#endif\n")
    asset_version = ("#ifndef PORTAL_ASSET_VERSION_H\n#define PORTAL_ASSET_VERSION_H\n"
                     f"#define PORTAL_ASSET_BUILD_ID {json.dumps(build_id)}\n#endif\n")
    output.mkdir(parents=True, exist_ok=True)
    for name, text in (("web_assets.h", header), ("project_branding.h", branding),
                       ("repo_slug_config.h", slug), ("portal_asset_version.h", asset_version)):
        with tempfile.NamedTemporaryFile(mode="w", dir=output, delete=False) as target:
            target.write(text)
        Path(target.name).replace(output / name)
    (output / "portal-assets.json").write_text(json.dumps(
        {"build_id": build_id, "conditions": decisions, "sizes": sizes}, indent=2) + "\n")
    print(f"Portal assets: one bundle per configuration, {sum(sizes.values())} gzip bytes")
    for symbol in ("portal_js_gz", "portal_all_css_gz"):
        print(f"  {symbol}: {sizes[symbol]} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project")
    parser.add_argument("display")
    compiler = parser.add_mutually_exclusive_group(required=True)
    compiler.add_argument("--properties", type=Path)
    compiler.add_argument("--cpp-command", help="JSON compiler argv, for focused host checks")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    command = compiler_from_properties(args.properties) if args.properties else json.loads(args.cpp_command)
    generate(command, args.output_dir, args.project, args.display)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.stderr.write(error.stderr or str(error))
        sys.exit(error.returncode)
