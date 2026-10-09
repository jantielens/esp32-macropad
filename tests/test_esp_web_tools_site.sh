#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
PROJECT_DIR="$PWD"

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

source config.sh

create_partitions_bin() {
    local destination="$1"
    python3 - "$destination" <<'PY'
import struct
import sys

entry = struct.pack(
    "<HBBII16sI",
    0x50AA, 0x00, 0x10, 0x10000, 0x100000, b"app0\0", 0,
)
with open(sys.argv[1], "wb") as output:
    output.write(entry)
    output.write(b"\xff" * 32)
PY
}

mkdir -p "$TMP_DIR/build/extensions"

for board_name in "${!FQBN_TARGETS[@]}"; do
    if [[ "$board_name" == *beta* ]] || [[ "$board_name" == *experimental* ]]; then
        continue
    fi

    board_dir="$TMP_DIR/build/$board_name"
    mkdir -p "$board_dir"
    : > "$board_dir/app.ino.bin"
    : > "$board_dir/app.ino.bootloader.bin"
    : > "$board_dir/boot_app0.bin"
    create_partitions_bin "$board_dir/app.ino.partitions.bin"
done

while IFS= read -r source; do
    package_name="$(python3 tools/extension_package_name.py "$source")"
    : > "$TMP_DIR/build/extensions/${package_name%.elf}-p4.ext"
    : > "$TMP_DIR/build/extensions/${package_name%.elf}-s3.ext"
    : > "$TMP_DIR/build/extensions/${package_name%.elf}-esp32.ext"
done < <(grep -rl --include='*.cpp' 'native_extension_descriptor' "$PROJECT_DIR/extensions"/*/ | sort)

GITHUB_SHA=smoketest \
RELEASE_TAG=v0.0.0 \
RELEASE_NOTES_PATH="$TMP_DIR/release-notes.md" \
BUILD_DIR="$TMP_DIR/build" \
./tools/build-esp-web-tools-site.sh "$TMP_DIR/site"

test -f "$TMP_DIR/site/manifests/esp32-p4-lcd4b-voice.json"
for page in index flash update extensions; do
    test -f "$TMP_DIR/site/$page.html"
done
! grep -q 'Open-source ESP32 firmware' "$TMP_DIR/site/index.html"
grep -q 'site-footer.*Release ' "$TMP_DIR/site/index.html"
grep -q 'General-purpose firmware</h2>' "$TMP_DIR/site/index.html"
grep -q 'Specialized firmware</h2>' "$TMP_DIR/site/index.html"
! grep -q 'Explore device classes</h2>' "$TMP_DIR/site/index.html"
python3 - "$TMP_DIR/site/index.html" <<'PY'
from html.parser import HTMLParser
from pathlib import Path
import sys


class GroupCards(HTMLParser):
    def __init__(self):
        super().__init__()
        self.group = None
        self.cards = {"general": [], "specialized": []}

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        classes = attrs.get("class", "").split()
        if tag == "section" and "firmware-group" in classes:
            self.group = next(name for name in self.cards if name in classes)
        if tag == "a" and "class-card" in classes:
            self.cards[self.group].append(attrs["href"])

    def handle_endtag(self, tag):
        if tag == "section":
            self.group = None


parser = GroupCards()
parser.feed(Path(sys.argv[1]).read_text())
assert parser.cards == {
    "general": ["./devices/macropad.html", "./devices/headless.html"],
    "specialized": [
        "./devices/epaper_frame.html",
        "./devices/coffee_scale.html",
        "./devices/darkroom_timer.html",
        "./devices/shutter_tester.html",
        "./devices/voice_assistant.html",
    ],
}, parser.cards
PY
jq -e 'all(.[]; [.summary, .intro] | all(.[]; test("[.!?] [A-Z].+[.!?]$")))' tools/esp-web-tools-site/device-classes.json >/dev/null
for device_class in macropad epaper_frame headless coffee_scale shutter_tester darkroom_timer voice_assistant; do
    test -f "$TMP_DIR/site/devices/$device_class.html"
    grep -q "./devices/$device_class.html" "$TMP_DIR/site/index.html"
    icon="$(jq -r --arg slug "$device_class" '.[$slug].icon' tools/esp-web-tools-site/device-classes.json)"
    test -n "$icon" && test "$icon" != null
    grep -q "class-icon material-symbols-rounded.*>$icon</span>" "$TMP_DIR/site/index.html"
    grep -q "class-icon material-symbols-rounded.*>$icon</span>" "$TMP_DIR/site/devices/$device_class.html"
done
! grep -q 'class-card-top">Device class' "$TMP_DIR/site/index.html"
! grep -q 'class-card-arrow' "$TMP_DIR/site/index.html"
grep -q 'location.replace(`./update.html' "$TMP_DIR/site/index.html"
grep -q 'ESP32-MP Voice Assistant' "$TMP_DIR/site/flash.html"
grep -q 'aria-label="USB HID: Acts as a USB keyboard and mouse for a connected computer."' "$TMP_DIR/site/flash.html"
grep -q 'data-board="inkplate6flick-frame".*data-class="epaper_frame"' "$TMP_DIR/site/flash.html"
grep -q 'data-board="inkplate6flick-interactive".*data-class="macropad"' "$TMP_DIR/site/flash.html"
grep -q 'data-board="reterminal-e1003-frame".*data-class="epaper_frame"' "$TMP_DIR/site/flash.html"
grep -q 'data-board="reterminal-e1003-interactive".*data-class="macropad"' "$TMP_DIR/site/flash.html"
grep -q 'ESP32-MP E-Paper Frame Inkplate6Flick Frame' "$TMP_DIR/site/devices/epaper_frame.html"
grep -q '../flash.html?board=inkplate6flick-interactive' "$TMP_DIR/site/devices/macropad.html"
grep -q '../flash.html?board=jc4880p433-hx711' "$TMP_DIR/site/devices/coffee_scale.html"
grep -q '../flash.html?board=jc4880p433-nau7802' "$TMP_DIR/site/devices/coffee_scale.html"
grep -q './macropad.html' "$TMP_DIR/site/devices/epaper_frame.html"
grep -q 'epaper_frame.html.*location.search + location.hash' "$TMP_DIR/site/devices/epaper.html"
grep -q "key: 'epaper_frame'" "$TMP_DIR/site/app.js"
grep -q 'Advanced downloads' "$TMP_DIR/site/flash.html"
grep -q './firmware/inkplate6flick-frame/app.bin' "$TMP_DIR/site/flash.html"
grep -q 'id="deviceBase"' "$TMP_DIR/site/update.html"
find "$TMP_DIR/site/extensions" -maxdepth 1 -name '*.ext' -print -quit | grep -q .
grep -q 'ESP32 download' "$TMP_DIR/site/extensions.html"
test -f "$TMP_DIR/site/extensions/hello-world@1.0.0-esp32.ext"
! grep -q 'board-footprint-link' "$TMP_DIR/site/flash.html"
! grep -q '{{FOOTPRINT_NAV}}' "$TMP_DIR/site/flash.html"
! grep -q 'firmware-footprint/' "$TMP_DIR/site/index.html"

printf '%s\n' '{"schema_version":1,"reports":[{"board":"esp32-4848S040"}]}' > "$TMP_DIR/footprint.json"
FOOTPRINT_DATASET="$TMP_DIR/footprint.json" BOARD_FILTER=esp32-4848S040 BUILD_DIR="$TMP_DIR/build" \
    ./tools/build-esp-web-tools-site.sh "$TMP_DIR/filtered-site"
test -f "$TMP_DIR/filtered-site/devices/coffee_scale.html"
grep -q 'No builds for this device class are included in this preview.' "$TMP_DIR/filtered-site/devices/coffee_scale.html"
grep -q 'Enabled features <a class="board-footprint-link" href="./firmware-footprint/?board=esp32-4848S040"' "$TMP_DIR/filtered-site/flash.html"
grep -q 'href="./firmware-footprint/">Footprint' "$TMP_DIR/filtered-site/index.html"
grep -q 'href="../firmware-footprint/">Footprint' "$TMP_DIR/filtered-site/devices/macropad.html"
test -f "$TMP_DIR/filtered-site/firmware-footprint/index.html"
jq -e '.installer_url == "../index.html"' "$TMP_DIR/filtered-site/firmware-footprint/reports.json" >/dev/null

echo "ESP Web Tools site smoke test passed"