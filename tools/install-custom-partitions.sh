#!/usr/bin/env bash

# Install/register custom partition tables into the Arduino ESP32 core.
#
# This is intentionally a standalone script so:
# - Local dev can run: ./tools/install-custom-partitions.sh
# - setup.sh can call it automatically
# - CI workflows can call it before compiling boards that use PartitionScheme=...

set -euo pipefail

SCRIPT_DIR="$(CDPATH="" cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH="" cd "$SCRIPT_DIR/.." && pwd)"

# Base of all installed Arduino packages.
PACKAGES_BASE="$HOME/.arduino15/packages"

trim() {
  echo "$1" | xargs
}

# Resolve the on-disk install directory for the package:arch combo of an FQBN.
# Example: vendor=esp32, arch=esp32 → $HOME/.arduino15/packages/esp32/hardware/esp32/<version>/
#          vendor=Inkplate_Boards, arch=esp32 → .../Inkplate_Boards/hardware/esp32/<version>/
find_core_dir_for() {
  local vendor="$1"
  local arch="$2"

  local hw_base="$PACKAGES_BASE/$vendor/hardware/$arch"
  if [[ ! -d "$hw_base" ]]; then
    return 1
  fi

  # Pick the latest installed version directory under hw_base.
  local core_dir
  core_dir="$(ls -1d "$hw_base"/*/ 2>/dev/null | sort -V | tail -n 1 || true)"
  core_dir="${core_dir%/}"

  if [[ -z "$core_dir" || ! -d "$core_dir" ]]; then
    return 1
  fi

  echo "$core_dir"
  return 0
}

get_fqbn_option() {
  local key="$1"
  local fqbn="$2"

  local _pkg _arch _board _opts
  IFS=':' read -r _pkg _arch _board _opts <<<"$fqbn"

  if [[ -z "${_opts:-}" ]]; then
    return 1
  fi

  local part
  IFS=',' read -ra parts <<<"$_opts"
  for part in "${parts[@]}"; do
    if [[ "$part" == "$key="* ]]; then
      echo "${part#*=}"
      return 0
    fi
  done
  return 1
}

partition_csv_app0_size_dec() {
  local csv_path="$1"

  while IFS=',' read -r name type subtype offset size flags; do
    name=$(trim "${name:-}")
    type=$(trim "${type:-}")
    subtype=$(trim "${subtype:-}")
    size=$(trim "${size:-}")

    [[ -z "$name" ]] && continue
    [[ "$name" == \#* ]] && continue

    if [[ "$type" == "app" ]]; then
      if [[ "$name" == "app0" || "$subtype" == "ota_0" || "$subtype" == "factory" ]]; then
        # size is typically hex (0x...) or decimal; bash arithmetic handles both.
        echo "$((size))"
        return 0
      fi
    fi
  done < "$csv_path"

  return 1
}

register_partition_scheme_if_needed() {
  local boards_txt="$1"
  local board_id="$2"
  local scheme_id="$3"
  local partition_name_no_ext="$4"
  local upload_max_size_dec="$5"

  if grep -q "^${board_id}\.menu\.PartitionScheme\.${scheme_id}=" "$boards_txt"; then
    echo "✓ PartitionScheme '$scheme_id' already registered for board '$board_id'"
    return 0
  fi

  local label
  case "$scheme_id" in
    ota_1_9mb)
      label="Custom OTA (1.9MB APP×2)"
      ;;
    ota_2mb)
      label="Custom OTA headless (1.94MB APP×2, no SPIFFS)"
      ;;
    ota_3mb_8MB)
      label="Custom OTA 8MB (3MB APP×2)"
      ;;
    *)
      label="Custom (${scheme_id})"
      ;;
  esac

  # Derive flash_size override from a trailing _<N>MB suffix on the scheme id
  # (e.g. ota_3mb_8MB → 8MB, ota_6mb_16MB → 16MB). Required because some board
  # defs (notably Inkplate5V2) ship build.flash_size=4MB even on 8MB hardware;
  # without this override the bootloader rejects partitions past 0x400000 and
  # the device immediately resets after the ROM hands off to stage 2.
  local flash_size_override=""
  if [[ "$scheme_id" =~ _([0-9]+)MB$ ]]; then
    flash_size_override="${BASH_REMATCH[1]}MB"
  fi

  {
    echo ""
    echo "# Custom partition scheme '$scheme_id' (installed by $REPO_ROOT/tools/install-custom-partitions.sh)"
    echo "${board_id}.menu.PartitionScheme.${scheme_id}=${label}"
    echo "${board_id}.menu.PartitionScheme.${scheme_id}.build.partitions=${partition_name_no_ext}"
    echo "${board_id}.menu.PartitionScheme.${scheme_id}.upload.maximum_size=${upload_max_size_dec}"
    if [[ -n "$flash_size_override" ]]; then
      echo "${board_id}.menu.PartitionScheme.${scheme_id}.build.flash_size=${flash_size_override}"
    fi
  } >> "$boards_txt"

  echo "✓ Registered PartitionScheme '$scheme_id' for board '$board_id'${flash_size_override:+ (flash_size=$flash_size_override)}"
}

ESP32_DIR=""

# Source config to access FQBN_TARGETS (and project overrides).
source "$REPO_ROOT/config.sh"

shopt -s nullglob
PARTITION_FILES=("$REPO_ROOT"/partitions/*.csv)
shopt -u nullglob
if [[ ${#PARTITION_FILES[@]} -eq 0 ]]; then
  echo "Note: no partition CSVs found under $REPO_ROOT/partitions/"
fi

# Track which package install dirs already received the CSV copy this run,
# so multi-board configs sharing one package don't trigger duplicate work.
declare -A INSTALLED_PACKAGES

for board_name in "${!FQBN_TARGETS[@]}"; do
  fqbn="${FQBN_TARGETS[$board_name]}"
  IFS=':' read -r vendor arch board_id _ <<<"$fqbn"

  scheme_id=$(get_fqbn_option "PartitionScheme" "$fqbn" || true)
  if [[ -z "$scheme_id" ]]; then
    continue
  fi

  csv_path="$REPO_ROOT/partitions/partitions_${scheme_id}.csv"
  # If this is a core-provided scheme (no matching CSV in repo) skip silently.
  if [[ ! -f "$csv_path" ]]; then
    continue
  fi

  core_dir=$(find_core_dir_for "$vendor" "$arch" || true)
  if [[ -z "$core_dir" ]]; then
    echo "Warning: skipping board '$board_name' — Arduino package '${vendor}:${arch}' not installed." >&2
    echo "         Run ./setup.sh (or arduino-cli core install ${vendor}:${arch}) to enable this board." >&2
    continue
  fi

  partition_dir="$core_dir/tools/partitions"
  boards_txt="$core_dir/boards.txt"

  if [[ ! -d "$partition_dir" || ! -f "$boards_txt" ]]; then
    echo "Warning: '$core_dir' missing partitions/ or boards.txt — skipping board '$board_name'." >&2
    continue
  fi

  # Copy every repo-provided CSV into this package's partitions/ on first touch.
  if [[ -z "${INSTALLED_PACKAGES[$core_dir]:-}" ]]; then
    echo "Installing custom partition tables into Arduino package..."
    echo "- Package:    ${vendor}:${arch}"
    echo "- Core:       $core_dir"
    echo "- Partitions: $partition_dir"
    for csv in "${PARTITION_FILES[@]}"; do
      base="$(basename "$csv")"
      cp "$csv" "$partition_dir/$base"
      echo "✓ Installed $base"
    done
    INSTALLED_PACKAGES[$core_dir]=1
  fi

  partition_name_no_ext="partitions_${scheme_id}"
  upload_max_size_dec=$(partition_csv_app0_size_dec "$csv_path" || true)
  if [[ -z "$upload_max_size_dec" ]]; then
    echo "Error: could not derive app0 size from $csv_path" >&2
    exit 1
  fi

  register_partition_scheme_if_needed "$boards_txt" "$board_id" "$scheme_id" "$partition_name_no_ext" "$upload_max_size_dec"
done

echo "Done."