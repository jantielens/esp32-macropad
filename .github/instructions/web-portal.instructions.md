---
description: "Web portal conventions — multi-page architecture, REST API design, responsive layout, and UI patterns"
applyTo: "**/web_portal*, **/web/*.html, **/web/*.js, **/web/*.css, **/web_assets.h, **/config_manager.*, **/components/*_component.cpp, **/portal_components.cpp"
---

# Web Portal Conventions

## Component Registration (READ THIS BEFORE ADDING A NEW COMPONENT)

Arduino's build system only compiles `.cpp` files in the sketch root (`src/app/`). Files under `src/app/components/` are **not** compiled directly — they are `#include`-aggregated into `src/app/portal_components.cpp`.

**When you add a new `src/app/components/<name>_component.cpp`:**

1. Add `#include "components/<name>_component.cpp"` to `src/app/portal_components.cpp` under the correct feature-flag block (always-available, `HAS_DISPLAY`, `HAS_MQTT`, etc.).
2. Without this include, the component's `REGISTER_COMPONENT()` static initializer never runs. The component will be **silently absent** from the registry and the nav API — no compile error, no runtime warning.
3. Symptom of a missing include: `/api/portal/nav` returns `"categories":[]` (or the category that should contain the component is missing) even though the source file looks correct.

Same aggregation pattern applies to `widgets.cpp`, `screens.cpp`, `display_drivers.cpp`, `touch_drivers.cpp`, and `custom_fonts.cpp`.

## Multi-Page Architecture

- **Home** (`/` or `/home.html`): Operating mode, sensor, and display settings (Full Mode only)
- **Pads** (`/pads.html`): Visual pad editor with button editor dialog (Full Mode only)
- **Network** (`/network.html`): WiFi, device, and network configuration (both modes)
- **Firmware** (`/firmware.html`): Online update (GitHub Releases), manual upload, and factory reset (Full Mode only)
- Template fragments: `_header.html`, `_nav.html`, `_footer.html`, `_binding_help.html` used via `{{HEADER}}`, `{{NAV}}`, `{{FOOTER}}`, `{{BINDING_HELP}}` placeholders
- Build-time template replacement in `tools/minify-web-assets.sh`

## JavaScript Bundle System

- All JS source files are concatenated into a single `portal.js` asset at build time
- `portal.js.bundle` is the manifest listing every JS module in dependency order
- One `<script src="/portal.js">` per HTML page, one C++ handler (`handleJS`)
- Adding a new JS file: create it, add the filename to `portal.js.bundle`, run `./build.sh`
- No C++ changes needed when adding/removing JS modules
- Fragment pattern: large modules split into fragments listed before the main file in the manifest
- Build-time validation: manifest completeness check (missing files = hard error) and `node --check` syntax validation on every `.js` file
- `portal.js` is always the last entry (entry point)

## Portal Modes

- **Core Mode**: AP with captive portal (192.168.4.1) — WiFi not configured
  - Only Network page accessible (Home/Firmware redirect to Network)
  - Navigation tabs for Home/Firmware hidden via JavaScript
- **Full Mode**: Connected to WiFi — portal at device IP/hostname
  - All four pages accessible

## Responsive Design

- Container max-width: 900px
- 2-column grid on desktop (≥768px) using `.grid-2col` class
- Sections stack vertically on mobile (<768px)
- Network page: WiFi + Device Settings side-by-side, Network Config full-width
- Home page: Operating Mode settings, sensor and display configuration
- Pads page: Visual pad editor (when display enabled)

## REST API Design

- All endpoints under `/api/*` namespace
- Use semantic names: `/api/info` (device info), `/api/health` (real-time stats), `/api/config` (settings)
- Return JSON responses with proper HTTP status codes
- POST `/api/config` triggers device reboot (use `?no_reboot=1` to skip)
- Partial config updates: Backend only updates fields present in JSON request via `doc.containsKey()`

## Health Monitoring

- `/api/health` provides real-time metrics (CPU, memory, WiFi, temperature, uptime)
- CPU usage calculated via IDLE task: `100 - (idle_runtime/total_runtime * 100)`
- Temperature sensor with `SOC_TEMP_SENSOR_SUPPORTED` guards for cross-platform compatibility
- Update interval: 10s (compact widget), 5s (expanded widget)

## UI Design

- Minimalist card-based layout with gradient header
- 6 header badges with fixed widths and format placeholders:
  - Firmware version (`Firmware v-.-.-` → `Firmware v0.0.1`) — 140px
  - Chip info (`--- rev -` → `ESP32-C6 rev 2`) — 140px
  - CPU cores (`- Core` → `1 Core`) — 75px
  - CPU frequency (`--- MHz` → `160 MHz`) — 85px
  - Flash size (`-- MB Flash` → `8 MB Flash`) — 110px
  - PSRAM status (`No PSRAM` → `No PSRAM` or `2 MB PSRAM`) — 105px
- Floating health widget with compact/expanded views
- Breathing animation on status updates
- Tabbed navigation with active page highlighting
