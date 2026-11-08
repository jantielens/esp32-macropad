import argparse
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "src/app"
MANIFEST = APP / "widgets/manifest.json"


def load_widgets(path=MANIFEST):
    widgets = json.loads(Path(path).read_text(encoding="utf-8"))
    flags = set(re.findall(r"^#ifndef ((?:HAS|IS)_[A-Z0-9_]+)",
                           (APP / "board_config.h").read_text(), re.MULTILINE))
    types = set()
    sources = set()
    portal_asset_gates = {}
    if not isinstance(widgets, list):
        raise ValueError("Widget manifest must be a list")
    for widget in widgets:
        kind = widget["type"]
        if not re.fullmatch(r"[a-z][a-z0-9_]*", kind) or kind in types:
            raise ValueError(f"Invalid or duplicate widget type: {kind}")
        types.add(kind)
        if widget["source"] in sources:
            raise ValueError(f"Duplicate widget source: {widget['source']}")
        sources.add(widget["source"])
        gates = widget["gates"]
        if not isinstance(gates, list) or not gates or any(gate not in flags for gate in gates):
            raise ValueError(f"Invalid widget gates: {kind}")
        any_gates = widget.get("any_gates", [])
        if not isinstance(any_gates, list) or any(gate not in flags for gate in any_gates):
            raise ValueError(f"Invalid widget any_gates: {kind}")
        assets = [(widget["source"], APP, ".cpp")]
        portal = widget.get("portal")
        if portal is not None:
            portal_gates = portal.get("gates", [])
            if not isinstance(portal_gates, list) or any(gate not in gates for gate in portal_gates):
                raise ValueError(f"Portal gates must be widget gates: {kind}")
            if not isinstance(portal["label"], str) or not portal["label"]:
                raise ValueError(f"Missing widget label: {kind}")
            assets.extend((portal[key], APP / "web", suffix)
                          for key, suffix in (("html", ".html"), ("js", ".js"), ("css", ".css"))
                          if key in portal)
            if "html" not in portal or "js" not in portal:
                raise ValueError(f"Missing widget editor assets: {kind}")
            for key in ("html", "js", "css"):
                if key not in portal:
                    continue
                previous = portal_asset_gates.setdefault(portal[key], portal_gates)
                if previous != portal_gates:
                    raise ValueError(f"Shared portal asset has conflicting gates: {portal[key]}")
        for asset, base, suffix in assets:
            resolved = (base / asset).resolve()
            if not resolved.is_relative_to(base) or resolved.suffix != suffix or not resolved.is_file():
                raise ValueError(f"Invalid or missing widget asset: {asset}")
    return widgets


def firmware_includes(widgets):
    def condition(widget):
        gates = list(widget["gates"])
        if widget.get("any_gates"):
            gates.append(f"({' || '.join(widget['any_gates'])})")
        return " && ".join(gates)

    return "\n".join(f"#if {condition(widget)}\n"
                     f"#include \"{widget['source']}\"\n"
                     f"static_assert(sizeof({widget['type']}_widget_type) == sizeof(WidgetType), "
                     f"\"Manifest widget registration: {widget['type']}\");\n#endif"
                     for widget in widgets) + "\n"


def portal_widgets(widgets, enabled_flags=None):
    return [widget for widget in widgets if "portal" in widget and
            (enabled_flags is None or all(gate in enabled_flags for gate in widget["portal"].get("gates", [])))]


def portal_assets(widgets, extension):
    return list(dict.fromkeys(widget["portal"][extension] for widget in widgets
                              if extension in widget.get("portal", {})))


def validate_editors(widgets):
    script = """
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict');
const manifest = JSON.parse(fs.readFileSync(0, 'utf8'));
const registered = [];
const context = vm.createContext({padRegisterWidgetEditor: editor => registered.push(editor.type)});
for (const module of manifest.modules) vm.runInContext(fs.readFileSync(module, 'utf8'), context, {filename: module});
assert.deepEqual(registered.sort(), manifest.types.sort());
"""
    result = subprocess.run(["node", "-e", script], text=True, capture_output=True,
                            input=json.dumps({
                                "modules": [str(APP / "web" / asset) for asset in portal_assets(widgets, "js")],
                                "types": [widget["type"] for widget in widgets if "portal" in widget],
                            }))
    if result.returncode:
        raise ValueError("Widget editor registration mismatch:\n" + result.stderr)


def expand_bundle(path):
    path = Path(path)
    widgets = load_widgets()
    extension = "js" if path.name.endswith(".js.bundle") else "css"
    lines = []
    chunk_name, chunk_flags = None, []
    restore_chunk = False
    serial = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        marker = re.fullmatch(r"#\s*\[chunk:([a-z0-9_-]+)((?:\s+(?:HAS|IS)_[A-Z_0-9]+)*)\]\s*", line.strip())
        if marker:
            chunk_name, chunk_flags = marker[1], marker[2].split()
            restore_chunk = False
        if line.strip() == "@widgets":
            seen = set()
            active_flags = chunk_flags
            for widget in portal_widgets(widgets):
                portal = widget["portal"]
                asset = portal.get(extension)
                if not asset or asset in seen:
                    continue
                seen.add(asset)
                flags = list(dict.fromkeys(chunk_flags + portal.get("gates", [])))
                if flags != active_flags:
                    if chunk_name is None:
                        raise ValueError("Gated widget assets require a chunked bundle")
                    serial += 1
                    lines.append(f"# [chunk:{chunk_name}_widgets_{serial} {' '.join(flags)}]")
                    active_flags = flags
                lines.append(asset)
            restore_chunk = active_flags != chunk_flags
        else:
            if restore_chunk and line.strip() and not line.lstrip().startswith("#"):
                serial += 1
                suffix = " " + " ".join(chunk_flags) if chunk_flags else ""
                lines.append(f"# [chunk:{chunk_name}_widgets_{serial}{suffix}]")
                restore_chunk = False
            lines.append(line)
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--validate-editors", action="store_true")
    parser.add_argument("--portal-gates", action="store_true")
    args = parser.parse_args()
    if args.portal_gates:
        print("\n".join(dict.fromkeys(gate for widget in portal_widgets(load_widgets())
                                     for gate in widget["portal"].get("gates", []))))
    elif args.validate_editors:
        validate_editors(load_widgets())
    elif args.bundle:
        print(expand_bundle(args.bundle), end="")
    else:
        content = firmware_includes(load_widgets())
        if args.firmware:
            args.firmware.parent.mkdir(parents=True, exist_ok=True)
            args.firmware.write_text(content, encoding="utf-8")
        else:
            print(content, end="")


if __name__ == "__main__":
    main()