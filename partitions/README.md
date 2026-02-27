# Custom Partition Schemes

This folder contains **optional** custom partition table CSVs that can be enabled via the Arduino ESP32 core’s `PartitionScheme=...` FQBN option.

For the broader build/release context, also see: [docs/build-and-release-process.md](../docs/build-and-release-process.md#custom-partition-schemes)

## How Arduino “PartitionScheme” works (important)

For Arduino ESP32 core builds, using a custom `PartitionScheme` requires **both**:

1. The partition CSV exists inside the installed ESP32 core:
   - `~/.arduino15/packages/esp32/hardware/esp32/<version>/tools/partitions/`
2. The scheme is registered in that same core’s `boards.txt` for the target board ID.

This template automates those two steps via: [tools/install-custom-partitions.sh](../tools/install-custom-partitions.sh)

## Adding a new partition scheme (another board)

### 1) Create the partition CSV in this repo

- Add a new CSV under this folder, e.g. `partitions_my_scheme.csv`.
- The Arduino core will refer to the partition “name” **without** `.csv`.
- Keep offsets aligned to ESP32 partition rules (commonly: app partitions aligned to `0x10000` / 64KB; `nvs`/`otadata` are usually the exceptions).

Example header (recommended):

```csv
# Name,    Type, SubType,  Offset,   Size,     Flags
nvs,       data, nvs,      0x9000,   0x5000,
otadata,   data, ota,      0xE000,   0x2000,
app0,      app,  ota_0,    0x10000,  0x1E0000,
app1,      app,  ota_1,    0x1F0000, 0x1E0000,
```

### 2) Register the scheme in the ESP32 Arduino core (via the installer script)

Run [tools/install-custom-partitions.sh](../tools/install-custom-partitions.sh) to install/register your new scheme.

This template’s installer script:
- Copies all `partitions/*.csv` from this repo into the installed ESP32 core.
- Registers only the repo-provided schemes that are actively used by the configured boards (from `config.sh` / `config.project.sh`).
- Derives `upload.maximum_size` from the `app0` partition size in the CSV.

To be auto-discovered by the script, name your CSV like:
- `partitions/partitions_<scheme_id>.csv`

You’ll need (conceptually):

- **Board ID**: the 3rd segment of the FQBN
  - Example: `esp32:esp32:nologo_esp32c3_super_mini:...` → board ID is `nologo_esp32c3_super_mini`
- **Scheme ID**: the string used in `PartitionScheme=<scheme_id>`
- **Partition filename (no extension)**: what `boards.txt` uses for `build.partitions` (the installer uses `partitions_<scheme_id>`)
- **upload.maximum_size**: must match your app partition size (bytes) (the installer derives this from the CSV)

The `boards.txt` entries you’re effectively adding look like:

```text
<BOARD_ID>.menu.PartitionScheme.<SCHEME_ID>=<Human label>
<BOARD_ID>.menu.PartitionScheme.<SCHEME_ID>.build.partitions=<PARTITION_FILE_NO_EXT>
<BOARD_ID>.menu.PartitionScheme.<SCHEME_ID>.upload.maximum_size=<MAX_BYTES>
```

### 3) Add/enable it in `config.sh` (or `config.project.sh`)

Add a target in [config.sh](../config.sh) (or, for template-based projects, in `config.project.sh`) that includes your `PartitionScheme` option in the FQBN, for example:

```bash
["<board_name>"]="esp32:esp32:<BOARD_ID>:CDCOnBoot=cdc,PartitionScheme=<SCHEME_ID>"
```

### 4) Re-run setup (or installer) and build

After adding or changing partition schemes, run:

```bash
./setup.sh
# or (manual)
./tools/install-custom-partitions.sh

./build.sh <board_name>
```

## Operational note

After changing the partition table, the **first flash should be done over serial (USB)**. OTA updates will work normally afterwards once the correct partition table is on the device.
