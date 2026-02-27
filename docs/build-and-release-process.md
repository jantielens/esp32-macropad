# Build and Release Process Guide

This document describes the build system configuration and automated release workflow for the ESP32 Template project.

## Table of Contents

- [Project Branding Configuration](#project-branding-configuration)
- [Build System](#build-system)
- [Release Workflow](#release-workflow)
- [Release Scenarios](#release-scenarios)
- [Custom Partition Schemes](#custom-partition-schemes)

---

## Project Branding Configuration

### Overview

The project uses a centralized branding system defined in `config.sh` (with an optional `config.project.sh` overlay). These values control your project's identity across builds, releases, web UI, and device names.

### Configuration Variables

Defaults are located at the top of `config.sh` (and can be overridden in `config.project.sh`):

```bash
PROJECT_NAME="esp32-template"       # Slug format (no spaces)
PROJECT_DISPLAY_NAME="ESP32 Template"   # Human-readable format
```

### Usage Map

#### `PROJECT_NAME` (filename-safe slug)

Used for technical identifiers and filenames:

| Location | Usage | Example |
|----------|-------|---------|
| **Build artifacts** | Local build output | `build/esp32-nodisplay/app.ino.bin` |
| **CI/CD artifacts** | GitHub Actions artifact names | `esp32-template-esp32-nodisplay` |
| **Release files** | GitHub Release download files | `esp32-template-esp32-nodisplay-v0.0.5.bin` |
| **AP SSID** | WiFi Access Point name (uppercase) | `ESP32-TEMPLATE-1A2B3C4D` |
| **API response** | `/api/info` endpoint | `{"project_name": "esp32-template"}` |

#### `PROJECT_DISPLAY_NAME` (human-readable)

Used for user-facing text and branding:

| Location | Usage | Example |
|----------|-------|---------|
| **Web portal title** | Browser tab title | `"ESP32 Template Configuration Portal"` |
| **Web portal header** | Main page heading | `"ESP32 Template Configuration"` |
| **Default device name** | First-time device name | `"ESP32 Template 1A2B"` |
| **API response** | `/api/info` endpoint | `{"project_display_name": "ESP32 Template"}` |

### Customizing for Your Project

1. **Set branding values**:

  - For standalone use: edit `config.sh` at the top of the file.
  - For template-based projects (recommended): copy `config.project.sh.example` to `config.project.sh` and put your project-specific overrides there.

   ```bash
   PROJECT_NAME="my-iot-device"
   PROJECT_DISPLAY_NAME="My IoT Device"
   ```

2. **Rebuild** to apply changes:
   ```bash
   ./build.sh
   ```

3. **What gets updated automatically**:
   - HTML page titles and headers (baked in at build time)
   - AP SSID prefix (defined in firmware)
   - Default device names (defined in firmware)
   - GitHub workflow artifact names
   - Release file names

**Note**: No code changes required - everything is templated!

---

## Build System

### Build Configuration

The build system automatically applies project branding during compilation:

1. `build.sh` sources `config.sh` to get `PROJECT_NAME` and `PROJECT_DISPLAY_NAME` (and `config.sh` will also source `config.project.sh` if present)
2. (Optional) If `assets/png/*.png` exists and you are building a display-enabled board, `build.sh` generates LVGL image assets into `src/app/png_assets.cpp` and `src/app/png_assets.h`
3. `tools/minify-web-assets.sh` performs template substitution in HTML files
4. Branding C++ `#define` statements are generated in `src/app/project_branding.h` (and `web_assets.h` includes it)
5. If the repo was built from a GitHub checkout with a detectable `remote.origin.url`, the repo slug is generated into `src/app/repo_slug_config.h` (used to construct GitHub Pages links)
6. `build.sh` also embeds the board name as a compile-time string define (`BUILD_BOARD_NAME`) so the portal can report the active board
7. Firmware compiles with branded values embedded

### Board-Specific Configuration

The build system supports optional board-specific configuration using compile-time defines and conditional compilation.

**How It Works:**

1. **Board Defines Hardware Capabilities**: Create `src/boards/[board-name]/board_overrides.h` with board-specific defines
2. **Two-Phase Include Pattern**: Main config includes board overrides first, then applies defaults with `#ifndef` guards
3. **Conditional Compilation in App**: Use `#if HAS_xxx` in application code for board-specific logic
4. **Zero Runtime Overhead**: Compiler eliminates unused code automatically

**Default Configuration** (`src/app/board_config.h`):
- Hardware capabilities (LED, buttons, sensors, displays, etc.)
- Pin mappings (GPIO numbers)
- WiFi settings (max attempts, retry delays)
- Power management settings

**Board-Specific Overrides** (Optional): Create `src/boards/[board-name]/board_overrides.h`:

```bash
# Example: ESP32 with built-in LED and button
mkdir -p src/boards/esp32-nodisplay
cat > src/boards/esp32-nodisplay/board_overrides.h << 'EOF'
#ifndef BOARD_OVERRIDES_ESP32_H
#define BOARD_OVERRIDES_ESP32_H

// LED configuration
#define HAS_BUILTIN_LED true
#define LED_PIN 2
#define LED_ACTIVE_HIGH true

// Button configuration
#define HAS_BUTTON true
#define BUTTON_PIN 0
#define BUTTON_ACTIVE_LOW true

#endif
EOF
```

**Application Usage** (`src/app/app.ino`):

```cpp
#include "board_config.h"

void setup() {
  #if HAS_BUILTIN_LED
  // Only compiled for boards with LED
  pinMode(LED_PIN, OUTPUT);
  #endif

  #if HAS_BUTTON
  // Only compiled for boards with button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  #endif
}

void loop() {
  #if HAS_BUTTON
  if (digitalRead(BUTTON_PIN) == (BUTTON_ACTIVE_LOW ? LOW : HIGH)) {
    // Handle button press - only on boards with buttons
  }
  #endif
}
```

**Advanced Example - Different Display Backends (HAL):**

This project selects display backends via a single selector macro in `board_overrides.h`:

```cpp
// Board A override (example: Arduino_GFX QSPI)
#define HAS_DISPLAY true
#define DISPLAY_DRIVER DISPLAY_DRIVER_ARDUINO_GFX
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 480

// Board B override (example: TFT_eSPI backend, controller configured separately)
#define HAS_DISPLAY true
#define DISPLAY_DRIVER DISPLAY_DRIVER_TFT_ESPI
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
```

Application code typically does **not** include display library headers directly. Instead it talks to the HAL (`DisplayDriver`) and/or the `DisplayManager`.
If you need compile-time backend-specific logic, use the selector value:

```cpp
#include "board_config.h"

#if HAS_DISPLAY
  #if DISPLAY_DRIVER == DISPLAY_DRIVER_ARDUINO_GFX
    // Arduino_GFX-specific compile-time tweaks
  #elif DISPLAY_DRIVER == DISPLAY_DRIVER_TFT_ESPI
    // TFT_eSPI-specific compile-time tweaks
  #endif
#endif
```

Driver implementations are conditionally compiled via the sketch-root translation unit `src/app/display_drivers.cpp`.

**Build Detection**: The build script automatically:
1. Checks if `src/boards/[board-name]/` directory exists
2. Adds it to the compiler include path with `-I` flag (for both C++ and C compilation units)
3. Defines `BOARD_<BOARDNAME>` (uppercased, e.g., `BOARD_CYD_V2`) and `BOARD_HAS_OVERRIDE=1` (for both C++ and C compilation units)
4. `src/app/board_config.h` includes `board_overrides.h` first (Phase 1)
5. Default values are defined with `#ifndef` guards so overrides take precedence (Phase 2)
6. If no override directory exists, uses defaults only

**Benefits:**
- ✅ Single codebase works for all board variants
- ✅ Zero runtime overhead (dead code elimination)
- ✅ Type-safe compile-time checks
- ✅ Easy to add new boards without modifying application code

### Build Profiles (Optional)

`build.sh` honors an optional `BOARD_PROFILE` (or `PROFILE`) environment variable. If `config.sh` defines `get_build_props_for_board <board> <profile>`, the build script will pass the returned extra `--build-property` flags to `arduino-cli`.

**Function Contract:**
- `get_build_props_for_board` must output **one argument per line** (newline-delimited)
- Each line is a complete argument (e.g., `--build-property` on one line, then its value on the next)
- Values can contain spaces and quotes - they will be preserved correctly

**Usage Examples:**
```bash
BOARD_PROFILE=psram ./build.sh esp32-nodisplay
PROFILE=16m ./build.sh esp32c3-withsensors
```

**Example Implementation:**
```bash
# Add to config.sh to define custom build properties
get_build_props_for_board() {
    local board="$1"
    local profile="$2"
    
    case "$board:$profile" in
      esp32-nodisplay:psram)
            echo "--build-property"
            echo "build.extra_flags=-DBOARD_HAS_PSRAM -DCONFIG_SPIRAM_SUPPORT=1"
            echo "--build-property"
            echo "compiler.cpp.extra_flags=-mfix-esp32-psram-cache-issue"
            ;;
      esp32c3-withsensors:16m)
            echo "--build-property"
            echo "build.flash_size=16MB"
            ;;
    esac
}
```

---

## Custom Partition Schemes

Some ESP32 boards (notably ESP32-C3 “Super Mini” variants with 4MB flash) can run out of space for OTA-enabled firmware as projects grow.

This template includes an **optional** custom partition scheme that:
- Keeps two OTA app partitions (safer OTA)
- Increases each app partition size to ~1.9MB
- Shrinks SPIFFS to a minimal size

### What’s Included

- Partition CSV: `partitions/partitions_ota_1_9mb.csv`
- Board example in `config.sh`: `esp32c3_ota_1_9mb` (uses `PartitionScheme=ota_1_9mb`)
- Installer script: `tools/install-custom-partitions.sh`

### How It’s Installed

For Arduino ESP32 core builds, **both** steps are required:
1. Copy the CSV into the installed ESP32 core’s partitions directory:
  `~/.arduino15/packages/esp32/hardware/esp32/*/tools/partitions/`
2. Register the scheme in that core’s `boards.txt` so `PartitionScheme=ota_1_9mb` is recognized.

This template automates the installation:
- Local development: `./setup.sh` runs `tools/install-custom-partitions.sh`
- CI/CD: GitHub Actions workflows run the same installer before compiling

### Important Operational Notes

- The **first flash after changing a partition table should be done over serial** (USB). OTA updates will work normally afterwards.
- For boards using `PartitionScheme=...`, `upload.sh` defaults to a full flash (`--full`) to ensure the partition layout on the device matches what was built.
- If you see errors like “offset not aligned” or “sketch too big”, verify your offsets are 0x10000-aligned (except NVS/otadata) and that your firmware fits in the configured app partition size.

**Notes:**
- If `get_build_props_for_board` is **not** defined, the build still proceeds (the call is guarded).
- Use profiles to toggle flash/PSRAM options or other board-specific build properties.
- Each argument must be on its own line (newline-separated, not space-separated).

**Benefits**:
- Zero code duplication when boards are identical
- Add customization only when needed
- Clear separation of common vs board-specific settings
- Compile-time optimization (zero runtime overhead)

### Compile-Time Flags Reporting

This repository includes tooling to keep compile-time configuration discoverable and auditable.

- Generated report: `docs/compile-time-flags.md` (flag list, per-board matrices, and per-file preprocessor usage map)
- Generator: `tools/compile_flags_report.py`
- Build logs: `build.sh` prints a per-board "Compile-time flags summary" so you can see which `HAS_*` features and key selectors are active for the board being built

**Update the report locally:**
```bash
python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md
```

### Template Syntax

HTML files use simple placeholder syntax:

```html
<title>{{PROJECT_DISPLAY_NAME}} Configuration Portal</title>
<h1>{{PROJECT_DISPLAY_NAME}} Configuration</h1>
```

These are replaced at build time with actual values from `config.sh`.

---

## Release Workflow

### Overview

The project uses an automated release workflow that triggers on version tags. Releases are created through GitHub Actions, which builds firmware for all board variants and publishes them to GitHub Releases with branded filenames.

### Workflow Components

- **`config.sh`** - Project branding configuration
- **`.github/workflows/release.yml`** - Automated release pipeline triggered by version tags
- **`.github/workflows/pages-from-release.yml`** - Deploys the GitHub Pages firmware installer for stable releases
- **`tools/extract-changelog.sh`** - Parses CHANGELOG.md for version-specific notes
- **`create-release.sh`** - Helper script to automate release preparation
- **`tools/build-esp-web-tools-site.sh`** - Builds the static installer site (HTML + manifests + firmware copies)
- **`src/version.h`** - Firmware version tracking
- **`CHANGELOG.md`** - Release notes in Keep a Changelog format

---

## Web Firmware Installer (GitHub Pages / ESP Web Tools)

This repository includes a static firmware installer site (no backend) powered by ESP Web Tools. It is deployed to GitHub Pages from **stable GitHub Releases**.

### Why multi-part flashing is required

For custom partition layouts, flashing a single “merged.bin at offset 0” can overwrite areas like NVS (and can fail to boot if `app0` is not at the default offset).

This template’s browser installer uses **multi-part flashing**:

- bootloader @ `0x0`
- partition table @ `0x8000`
- `boot_app0` @ `0xE000`
- app @ the `app0` offset derived from `app.ino.partitions.bin`

### Same-origin requirement (CORS)

To avoid CORS issues in browsers, the **installer page**, **manifest JSON**, and **firmware `.bin` files** must be served from the same GitHub Pages origin.

### How Pages deployment works

- **Trigger**: `.github/workflows/pages-from-release.yml` runs on `release.published` and refuses pre-releases.
- **Manual deploy**: The same workflow supports `workflow_dispatch` with a `tag_name` input (still refuses pre-releases).
- **Inputs**:
  - Downloads app + multi-part assets from the release.
  - Fetches the release body into `release-notes.md`.
  - Rehydrates `build/<board>/` with:
    - `app.ino.bin`
    - `app.ino.bootloader.bin`
    - `app.ino.partitions.bin`
    - `boot_app0.bin`
- **Output**: `tools/build-esp-web-tools-site.sh` generates `site/` (HTML, `manifests/*.json`, `firmware/<board>/*.bin`) which is deployed via GitHub Pages “Source: GitHub Actions”.

---

## Release Scenarios

## Scenario 1: Standard Release (Recommended)

**When to use**: Regular version releases (e.g., v0.0.5, v1.0.0)

### Step 1: Prepare the Release

```bash
# Run the release preparation script
./create-release.sh 0.0.5 "Improved logging and stability"
```

**What happens**:
- Creates branch `release/v0.0.5`
- Updates `src/version.h` with version numbers (0, 0, 5)
- Moves `[Unreleased]` section in `CHANGELOG.md` to `[0.0.5] - 2025-11-26`
- Adds new empty `[Unreleased]` section
- Commits changes
- Pushes branch and provides PR URL

### Step 2: Review and Merge PR

1. Open the PR URL provided by the script
2. Review the version bump and changelog changes
3. CI validates the build (GitHub Actions runs on PR)
4. Approve and merge the PR

### Step 3: Tag and Publish

```bash
# Switch to main and pull merged changes
git checkout main
git pull

# Create annotated tag
git tag -a v0.0.5 -m "Release v0.0.5"

# Push the tag to trigger release workflow
git push origin v0.0.5
```

**What happens automatically**:
- GitHub Actions builds firmware for all boards (e.g., esp32-nodisplay, cyd-v2, jc3636w518)
- Extracts changelog section for v0.0.5
- Creates GitHub Release with:
  - `esp32-template-esp32-nodisplay-v0.0.5.bin` (app-only)
  - `esp32-template-esp32-nodisplay-v0.0.5-bootloader.bin`
  - `esp32-template-esp32-nodisplay-v0.0.5-partitions.bin`
  - `esp32-template-esp32-nodisplay-v0.0.5-boot_app0.bin`
  - `esp32-template-esp32-nodisplay-v0.0.5-merged.bin` (legacy; may overwrite NVS)
  - `SHA256SUMS.txt`
- Release notes populated from CHANGELOG.md (no auto-generated “What’s Changed” section)
- Debug symbols (`.elf`) and build metadata available in workflow artifacts

**Additional automation for stable releases**:
- After the GitHub Release is published (and is not a prerelease), `.github/workflows/pages-from-release.yml` deploys the GitHub Pages firmware installer.

### Step 4: Verify

Check the release page: `https://github.com/<owner>/<repo>/releases/tag/v0.0.5`

Verify:
- ✅ Release notes match CHANGELOG
- ✅ All firmware binaries present
- ✅ SHA256 checksums included
- ✅ Not marked as pre-release

---

## Scenario 2: Pre-Release / Beta Testing

**When to use**: Testing release workflow, beta versions, release candidates

### Create Pre-Release

```bash
# Tag with hyphen suffix for pre-release
git tag -a v0.0.5-beta.1 -m "Pre-release v0.0.5-beta.1"
git push origin v0.0.5-beta.1
```

**What happens**:
- Same build process as standard release
- Release automatically marked as **"Pre-release"** on GitHub
- Useful for testing without affecting stable release stream

**Pre-release naming examples**:
- `v1.0.0-beta.1`, `v1.0.0-beta.2` - Beta versions
- `v1.0.0-rc.1`, `v1.0.0-rc.2` - Release candidates
- `v1.0.0-alpha.1` - Alpha versions

---

## Scenario 3: Hotfix Release

**When to use**: Critical bug fixes that need immediate release

### Quick Process

```bash
# Create hotfix branch from main
git checkout main
git pull
git checkout -b hotfix/v0.0.6

# Make your fixes
# ... edit files ...

# Update version and changelog manually
# Edit src/version.h: VERSION_PATCH 6
# Edit CHANGELOG.md: Add [0.0.6] section with fix details

# Commit, push, and create PR
git add .
git commit -m "fix: critical bug in WiFi reconnection"
git push origin hotfix/v0.0.6

# After PR merged:
git checkout main
git pull
git tag -a v0.0.6 -m "Hotfix v0.0.6"
git push origin v0.0.6
```

**Note**: You can use `./create-release.sh 0.0.6 "Critical hotfix"` instead of manual edits.

---

## Scenario 4: Manual Release (Without Helper Script)

**When to use**: Custom workflows, special cases

### Step-by-Step Manual Process

```bash
# 1. Create release branch
git checkout -b release/v1.0.0

# 2. Edit src/version.h
# Change:
#   #define VERSION_MAJOR 0
#   #define VERSION_MINOR 0
#   #define VERSION_PATCH 4
# To:
#   #define VERSION_MAJOR 1
#   #define VERSION_MINOR 0
#   #define VERSION_PATCH 0

# 3. Edit CHANGELOG.md
# Replace:
#   ## [Unreleased]
# With:
#   ## [1.0.0] - 2025-11-26
# Add new [Unreleased] section at top

# 4. Commit and push
git add src/version.h CHANGELOG.md
git commit -m "chore: bump version to 1.0.0"
git push origin release/v1.0.0

# 5. Create PR, review, and merge

# 6. Tag the release
git checkout main
git pull
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

---

## Scenario 5: Testing Release Workflow Changes

**When to use**: Validating changes to `.github/workflows/release.yml`

### Safe Testing Process

```bash
# Use a test pre-release tag
git tag -a v0.0.0-test.1 -m "Testing release workflow"
git push origin v0.0.0-test.1

# Watch GitHub Actions: https://github.com/<owner>/<repo>/actions

# If successful, delete test release and tag
# Via GitHub UI: Delete the release
# Via CLI:
git push --delete origin v0.0.0-test.1
git tag -d v0.0.0-test.1
```

---

## Scenario 6: Feature Branch with Multiple Commits

**When to use**: Large feature development over multiple commits

### Development Process

```bash
# 1. Create feature branch
git checkout -b feature/new-sensor-support

# 2. Make multiple commits during development
git add .
git commit -m "feat: add sensor library"
# ... more work ...
git commit -m "feat: implement sensor reading"
# ... more work ...
git commit -m "docs: update sensor documentation"

# 3. Push and create PR
git push origin feature/new-sensor-support
# Create PR: feature/new-sensor-support → main

# 4. Update CHANGELOG.md during PR review
# Add feature details to [Unreleased] section

# 5. After PR merged, prepare release as normal
git checkout main
git pull
./create-release.sh 0.1.0 "Added sensor support"
```

**Best Practice**: Update CHANGELOG.md in the feature branch or during PR review, so the Unreleased section is always current.

---

## Scenario 7: Rollback Release

**When to use**: Critical issue discovered in a release

### Immediate Actions

```bash
# Option 1: Delete the release (GitHub UI or API)
# Go to: https://github.com/<owner>/<repo>/releases
# Edit release → Delete release

# Option 2: Mark release as "Pre-release" to hide from latest
# Edit release → Check "Set as a pre-release"

# Then create a hotfix release with the fix
./create-release.sh 0.0.7 "Hotfix for v0.0.6 issue"
```

**Note**: Git tags are permanent in history. Deleting a release only removes the GitHub Release, not the tag.

---

## Release Artifacts

### What's in GitHub Releases (Public)

Each release includes:
- **Firmware binaries** (app-only): `$PROJECT_NAME-<board>-vX.Y.Z.bin` for each board variant
- **Firmware binaries** (multi-part; browser installer):
  - `$PROJECT_NAME-<board>-vX.Y.Z-bootloader.bin`
  - `$PROJECT_NAME-<board>-vX.Y.Z-partitions.bin`
  - `$PROJECT_NAME-<board>-vX.Y.Z-boot_app0.bin`
- **Firmware binaries** (legacy merged; may overwrite NVS): `$PROJECT_NAME-<board>-vX.Y.Z-merged.bin`
- **Checksums**: `SHA256SUMS.txt` for verification
- **Release notes**: Extracted from CHANGELOG.md

### What's in Workflow Artifacts (Developers)

Available for 90 days in GitHub Actions:
- **Debug symbols**: `.elf` files for debugging
- **Build metadata**: `build-info-*.txt` with version, commit SHA, flash commands

---

## Changelog Format

Follow [Keep a Changelog](https://keepachangelog.com/) format:

```markdown
## [Unreleased]

### Added
- New feature description

### Changed
- Changed behavior description

### Fixed
- Bug fix description

---

## [0.0.5] - 2025-11-26

### Added
- Feature that was released

### Fixed
- Bug that was fixed
```

**Categories**: Added, Changed, Deprecated, Removed, Fixed, Security

---

## Versioning Strategy

Follow [Semantic Versioning](https://semver.org/):

- **MAJOR** (X.0.0): Breaking changes, incompatible API changes
- **MINOR** (0.X.0): New features, backward-compatible
- **PATCH** (0.0.X): Bug fixes, backward-compatible

**Examples**:
- `0.0.1` → `0.0.2`: Bug fix
- `0.0.5` → `0.1.0`: New feature added
- `0.9.0` → `1.0.0`: Major milestone or breaking change
- `1.0.0-beta.1`: Pre-release before 1.0.0

---

## Troubleshooting

### Release Workflow Fails

1. Check GitHub Actions logs: `https://github.com/<owner>/<repo>/actions`
2. Common issues:
   - **Compilation error**: Fix code and push new tag
   - **Changelog parsing error**: Verify CHANGELOG.md format
   - **Missing tag**: Ensure tag was pushed (`git push origin v0.0.5`)

### Changelog Not Extracted

- Verify CHANGELOG.md has exact format: `## [0.0.5] - 2025-11-26`
- Test locally: `./tools/extract-changelog.sh 0.0.5`

### Wrong Version in Firmware

- Check `src/version.h` matches the tag
- Rebuild: Delete tag, fix version.h, re-tag

### Duplicate Release

- Delete existing release in GitHub UI
- Delete and re-push tag:
  ```bash
  git tag -d v0.0.5
  git push --delete origin v0.0.5
  git tag -a v0.0.5 -m "Release v0.0.5"
  git push origin v0.0.5
  ```

### Tag Created but Release Workflow Didn't Run

**Symptom**: After using `/release` command, the tag exists but no GitHub Release was created.

**Cause**: The PR commands workflow uses `GITHUB_TOKEN` which doesn't trigger other workflows (security feature).

**Solution**:
1. Configure `PAT_TOKEN` secret (see [PR Commands Workflow](#pr-commands-workflow-githubworkflowspr-commandsyml) section)
2. For existing orphaned tags, manually recreate them:
   ```bash
   # Delete tag remotely and locally
   git push --delete origin v0.0.6
   git tag -d v0.0.6
   
   # Recreate and push (this will trigger release workflow)
   git tag -a v0.0.6 -m "Release v0.0.6"
   git push origin v0.0.6
   ```

### Release Published but Pages Installer Didn't Deploy

**Symptoms**: The GitHub Release exists, but the GitHub Pages installer site did not update.

**Checklist**:
1. Confirm the release is **stable** (not marked as prerelease). The Pages workflow refuses prereleases.
2. Check `.github/workflows/pages-from-release.yml` ran (Actions → workflow runs).
3. Ensure the release was created using a token that can emit downstream events:
  - The release workflow prefers `PAT_TOKEN` when creating the GitHub Release.
  - If the release was created with `GITHUB_TOKEN` only, `release.published` may not reliably trigger downstream workflows.

**Fix**: Configure `PAT_TOKEN` and re-publish the release (or recreate the release) so `release.published` triggers the Pages deploy.

---

## CI/CD Pipeline Summary

### Build Workflow (`.github/workflows/build.yml`)

**Triggers**:
- Pull requests to `main` or `develop`
- Manual trigger

**Purpose**: Validate code compiles before merge

**Notes**:
- The board build matrix order is deterministic (board names are sorted) so CI output is stable.
- CI regenerates `docs/compile-time-flags.md` and fails if the generated output differs (keeps the report in sync with code).

### Release Workflow (`.github/workflows/release.yml`)

**Triggers**:
- Tags matching `v*.*.*`

**Purpose**: Build and publish firmware releases

**Process**:
1. Extract version from tag
2. Build firmware for all boards (matrix)
3. Extract changelog section
4. Create GitHub Release
5. Upload firmware binaries
6. Generate checksums

### Pages Installer Deploy Workflow (`.github/workflows/pages-from-release.yml`)

**Triggers**:
- On stable release publish (`release.published` where `prerelease == false`)
- Manual trigger with `workflow_dispatch` (requires a stable `tag_name`)

**Purpose**: Deploy a static ESP Web Tools installer to GitHub Pages without rebuilding firmware.

**Note**: If GitHub Pages is not configured for the repository, the workflow will skip Pages configuration/upload/deploy steps and log a clear notice in the summary.

**Process**:
1. Download app + multi-part assets from the release
2. Rehydrate `build/<board>/` with app/bootloader/partitions/boot_app0 binaries
3. Run `tools/build-esp-web-tools-site.sh` to produce `site/`
4. Deploy `site/` to GitHub Pages via GitHub Actions artifacts

### PR Commands Workflow (`.github/workflows/pr-commands.yml`)

**Triggers**:
- PR comments containing `/release`, `/release-beta`, or `/merge-only`

**Purpose**: Streamlined release process via PR comments

**Setup Required**:

⚠️ **Important**: To allow the release workflow to trigger automatically, you must configure a Personal Access Token (PAT):

1. **Create PAT**:
   - Go to GitHub Settings → Developer settings → Personal access tokens → Tokens (classic)
   - Click "Generate new token (classic)"
   - Name: `ESP32 Release Token`
   - Expiration: Choose appropriate duration
   - Scopes: Select `repo` (full control of private repositories)
   - Click "Generate token" and copy it immediately

2. **Add Secret**:
   - Go to repository Settings → Secrets and variables → Actions
   - Click "New repository secret"
   - Name: `PAT_TOKEN`
   - Value: Paste your PAT
   - Click "Add secret"

**Why PAT is needed**: GitHub Actions doesn't trigger workflows when tags are created by `GITHUB_TOKEN` (security feature). Using a PAT allows the tag push to trigger the release workflow.

**Commands**:
- `/release` - Merge PR and create stable release tag
- `/release-beta <N>` - Merge PR and create beta release tag (e.g., `/release-beta 1`)
- `/merge-only` - Just merge PR without creating a tag

**Process**:
1. Check user has write permissions
2. Verify PR is mergeable
3. Extract version from `src/version.h`
4. Merge PR with descriptive commit message
5. Create and push tag (triggers release workflow)
6. Comment with workflow links

---

## Quick Reference

| Task | Command |
|------|---------|
| Prepare release | `./create-release.sh X.Y.Z "Title"` |
| Create tag | `git tag -a vX.Y.Z -m "Message"` |
| Push tag | `git push origin vX.Y.Z` |
| Delete remote tag | `git push --delete origin vX.Y.Z` |
| Delete local tag | `git tag -d vX.Y.Z` |
| Test changelog parser | `./tools/extract-changelog.sh X.Y.Z` |
| Pre-release tag | `git tag -a vX.Y.Z-beta.1 -m "Message"` |

---

## Best Practices

1. **Always use the helper script** for consistency
2. **Update CHANGELOG.md regularly** during development
3. **Test with pre-release tags** before stable releases
4. **Review release PR carefully** - last chance to verify version/changelog
5. **Keep version.h in sync** with git tags
6. **Use semantic versioning** for clear version progression
7. **Write descriptive release titles** for clarity
8. **Verify releases** after creation on GitHub

---

## Related Documentation

- [Main README](../README.md) - Project overview
- [Scripts Guide](scripts.md) - Build and upload scripts
- [Library Management](library-management.md) - Managing dependencies
- [CHANGELOG](../CHANGELOG.md) - Release history
