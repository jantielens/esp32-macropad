#!/usr/bin/env python3
import re
from pathlib import Path


bridge_dir = Path("src/app/device_classes/epaper_ble_bridge")
source = "\n".join(path.read_text(encoding="utf-8") for path in bridge_dir.glob("*.cpp"))

for log_call in re.findall(r"LOG[EWIDV]\s*\([^;]*\);", source, re.DOTALL):
    lowered = log_call.lower()
    assert "api_key" not in lowered
    assert "auth_tag" not in lowered
    assert "derived_key" not in lowered
    assert " url" not in lowered

config_source = (bridge_dir / "epaper_ble_bridge_config.cpp").read_text(encoding="utf-8")
assert 'output["api_key_set"]' in config_source
assert 'output["api_key"]' not in config_source

print("e-paper BLE secret guards OK")