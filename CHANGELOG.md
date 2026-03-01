# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

## [1.2.0] - 2026-03-01

### Added
- **Background image fetch** — per-button HTTP(S) image backgrounds with configurable refresh interval
  - `image_fetch` module: FreeRTOS background task with round-robin slot scheduling (up to 64 slots)
  - `image_decoder` module: JPEG (tjpgd) and PNG (lodepng) decode with cover-mode bilinear scaling to RGB565
  - Double-buffered PSRAM pixel data with tile-owned copy for safe LVGL rendering
  - HTTP Basic Auth support for authenticated camera feeds
  - Per-slot pause/resume — only the active page fetches images; hidden pages retain their last frame in memory
  - Web portal: "Image Background" section in pad button editor (URL, auth, refresh interval)
- **LVGL image decoders enabled** — `LV_USE_TJPGD=1` and `LV_USE_LODEPNG=1` in lv_conf.h

### Fixed
- LVGL task stack increased from 8 KB to 16 KB to prevent overflow when rendering scaled images
- WiFiClientSecure heap-allocated only for HTTPS URLs to reduce fetch task stack pressure
- ESP Web Tools flash site: ESP32-P4 boards now correctly identified as `ESP32-P4` chip family with bootloader offset 0x2000 (previously fell through to classic `ESP32` with wrong offset 0x1000)

## [1.1.0] - 2026-02-28

### Added
- **Pad screen system** — multi-page configurable button grid with tap and long-press actions
  - Layout engine with grid computation (up to 8×8) and UI scale tiers per board
  - LittleFS-backed JSON config at `/config/pad_N.json` (8 pages, 64 buttons max)
  - REST API: `GET/POST/DELETE /api/pad?page=N` with full validation
  - LVGL rendering with per-tile colors, border, corner radius, 3 labels (top/center/bottom), spanning
  - Typed action system: navigate to screen, navigate back, publish MQTT
  - Screen saver pixel-shift integration
- **MQTT live labels** — per-label MQTT bindings with topic, JSON path extraction, and printf format strings
  - Thread-safe `mqtt_sub_store` (FreeRTOS mutex, PSRAM-allocated, 64 entries)
  - Subscribe-all-pages on connect for instant data availability
  - Automatic resubscribe on config save/delete
- **Toggle state** — per-button state binding (topic, JSON path, on-value) dims tiles when OFF
- **Web portal pad section** — full CRUD UI for pad configuration
  - Edit dialog with sticky header/footer, scrollable body, mobile-responsive layout
  - Per-button: colors, labels, actions (tap + long-press), MQTT label bindings, toggle state
  - Pad selector with 1-indexed "Pad 1–8" naming
- **Display/touch improvements** — `goBack()` navigation, board-specific UI scale tiers and display shapes
- **LittleFS migration** — filesystem health monitoring with `fs_health` module
- **Board config** — added `DISPLAY_SHAPE` and `UI_SCALE_TIER` for all boards

### Changed
- Partition tables updated for LittleFS support (6MB and 8MB variants)
- Compile-time flags documentation simplified

## [1.0.0] - 2026-02-27

### Added
- Initial release of ESP32 Macropad firmware (forked from esp32-template)
