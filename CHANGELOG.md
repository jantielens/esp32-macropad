# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

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
- **Board config** — added `DISPLAY_SHAPE`, `UI_SCALE_TIER`, `HAS_PAD_SCREEN` flags for all boards

### Changed
- Partition tables updated for LittleFS support (6MB and 8MB variants)
- Compile-time flags documentation simplified

## [1.0.0] - 2026-02-27

### Added
- Initial release of ESP32 Macropad firmware (forked from esp32-template)
