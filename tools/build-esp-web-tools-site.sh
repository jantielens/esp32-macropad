#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

TEMPLATE_DIR="$REPO_ROOT/tools/esp-web-tools-site"

source "$REPO_ROOT/config.sh"

OUT_DIR="${1:-$REPO_ROOT/site}"

# Only deploy “latest” (site output is overwritten each deploy)
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/manifests" "$OUT_DIR/firmware" "$OUT_DIR/ota"

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

  if [[ "$board_id" == *"esp32s3"* ]]; then
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
    if [[ "$fqbn" == *"esp32s3"* ]]; then
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

  awk -v display_name="$PROJECT_DISPLAY_NAME" \
      -v site_version="$SITE_VERSION" \
      -v display_version="$DISPLAY_VERSION" \
      -v version_href="$VERSION_HREF" \
      -v changelog_href="$CHANGELOG_HREF" \
      -v frag="$board_fragment" \
      '
        {
          gsub(/{{PROJECT_DISPLAY_NAME}}/, display_name)
          gsub(/{{SITE_VERSION}}/, site_version)
          gsub(/{{DISPLAY_VERSION}}/, display_version)
          gsub(/{{VERSION_HREF}}/, version_href)
          gsub(/{{CHANGELOG_HREF}}/, changelog_href)
        }
        /{{BOARD_ENTRIES}}/ {
          while ((getline line < frag) > 0) print line
          close(frag)
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
  boards+=("$board_name")
done

# Sort for stable output
IFS=$'\n' boards=($(sort <<<"${boards[*]}"))
unset IFS

# Copy firmware + generate manifests
board_fragment_tmp="$(mktemp)"
trap 'rm -f "$board_fragment_tmp"' EXIT

for board_name in "${boards[@]}"; do
  fqbn="${FQBN_TARGETS[$board_name]}"
  chip_family="$(get_chip_family_for_fqbn "$fqbn")"
  bootloader_offset_dec="$(get_bootloader_offset_dec_for_chip_family "$chip_family")"

  src_dir="$REPO_ROOT/build/$board_name"

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

  manifest_path="$OUT_DIR/manifests/$board_name.json"

  cat > "$manifest_path" <<EOF
{
  "name": "${PROJECT_DISPLAY_NAME} (${board_name})",
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

  cat >> "$board_fragment_tmp" <<EOF
          <div class="board" data-board="${board_name}" data-chip="${chip_family}">
            <div>
              <div class="board-title">${board_name}</div>
              <div class="board-sub">Chip: <code>${chip_family}</code></div>
              <div class="board-links">
                <a href="./manifests/${board_name}.json">manifest</a>
                <a href="./firmware/${board_name}/app.bin">app</a>
                <a href="./ota/${board_name}.json">ota</a>
              </div>
            </div>
            <div>
              <esp-web-install-button manifest="./manifests/${board_name}.json"></esp-web-install-button>
            </div>
          </div>
EOF

done

# Copy static assets and render index.html from template
cp "$TEMPLATE_DIR/style.css" "$OUT_DIR/style.css"
cp "$TEMPLATE_DIR/app.js" "$OUT_DIR/app.js"
cp "$TEMPLATE_DIR/dashboard.html" "$OUT_DIR/dashboard.html"
cp "$TEMPLATE_DIR/dashboard.js" "$OUT_DIR/dashboard.js"
render_index "$TEMPLATE_DIR/index.template.html" "$OUT_DIR/index.html" "$board_fragment_tmp"

echo "Built ESP Web Tools site at: $OUT_DIR" >&2
echo "Manifests: $OUT_DIR/manifests" >&2
echo "Firmware:  $OUT_DIR/firmware" >&2
echo "OTA:       $OUT_DIR/ota" >&2
