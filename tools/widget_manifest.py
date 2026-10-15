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
        assets = [(widget["source"], APP, ".cpp")]
        portal = widget.get("portal")
        if portal is not None:
            if not isinstance(portal["label"], str) or not portal["label"]:
                raise ValueError(f"Missing widget label: {kind}")
            assets.extend((portal[key], APP / "web", suffix)
                          for key, suffix in (("html", ".html"), ("js", ".js"), ("css", ".css"))
                          if key in portal)
            if "html" not in portal or "js" not in portal:
                raise ValueError(f"Missing widget editor assets: {kind}")
        for asset, base, suffix in assets:
            resolved = (base / asset).resolve()
            if not resolved.is_relative_to(base) or resolved.suffix != suffix or not resolved.is_file():
                raise ValueError(f"Invalid or missing widget asset: {asset}")
    return widgets


def firmware_includes(widgets):
    return "\n".join(f"#if {' && '.join(widget['gates'])}\n"
                     f"#include \"{widget['source']}\"\n"
                     f"static_assert(sizeof({widget['type']}_widget_type) == sizeof(WidgetType), "
                     f"\"Manifest widget registration: {widget['type']}\");\n#endif"
                     for widget in widgets) + "\n"


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
    for line in path.read_text(encoding="utf-8").splitlines():
        lines.extend(portal_assets(widgets, extension) if line.strip() == "@widgets" else [line])
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--validate-editors", action="store_true")
    args = parser.parse_args()
    if args.validate_editors:
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