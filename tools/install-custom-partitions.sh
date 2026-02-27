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

ESP32_HW_BASE="$HOME/.arduino15/packages/esp32/hardware/esp32"

trim() {
  echo "$1" | xargs
}

find_esp32_core_dir() {
  if [[ ! -d "$ESP32_HW_BASE" ]]; then
    echo "Error: ESP32 Arduino core not found at $ESP32_HW_BASE" >&2
    echo "Run ./setup.sh first to install esp32:esp32." >&2
    exit 1
  fi

  # Pick the latest installed ESP32 core directory.
  # Example: ~/.arduino15/packages/esp32/hardware/esp32/3.0.7
  local esp32_dir
  esp32_dir="$(ls -1d "$ESP32_HW_BASE"/*/ 2>/dev/null | sort -V | tail -n 1 || true)"
  esp32_dir="${esp32_dir%/}"

  if [[ -z "$esp32_dir" || ! -d "$esp32_dir" ]]; then
    echo "Error: could not locate an installed ESP32 core version under $ESP32_HW_BASE" >&2
    exit 1
  fi

  echo "$esp32_dir"
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
    *)
      label="Custom (${scheme_id})"
      ;;
  esac

  {
    echo ""
    echo "# Custom partition scheme '$scheme_id' (installed by $REPO_ROOT/tools/install-custom-partitions.sh)"
    echo "${board_id}.menu.PartitionScheme.${scheme_id}=${label}"
    echo "${board_id}.menu.PartitionScheme.${scheme_id}.build.partitions=${partition_name_no_ext}"
    echo "${board_id}.menu.PartitionScheme.${scheme_id}.upload.maximum_size=${upload_max_size_dec}"
  } >> "$boards_txt"

  echo "✓ Registered PartitionScheme '$scheme_id' for board '$board_id'"
}

ESP32_DIR="$(find_esp32_core_dir)"

PARTITION_DIR="$ESP32_DIR/tools/partitions"
BOARDS_TXT="$ESP32_DIR/boards.txt"

if [[ ! -d "$PARTITION_DIR" ]]; then
  echo "Error: partition directory not found: $PARTITION_DIR" >&2
  exit 1
fi

if [[ ! -f "$BOARDS_TXT" ]]; then
  echo "Error: boards.txt not found: $BOARDS_TXT" >&2
  exit 1
fi

echo "Installing custom partition table into Arduino ESP32 core..."
echo "- Core:       $ESP32_DIR"
echo "- Partitions: $PARTITION_DIR"

# 1) Copy all repo-provided partition CSVs into the core (idempotent overwrite).
shopt -s nullglob
partition_files=("$REPO_ROOT"/partitions/*.csv)
shopt -u nullglob
if [[ ${#partition_files[@]} -eq 0 ]]; then
  echo "Note: no partition CSVs found under $REPO_ROOT/partitions/"
else
  for csv in "${partition_files[@]}"; do
    base="$(basename "$csv")"
    cp "$csv" "$PARTITION_DIR/$base"
    echo "✓ Installed $base"
  done
fi

# 2) Register only the custom schemes that are actively used in config.sh (or config.project.sh).

# Source config to access FQBN_TARGETS (and project overrides).
source "$REPO_ROOT/config.sh"

for board_name in "${!FQBN_TARGETS[@]}"; do
  fqbn="${FQBN_TARGETS[$board_name]}"
  board_id=$(echo "$fqbn" | cut -d':' -f3)
  scheme_id=$(get_fqbn_option "PartitionScheme" "$fqbn" || true)
  if [[ -z "$scheme_id" ]]; then
    continue
  fi

  # Only handle schemes that exist in this repo (treat everything else as core-provided).
  csv_path="$REPO_ROOT/partitions/partitions_${scheme_id}.csv"
  if [[ ! -f "$csv_path" ]]; then
    continue
  fi

  partition_name_no_ext="partitions_${scheme_id}"
  upload_max_size_dec=$(partition_csv_app0_size_dec "$csv_path" || true)
  if [[ -z "$upload_max_size_dec" ]]; then
    echo "Error: could not derive app0 size from $csv_path" >&2
    exit 1
  fi

  register_partition_scheme_if_needed "$BOARDS_TXT" "$board_id" "$scheme_id" "$partition_name_no_ext" "$upload_max_size_dec"
done

echo "Done."