#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

TEMPLATE_DIR="$REPO_ROOT/tools/esp-web-tools-site"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"

source "$REPO_ROOT/config.sh"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERROR: jq is required for parsing src/boards/*/metadata.json but is not installed" >&2
  echo "Install: sudo apt-get install jq  (Debian/Ubuntu)" >&2
  echo "         brew install jq          (macOS)" >&2
  exit 1
fi

OUT_DIR="${1:-$REPO_ROOT/site}"
CLASS_CONTENT="$TEMPLATE_DIR/device-classes.json"
class_slugs=(macropad headless epaper_frame coffee_scale darkroom_timer shutter_tester voice_assistant)
footprint_nav=""
if [[ -n "${FOOTPRINT_DATASET:-}" ]]; then
  jq -e '.schema_version == 1 and (.reports | type == "array")' "$FOOTPRINT_DATASET" >/dev/null
  footprint_nav='<a href="./firmware-footprint/">Footprint</a>'
fi

# Only deploy “latest” (site output is overwritten each deploy)
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/manifests" "$OUT_DIR/firmware" "$OUT_DIR/ota" "$OUT_DIR/extensions" "$OUT_DIR/devices" "$OUT_DIR/assets"

# Prevent GitHub Pages from invoking Jekyll processing
: > "$OUT_DIR/.nojekyll"

if [[ ! -d "$TEMPLATE_DIR" ]]; then
  echo "ERROR: Missing template directory: $TEMPLATE_DIR" >&2
  exit 1
fi

PARTITIONS_OFFSET_DEC=32768  # 0x8000
BOOT_APP0_OFFSET_DEC=57344   # 0xE000

get_bootloader_offset_dec_for_chip_family() {
  local chip_family="$1"
  case "$chip_family" in
    "ESP32"|"ESP32-S2")
      echo 4096  # 0x1000
      ;;
    "ESP32-P4")
      echo 8192  # 0x2000
      ;;
    *)
      echo 0
      ;;
  esac
}

find_esp32_core_dir() {
  local esp32_hw_base="$HOME/.arduino15/packages/esp32/hardware/esp32"
  if [[ ! -d "$esp32_hw_base" ]]; then
    return 1
  fi

  local esp32_dir
  esp32_dir="$(ls -1d "$esp32_hw_base"/*/ 2>/dev/null | sort -V | tail -n 1 || true)"
  esp32_dir="${esp32_dir%/}"
  if [[ -z "$esp32_dir" || ! -d "$esp32_dir" ]]; then
    return 1
  fi
  echo "$esp32_dir"
}

find_boot_app0_bin() {
  local esp32_dir
  esp32_dir="$(find_esp32_core_dir || true)"
  if [[ -z "$esp32_dir" ]]; then
    return 1
  fi

  local candidate="$esp32_dir/tools/partitions/boot_app0.bin"
  if [[ -f "$candidate" ]]; then
    echo "$candidate"
    return 0
  fi

  return 1
}

# Minimal HTML escaping for values injected into board fragment markup
# (description, board_label). Covers the characters that would otherwise
# break attribute or text contexts: & < > " '
html_escape() {
  local s="$1"
  s="${s//&/\&amp;}"
  s="${s//</\&lt;}"
  s="${s//>/\&gt;}"
  s="${s//\"/\&quot;}"
  s="${s//\'/\&#39;}"
  printf '%s' "$s"
}

get_capability_label() {
  case "$1" in
    mcp) echo "MCP" ;;
    ble_hid) echo "BLE HID" ;;
    usb_hid) echo "USB HID" ;;
    bthome) echo "BTHome" ;;
    image_fetch) echo "Images" ;;
    camera) echo "Camera" ;;
    audio) echo "Audio" ;;
    microphone) echo "Microphone" ;;
    extensions) echo "Extensions" ;;
    sd_card) echo "SD card" ;;
    mqtt) echo "MQTT / HA" ;;
  esac
}

get_capability_tooltip() {
  case "$1" in
    mcp) echo "Includes a local MCP server that you can enable in the device portal for compatible AI assistants." ;;
    ble_hid) echo "Acts as a Bluetooth keyboard for a paired computer, tablet, or phone." ;;
    usb_hid) echo "Acts as a USB keyboard and mouse for a connected computer." ;;
    bthome) echo "Broadcasts sensor readings as Bluetooth Low Energy telemetry." ;;
    image_fetch) echo "Downloads and displays images from HTTP or HTTPS URLs." ;;
    camera) echo "Captures still images from the connected camera." ;;
    audio) echo "Plays tones, alerts, and audio through the board's speaker output." ;;
    microphone) echo "Captures microphone audio for voice features." ;;
    extensions) echo "Runs installable native visual extensions." ;;
    sd_card) echo "Stores files and media on a microSD card." ;;
    mqtt) echo "Connects to MQTT and Home Assistant for telemetry and control." ;;
  esac
}

get_app_offset_dec_from_partitions_bin() {
  local partitions_bin="$1"
  local parser="$REPO_ROOT/tools/parse_esp32_partitions.py"
  if [[ ! -f "$parser" ]]; then
    echo "ERROR: Missing partitions parser: $parser" >&2
    exit 1
  fi
  if [[ ! -f "$partitions_bin" ]]; then
    echo "ERROR: Missing partitions bin: $partitions_bin" >&2
    exit 1
  fi
  python3 "$parser" "$partitions_bin" --app-offset --format dec
}

get_version() {
  local major minor patch
  major=$(grep -E '^#define[[:space:]]+VERSION_MAJOR' "$REPO_ROOT/src/version.h" | grep -oE '[0-9]+' | head -1)
  minor=$(grep -E '^#define[[:space:]]+VERSION_MINOR' "$REPO_ROOT/src/version.h" | grep -oE '[0-9]+' | head -1)
  patch=$(grep -E '^#define[[:space:]]+VERSION_PATCH' "$REPO_ROOT/src/version.h" | grep -oE '[0-9]+' | head -1)
  echo "${major:-0}.${minor:-0}.${patch:-0}"
}

get_chip_family_for_fqbn() {
  local fqbn="$1"

  # Prefer the board id (3rd FQBN field) over matching the full string.
  # Example FQBNs:
  #   esp32:esp32:esp32
  #   esp32:esp32:esp32s3:FlashSize=16M,...
  #   esp32:esp32:nologo_esp32c3_super_mini:PartitionScheme=...,CDCOnBoot=...
  local board_id=""
  IFS=':' read -r _pkg _arch board_id _rest <<< "$fqbn"
  board_id="${board_id,,}"

  if [[ "$board_id" == *"esp32p4"* ]]; then
    echo "ESP32-P4"
  elif [[ "$board_id" == *"esp32s3"* ]]; then
    echo "ESP32-S3"
  elif [[ "$board_id" == *"esp32s2"* ]]; then
    echo "ESP32-S2"
  elif [[ "$board_id" == *"esp32c6"* ]]; then
    echo "ESP32-C6"
  elif [[ "$board_id" == *"esp32c3"* ]]; then
    echo "ESP32-C3"
  elif [[ "$board_id" == *"esp32c2"* ]]; then
    echo "ESP32-C2"
  elif [[ "$board_id" == *"esp32h2"* ]]; then
    echo "ESP32-H2"
  else
    # Fallback for odd FQBN formats (keep behavior compatible).
    if [[ "$fqbn" == *"esp32p4"* ]]; then
      echo "ESP32-P4"
    elif [[ "$fqbn" == *"esp32s3"* ]]; then
      echo "ESP32-S3"
    elif [[ "$fqbn" == *"esp32s2"* ]]; then
      echo "ESP32-S2"
    elif [[ "$fqbn" == *"esp32c6"* ]]; then
      echo "ESP32-C6"
    elif [[ "$fqbn" == *"esp32c3"* ]]; then
      echo "ESP32-C3"
    elif [[ "$fqbn" == *"esp32c2"* ]]; then
      echo "ESP32-C2"
    elif [[ "$fqbn" == *"esp32h2"* ]]; then
      echo "ESP32-H2"
      return
    fi
    echo "ESP32"
  fi
}

is_beta_board() {
  local board_name="$1"
  # Conservative filter: exclude anything explicitly tagged beta/experimental.
  shopt -s nocasematch
  if [[ "$board_name" == *"beta"* ]] || [[ "$board_name" == *"experimental"* ]]; then
    return 0
  fi
  return 1
}

VERSION="$(get_version)"
SHA_SHORT="${GITHUB_SHA:-local}"
SHA_SHORT="${SHA_SHORT:0:7}"
SITE_VERSION="$VERSION+$SHA_SHORT"
DISPLAY_VERSION="$VERSION"

# Link the displayed version to something useful.
# Preference order:
#  1) Release tag (when generating from a published release)
#  2) Commit SHA (when running in GitHub Actions)
#  3) Repo homepage

VERSION_HREF="#"
CHANGELOG_HREF="#"
if [[ -n "${GITHUB_SERVER_URL:-}" && -n "${GITHUB_REPOSITORY:-}" ]]; then
  VERSION_HREF="$GITHUB_SERVER_URL/$GITHUB_REPOSITORY"
  CHANGELOG_HREF="$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/blob/main/CHANGELOG.md"

  if [[ -n "${RELEASE_TAG:-}" ]]; then
    VERSION_HREF="$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/releases/tag/$RELEASE_TAG"
  elif [[ -n "${GITHUB_SHA:-}" ]]; then
    VERSION_HREF="$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/commit/$GITHUB_SHA"
  fi
fi

repo_owner=""
repo_name=""

if [[ -n "${GITHUB_REPOSITORY:-}" ]]; then
  repo_owner="${GITHUB_REPOSITORY%%/*}"
  repo_name="${GITHUB_REPOSITORY##*/}"
else
  origin_url=""
  if command -v git >/dev/null 2>&1; then
    origin_url=$(git -C "$REPO_ROOT" config --get remote.origin.url 2>/dev/null || true)
  fi
  if [[ -n "$origin_url" ]]; then
    if [[ "$origin_url" =~ github\.com[:/]+([^/]+)/([^/]+)$ ]]; then
      repo_owner="${BASH_REMATCH[1]}"
      repo_name="${BASH_REMATCH[2]}"
      repo_name="${repo_name%.git}"
    fi
  fi
fi

pages_base_url=""
if [[ -n "$repo_owner" && -n "$repo_name" ]]; then
  pages_base_url="https://${repo_owner}.github.io/${repo_name}"
fi

if [[ -n "${RELEASE_NOTES_PATH:-}" && -f "$RELEASE_NOTES_PATH" ]]; then
  cp "$RELEASE_NOTES_PATH" "$OUT_DIR/release-notes.md"
else
  # Provide a tiny placeholder so the UI can load something.
  echo "Release notes are available on GitHub." > "$OUT_DIR/release-notes.md"
fi

render_index() {
  local template_path="$1"
  local out_path="$2"
  local board_fragment="$3"
  local extension_fragment="$4"

  awk -v site_version="$SITE_VERSION" \
      -v display_version="$DISPLAY_VERSION" \
      -v version_href="$VERSION_HREF" \
      -v changelog_href="$CHANGELOG_HREF" \
      -v footprint_nav="$footprint_nav" \
      -v frag="$board_fragment" \
      -v extension_frag="$extension_fragment" \
      '
        {
          gsub(/{{SITE_VERSION}}/, site_version)
          gsub(/{{DISPLAY_VERSION}}/, display_version)
          gsub(/{{VERSION_HREF}}/, version_href)
          gsub(/{{CHANGELOG_HREF}}/, changelog_href)
          gsub(/{{FOOTPRINT_NAV}}/, footprint_nav)
        }
        /{{BOARD_ENTRIES}}/ {
          while ((getline line < frag) > 0) print line
          close(frag)
          next
        }
        /{{EXTENSION_ENTRIES}}/ {
          while ((getline line < extension_frag) > 0) print line
          close(extension_frag)
          next
        }
        { print }
      ' "$template_path" > "$out_path"
}

# Build list of boards for the index
boards=()
for board_name in "${!FQBN_TARGETS[@]}"; do
  if is_beta_board "$board_name"; then
    echo "Skipping beta board: $board_name" >&2
    continue
  fi
  if [[ -n "${BOARD_FILTER:-}" && ",${BOARD_FILTER}," != *",${board_name},"* ]]; then
    continue
  fi
  boards+=("$board_name")
done

if [[ ${#boards[@]} -eq 0 ]]; then
  echo "ERROR: BOARD_FILTER selected no configured boards" >&2
  exit 1
fi

# Sort for stable output
IFS=$'\n' boards=($(sort <<<"${boards[*]}"))
unset IFS

# Copy firmware + generate manifests
board_fragment_tmp="$(mktemp)"
extension_fragment_tmp="$(mktemp)"
class_fragment_dir="$(mktemp -d)"
trap 'rm -f "$board_fragment_tmp" "$extension_fragment_tmp"; rm -rf "$class_fragment_dir"' EXIT

for board_name in "${boards[@]}"; do
  fqbn="${FQBN_TARGETS[$board_name]}"
  chip_family="$(get_chip_family_for_fqbn "$fqbn")"
  bootloader_offset_dec="$(get_bootloader_offset_dec_for_chip_family "$chip_family")"

  src_dir="$BUILD_DIR/$board_name"

  bootloader_bin="$src_dir/app.ino.bootloader.bin"
  partitions_bin="$src_dir/app.ino.partitions.bin"
  app_bin="$src_dir/app.ino.bin"

  if [[ ! -f "$bootloader_bin" ]]; then
    echo "ERROR: Missing bootloader binary for $board_name at $bootloader_bin" >&2
    echo "Hint: run ./build.sh $board_name first" >&2
    exit 1
  fi
  if [[ ! -f "$partitions_bin" ]]; then
    echo "ERROR: Missing partitions binary for $board_name at $partitions_bin" >&2
    echo "Hint: run ./build.sh $board_name first" >&2
    exit 1
  fi
  if [[ ! -f "$app_bin" ]]; then
    echo "ERROR: Missing app binary for $board_name at $app_bin" >&2
    echo "Hint: run ./build.sh $board_name first" >&2
    exit 1
  fi

  boot_app0_bin="$src_dir/boot_app0.bin"
  if [[ ! -f "$boot_app0_bin" ]]; then
    boot_app0_bin="$(find_boot_app0_bin || true)"
  fi
  if [[ -z "$boot_app0_bin" || ! -f "$boot_app0_bin" ]]; then
    echo "ERROR: boot_app0.bin not found (need $src_dir/boot_app0.bin or an installed ESP32 core via ./setup.sh)" >&2
    exit 1
  fi

  app_offset_dec="$(get_app_offset_dec_from_partitions_bin "$partitions_bin")"
  if [[ -z "$app_offset_dec" ]]; then
    echo "ERROR: Failed to determine app offset for $board_name" >&2
    exit 1
  fi

  dst_dir="$OUT_DIR/firmware/$board_name"
  mkdir -p "$dst_dir"

  # Stable filenames; add cache-busting query param in manifest.
  cp "$bootloader_bin" "$dst_dir/bootloader.bin"
  cp "$partitions_bin" "$dst_dir/partitions.bin"
  cp "$boot_app0_bin" "$dst_dir/boot_app0.bin"
  cp "$app_bin" "$dst_dir/app.bin"

  # ----- Optional per-board metadata (src/boards/<board>/metadata.json) -----
  # All fields optional. Missing file / fields use sensible defaults.
  metadata_file="$REPO_ROOT/src/boards/$board_name/metadata.json"
  device_class="macropad"
  board_label="$board_name"
  description=""
  flash_mb=""
  psram_mb=""
  display_size=""
  capabilities=()

  if [[ -f "$metadata_file" ]]; then
    if ! jq empty "$metadata_file" 2>/dev/null; then
      echo "ERROR: Invalid JSON in $metadata_file" >&2
      exit 1
    fi
    device_class=$(jq -r '.device_class // "macropad"' "$metadata_file")
    board_label=$(jq -r '.board_label // ""' "$metadata_file")
    description=$(jq -r '.description // ""' "$metadata_file")
    flash_mb=$(jq -r '(.flash_mb // "") | tostring' "$metadata_file")
    psram_mb=$(jq -r '(.psram_mb // "") | tostring' "$metadata_file")
    display_size=$(jq -r '.display.size // ""' "$metadata_file")

    if ! jq -e '(.capabilities // []) | type == "array" and all(.[]; type == "string")' "$metadata_file" >/dev/null; then
      echo "ERROR: capabilities must be an array of strings in $metadata_file" >&2
      exit 1
    fi
    while IFS= read -r capability; do
      capabilities+=("$capability")
    done < <(jq -r '.capabilities[]?' "$metadata_file")

    case "$device_class" in
      macropad|epaper_frame|headless|shutter_tester|coffee_scale|darkroom_timer|voice_assistant) ;;
      *)
        echo "WARNING: Unknown device_class '$device_class' in $metadata_file, defaulting to 'macropad'" >&2
        device_class="macropad"
        ;;
    esac

    if [[ -z "$board_label" || "$board_label" == "null" ]]; then
      board_label="$board_name"
    fi
  else
    echo "INFO: No metadata file for $board_name, using defaults (device_class=macropad)" >&2
  fi

  brand_prefix="$(device_class_brand_prefix "$device_class")"
  if [[ -z "$brand_prefix" ]]; then
    board_display_name="$board_label"
  else
    board_display_name="${brand_prefix} ${board_label}"
  fi

  manifest_path="$OUT_DIR/manifests/$board_name.json"

  cat > "$manifest_path" <<EOF
{
  "name": "${board_display_name}",
  "version": "${SITE_VERSION}",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "${chip_family}",
      "parts": [
        { "path": "../firmware/${board_name}/bootloader.bin?v=${SHA_SHORT}", "offset": ${bootloader_offset_dec} },
        { "path": "../firmware/${board_name}/partitions.bin?v=${SHA_SHORT}", "offset": ${PARTITIONS_OFFSET_DEC} },
        { "path": "../firmware/${board_name}/boot_app0.bin?v=${SHA_SHORT}", "offset": ${BOOT_APP0_OFFSET_DEC} },
        { "path": "../firmware/${board_name}/app.bin?v=${SHA_SHORT}", "offset": ${app_offset_dec} }
      ]
    }
  ]
}
EOF

  ota_manifest_path="$OUT_DIR/ota/$board_name.json"
  app_size_bytes=$(stat -c%s "$app_bin")
  app_sha256=$(sha256sum "$app_bin" | awk '{print $1}')
  ota_url=""
  if [[ -n "$pages_base_url" ]]; then
    ota_url="$pages_base_url/firmware/${board_name}/app.bin"
  fi

  cat > "$ota_manifest_path" <<EOF
{
  "version": "${DISPLAY_VERSION}",
  "url": "${ota_url}",
  "sha256": "${app_sha256}",
  "size": ${app_size_bytes}
}
EOF

  # Build spec badges (only emit non-empty ones)
  badges_html="<div class=\"row-label hardware-label\">⚙️ Hardware</div><div class=\"pill-row\"><span class=\"badge\">Chip: <code>${chip_family}</code></span>"
  [[ -n "$flash_mb" && "$flash_mb" != "null" ]] && badges_html="${badges_html}<span class=\"badge\">${flash_mb} MB Flash</span>"
  [[ -n "$psram_mb" && "$psram_mb" != "null" && "$psram_mb" != "0" ]] && badges_html="${badges_html}<span class=\"badge\">${psram_mb} MB PSRAM</span>"
  [[ -n "$display_size" && "$display_size" != "null" ]] && badges_html="${badges_html}<span class=\"badge\">${display_size} Display</span>"
  badges_html="${badges_html}</div>"

  capability_badges_html=""
  for capability in "${capabilities[@]}"; do
    capability_label="$(get_capability_label "$capability")"
    capability_tooltip="$(get_capability_tooltip "$capability")"
    if [[ -z "$capability_label" || -z "$capability_tooltip" ]]; then
      echo "ERROR: Unknown capability '$capability' in $metadata_file" >&2
      exit 1
    fi
    capability_label_esc="$(html_escape "$capability_label")"
    capability_tooltip_esc="$(html_escape "$capability_tooltip")"
    capability_badges_html="${capability_badges_html}<span class=\"badge capability-badge\" tabindex=\"0\" aria-label=\"${capability_label_esc}: ${capability_tooltip_esc}\" data-tooltip=\"${capability_tooltip_esc}\">${capability_label_esc}</span>"
  done

  capabilities_html=""
  footprint_link=""
  if [[ -n "${FOOTPRINT_DATASET:-}" ]] && jq -e --arg board "$board_name" '.reports | any(.board == $board)' "$FOOTPRINT_DATASET" >/dev/null; then
    footprint_link="<a class=\"board-footprint-link\" href=\"./firmware-footprint/?board=${board_name}\">Firmware footprint</a>"
  fi
  if [[ -n "$capability_badges_html" ]]; then
    capabilities_html="<div class=\"board-capabilities\"><div class=\"capabilities-label\">✨ Enabled features ${footprint_link}</div><div class=\"pill-row\">${capability_badges_html}</div></div>"
  elif [[ -n "$footprint_link" ]]; then
    capabilities_html="<div class=\"board-capabilities\">${footprint_link}</div>"
  fi

  downloads_html="<details class=\"board-downloads\"><summary>Advanced downloads</summary><div class=\"pill-row\"><a class=\"badge download-badge\" href=\"./manifests/${board_name}.json\">Flash manifest</a><a class=\"badge download-badge\" href=\"./firmware/${board_name}/app.bin\">OTA firmware</a><a class=\"badge download-badge\" href=\"./ota/${board_name}.json\">OTA metadata</a></div></details>"

  desc_html=""
  if [[ -n "$description" && "$description" != "null" ]]; then
    desc_html="<div class=\"board-desc\">$(html_escape "$description")</div>"
  fi

  board_display_name_esc="$(html_escape "$board_display_name")"

  cat >> "$board_fragment_tmp" <<EOF
          <div class="board" data-board="${board_name}" data-chip="${chip_family}" data-class="${device_class}">
            <div class="board-content">
              <div class="board-title">${board_display_name_esc}</div>
              ${desc_html}
              <div class="board-specs">${badges_html}</div>
              ${capabilities_html}
              ${downloads_html}
            </div>
            <div class="board-install">
              <span class="install-label">🔌 Flash via USB</span>
              <esp-web-install-button manifest="./manifests/${board_name}.json"></esp-web-install-button>
            </div>
          </div>
EOF

  cat >> "$class_fragment_dir/$device_class" <<EOF
          <article class="device-board">
            <div><h3>${board_display_name_esc}</h3>${desc_html}</div>
            <div class="device-board-actions"><span>${chip_family}${display_size:+ · $(html_escape "$display_size")}</span><a class="action-link" href="../flash.html?board=${board_name}">Flash this board <span aria-hidden="true">&rarr;</span></a></div>
          </article>
EOF

done

while IFS= read -r source; do
  package_base="$(python3 "$REPO_ROOT/tools/extension_package_name.py" "$source")"
  package_base="${package_base%.elf}"
  title="$(python3 "$REPO_ROOT/tools/extension_package_name.py" --title "$source")"
  metadata_file="$(dirname "$source")/metadata.json"
  extension_dir="$(basename "$(dirname "$source")")"
  readme_file="$(dirname "$source")/README.md"

  if [[ ! -f "$metadata_file" ]]; then
    echo "ERROR: Missing extension catalog metadata: $metadata_file" >&2
    exit 1
  fi
  if ! jq -e 'type == "object" and ((keys | sort) == ["summary", "usage"]) and (.summary | type == "string" and length > 0) and (.usage | type == "string" and length > 0)' "$metadata_file" >/dev/null; then
    echo "ERROR: Invalid extension catalog metadata: $metadata_file" >&2
    exit 1
  fi
  summary="$(jq -r '.summary' "$metadata_file")"
  usage="$(jq -r '.usage' "$metadata_file")"
  extension_downloads_html=""
  extension_readme_html=""
  if [[ -n "$repo_owner" && -n "$repo_name" && -f "$readme_file" ]]; then
    extension_readme_html="<a class=\"extension-readme\" href=\"https://github.com/${repo_owner}/${repo_name}/blob/main/extensions/${extension_dir}/README.md\" target=\"_blank\" rel=\"noreferrer\">README on GitHub</a>"
  fi
  for target in p4 s3 esp32; do
    package_name="$package_base-$target.ext"
    package_file="$BUILD_DIR/extensions/$package_name"
    if [[ ! -f "$package_file" ]]; then
      echo "ERROR: Missing extension package: $package_file" >&2
      exit 1
    fi
    package_size="$(stat -c%s "$package_file")"
    cp "$package_file" "$OUT_DIR/extensions/$package_name"
    target_label="${target^^} download"
    extension_downloads_html="${extension_downloads_html}<a class=\"extension-download\" href=\"./extensions/$package_name\" download>${target_label}<span>$((package_size / 1024)) KiB</span></a>"
  done

  cat >> "$extension_fragment_tmp" <<EOF
          <article class="extension">
            <div class="extension-title">$(html_escape "$title")</div>
            <div class="extension-summary">$(html_escape "$summary")</div>
            ${extension_readme_html}
            <details class="extension-usage-details">
              <summary>Usage</summary>
              <div class="extension-usage">$(html_escape "$usage")</div>
            </details>
            <div class="extension-downloads">${extension_downloads_html}</div>
          </article>
EOF
done < <(grep -rl --include='*.cpp' 'native_extension_descriptor' "$REPO_ROOT/extensions"/*/ | sort)

# Copy static assets and render index.html from template
cp "$TEMPLATE_DIR/style.css" "$OUT_DIR/style.css"
cp "$TEMPLATE_DIR/app.js" "$OUT_DIR/app.js"
cp "$TEMPLATE_DIR/dashboard.html" "$OUT_DIR/dashboard.html"
cp "$TEMPLATE_DIR/dashboard.js" "$OUT_DIR/dashboard.js"
cp "$REPO_ROOT/assets/png/logo.png" "$OUT_DIR/assets/logo.png"
render_index "$TEMPLATE_DIR/flash.template.html" "$OUT_DIR/flash.html" "$board_fragment_tmp" "$extension_fragment_tmp"
render_index "$TEMPLATE_DIR/update.template.html" "$OUT_DIR/update.html" "$board_fragment_tmp" "$extension_fragment_tmp"
render_index "$TEMPLATE_DIR/extensions.template.html" "$OUT_DIR/extensions.html" "$board_fragment_tmp" "$extension_fragment_tmp"

class_cards="$(mktemp)"
trap 'rm -f "$board_fragment_tmp" "$extension_fragment_tmp" "$class_cards"; rm -rf "$class_fragment_dir"' EXIT
for class_slug in "${class_slugs[@]}"; do
  title="$(jq -r --arg slug "$class_slug" '.[$slug].title' "$CLASS_CONTENT")"
  summary="$(jq -r --arg slug "$class_slug" '.[$slug].summary' "$CLASS_CONTENT")"
  icon="$(jq -r --arg slug "$class_slug" '.[$slug].icon' "$CLASS_CONTENT")"
  class_intro="$(jq -r --arg slug "$class_slug" '.[$slug].intro' "$CLASS_CONTENT")"
  class_guidance="$(jq -r --arg slug "$class_slug" '.[$slug].guidance[]' "$CLASS_CONTENT" | while IFS= read -r item; do printf '<li>%s</li>\n' "$(html_escape "$item")"; done)"
  if [[ "$class_slug" == macropad ]]; then
    cat >> "$class_cards" <<'EOF'
        <section class="firmware-group general" aria-labelledby="general-heading">
          <div class="section-heading"><h2 id="general-heading">General-purpose firmware</h2><p>Build your own controls and automations.</p></div>
          <div class="class-grid">
EOF
  elif [[ "$class_slug" == epaper_frame ]]; then
    cat >> "$class_cards" <<'EOF'
          </div>
        </section>
        <section class="firmware-group specialized" aria-labelledby="specialized-heading">
          <div class="section-heading"><h2 id="specialized-heading">Specialized firmware</h2><p>Purpose-built tools for specific tasks.</p></div>
          <div class="class-grid">
EOF
  fi
  cat >> "$class_cards" <<EOF
        <a class="class-card" href="./devices/${class_slug}.html"><span class="class-icon material-symbols-rounded" aria-hidden="true">$(html_escape "$icon")</span><h3>$(html_escape "$title")</h3><p>$(html_escape "$summary")</p></a>
EOF
  class_related=""
  case "$class_slug" in
    macropad) class_related='<a class="related-class" href="./epaper_frame.html">Prefer scheduled images and long battery life? Explore E-Paper Frame &rarr;</a>' ;;
    epaper_frame) class_related='<a class="related-class" href="./macropad.html">Want an interactive e-paper pad? Explore Macropad &rarr;</a>' ;;
  esac
  if [[ ! -f "$class_fragment_dir/$class_slug" ]]; then
    echo '<p>No builds for this device class are included in this preview.</p>' > "$class_fragment_dir/$class_slug"
  fi
  device_footprint_nav="${footprint_nav/.\/firmware-footprint/..\/firmware-footprint}"
  awk -v footprint_nav="$device_footprint_nav" -v title="$(html_escape "$title")" -v intro="$(html_escape "$class_intro")" -v slug="$class_slug" \
    -v icon="$(html_escape "$icon")" -v related="$class_related" -v guidance="$class_guidance" -v boards="$class_fragment_dir/$class_slug" '
    { gsub(/{{CLASS_TITLE}}/, title); gsub(/{{CLASS_INTRO}}/, intro); gsub(/{{CLASS_SLUG}}/, slug); gsub(/{{CLASS_ICON}}/, icon) }
    { gsub(/{{FOOTPRINT_NAV}}/, footprint_nav) }
    /{{CLASS_RELATED}}/ { print related; next }
    /{{CLASS_GUIDANCE}}/ { print guidance; next }
    /{{CLASS_BOARDS}}/ { while ((getline line < boards) > 0) print line; close(boards); next }
    { print }
  ' "$TEMPLATE_DIR/device.template.html" > "$OUT_DIR/devices/$class_slug.html"
done
cat >> "$class_cards" <<'EOF'
          </div>
        </section>
EOF
cat > "$OUT_DIR/devices/epaper.html" <<'EOF'
<!doctype html>
<html lang="en"><head><meta charset="utf-8" /><meta http-equiv="refresh" content="0; url=./epaper_frame.html" /><title>E-Paper Frame | ESP32 Macropad</title><script>location.replace('./epaper_frame.html' + location.search + location.hash);</script></head><body><a href="./epaper_frame.html">E-Paper Frame</a></body></html>
EOF
render_index "$TEMPLATE_DIR/index.template.html" "$OUT_DIR/index.html" "$class_cards" "$extension_fragment_tmp"
if [[ -n "${FOOTPRINT_DATASET:-}" ]]; then
  python3 "$REPO_ROOT/tools/firmware_size_report.py" --dataset "$FOOTPRINT_DATASET" --site "$OUT_DIR/firmware-footprint"
fi

echo "Built ESP Web Tools site at: $OUT_DIR" >&2
echo "Manifests: $OUT_DIR/manifests" >&2
echo "Firmware:  $OUT_DIR/firmware" >&2
echo "OTA:       $OUT_DIR/ota" >&2
echo "Extensions: $OUT_DIR/extensions" >&2
