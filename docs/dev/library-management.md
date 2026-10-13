---
title: Library Management
description: Manage installed Arduino dependencies and the pinned project-local FreeType library.
---

This project uses a configuration file to manage Arduino libraries, ensuring consistent dependencies across local development and CI/CD environments.

> **Note:** The template ships with a small set of libraries already configured. Treat `arduino-libraries.txt` as the source of truth and add/remove entries as your project evolves.

## Configuration File

### Project-Local FreeType

`libraries/MacropadFreeType` vendors the minimal FreeType 2.13.2 profile used by
`HAS_FREETYPE_FONTS`. `build.sh` passes this directory's parent with
`arduino-cli compile --libraries`; local and CI builds need no global FreeType
installation, symlink, or extra setup download. This library is not managed by
`library.sh` or `arduino-libraries.txt`.

The module profile contains TrueType, SFNT, autofit, monochrome rasterization,
and PS names. External PNG, zlib, bzip2, Brotli, and HarfBuzz dependencies are
disabled. Public headers and selected upstream sources are retained with their
license notices. Wrapper translation units live under `src/`; upstream sources
stay outside it to prevent duplicate Arduino compilation.

`PROVENANCE.txt` records the release URL, SHA-256, selected source directories,
and reproduction steps. `heap-workspaces.patch` is applied to the vendored
sources and keeps large transient auto-hinter and raster workspaces off the
16 KiB rendering stack. `FTL.TXT`, `GPLv2.TXT`, and `LICENSE.TXT` preserve
upstream licensing; this project uses the FreeType License option. When
updating the release or patch, rerun both font-cache host tests and the
interactive e-paper board builds.

Portions of this software are copyright 2023
[The FreeType Project](https://freetype.org/). All rights reserved.
This software is based in part on the work of the FreeType Team.

### Installed Libraries

**`arduino-libraries.txt`** - Contains the list of required Arduino libraries

Heart Monitor pins `SparkFun MAX3010x Pulse and Proximity Sensor Library@1.1.2`
for sensor setup. Its source notices and installed license files remain
unchanged. Acquisition bypasses the library's four-entry software sample ring
and reads checked hardware FIFO bursts directly, so accumulated batches cannot
silently wrap that ring. No library pulse or SpO2 algorithm is used in the raw
recording spike.

### Format

```
# Comments start with #
LibraryName
AnotherLibrary@1.2.3  # You can specify versions
"Library With Spaces"
```

- One library per line
- Library names must match `arduino-cli` library names exactly
- Optional version specification: `LibraryName@version`
- Lines starting with `#` are comments
- Empty lines are ignored

## Using the Library Management Script

The `library.sh` script provides convenient commands for managing libraries:

### Search for Libraries

```bash
./library.sh search <keyword>
```

Example:
```bash
./library.sh search mqtt
./library.sh search "adafruit sensor"
```

### Add a Library

Adds a library to `arduino-libraries.txt` and installs it immediately:

```bash
./library.sh add <library_name>
```

Examples:
```bash
./library.sh add PubSubClient
./library.sh add "Adafruit GFX Library"
./library.sh add ArduinoJson@6.21.3
```

### Remove a Library

Removes a library from `arduino-libraries.txt` (doesn't uninstall):

```bash
./library.sh remove <library_name>
```

### List Configured Libraries

Shows all libraries defined in `arduino-libraries.txt`:

```bash
./library.sh list
```

### Install All Libraries

Installs all libraries from `arduino-libraries.txt`:

```bash
./library.sh install
```

This is automatically called by `setup.sh`.

### Show Installed Libraries

Lists all currently installed libraries:

```bash
./library.sh installed
```

## Workflow Integration

### Local Development

1. Search for a library:
   ```bash
   ./library.sh search "sensor library"
   ```

2. Add it to your project:
   ```bash
   ./library.sh add "Adafruit BME280 Library"
   ```

3. The library is automatically installed and added to `arduino-libraries.txt`

4. Commit `arduino-libraries.txt`:
   ```bash
   git add arduino-libraries.txt
   git commit -m "Add BME280 sensor library"
   ```

### CI/CD (GitHub Actions)

The GitHub workflow automatically:
1. Runs `setup.sh` during environment setup
2. `setup.sh` reads `arduino-libraries.txt` and installs all listed libraries
3. Libraries are cached for faster subsequent builds

No additional workflow configuration needed!

## Manual Library Management

You can also edit `arduino-libraries.txt` directly:

```bash
# Edit the file
nano arduino-libraries.txt

# Install the updated libraries
./library.sh install
```

## Finding Library Names

To find the exact library name for `arduino-cli`:

```bash
./library.sh search <keyword>
```

Or visit: https://www.arduinolibraries.info/

## Example Workflow

```bash
# Initial setup
./setup.sh

# Add a new dependency (example: BME280 sensor)
./library.sh search bme280
./library.sh add "Adafruit BME280 Library"

# Verify it's configured
./library.sh list

# Commit the change
git add arduino-libraries.txt
git commit -m "Add BME280 sensor library"
git push

# GitHub Actions will automatically install configured libraries during CI
```

## Troubleshooting

### Library Not Found

If `./library.sh add LibraryName` fails:
1. Search for the exact name: `./library.sh search keyword`
2. Check on https://www.arduinolibraries.info/
3. Verify the library is available for ESP32

### Version Conflicts

Specify exact versions in `arduino-libraries.txt`:
```
ArduinoJson@6.21.3
PubSubClient@2.8.0
```

### Library Already Installed

If you manually installed a library before, it won't be in `arduino-libraries.txt`. To add it:

```bash
./library.sh add LibraryName
```

This updates the config without reinstalling.
