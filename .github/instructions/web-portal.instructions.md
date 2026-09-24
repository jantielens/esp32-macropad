---
description: "Web portal conventions for the shell, fragments, assets, component navigation, and REST API"
applyTo: "**/web_portal*, **/web/*.html, **/web/*.js, **/web/*.css, **/web/*.bundle, **/web_assets.h, **/config_manager.*, **/components/*_component.cpp, **/portal_components.cpp"
---

# Web Portal Conventions

## Component Registration (READ THIS BEFORE ADDING A NEW COMPONENT)

Arduino's build system only compiles `.cpp` files in the sketch root (`src/app/`). Files under `src/app/components/` are **not** compiled directly — they are `#include`-aggregated into `src/app/portal_components.cpp`.

**When you add a new `src/app/components/<name>_component.cpp`:**

1. Add `#include "components/<name>_component.cpp"` to `src/app/portal_components.cpp` under the correct feature-flag block (always-available, `HAS_DISPLAY`, `HAS_MQTT`, etc.).
2. Without this include, the component's `REGISTER_COMPONENT()` static initializer never runs. The component will be **silently absent** from the registry and the nav API — no compile error, no runtime warning.
3. Symptom of a missing include: `/api/portal/nav` returns `"categories":[]` (or the category that should contain the component is missing) even though the source file looks correct.
4. Set `ComponentDef.category` to an emitted navigation category: `device`, `display`, `camera`, `pads`, `actions`, `connectivity`, `audio`, `sensors`, or `firmware`. The navigation API silently omits registered components in any other category. A custom category is valid only when its board config defines it as `PORTAL_PRIMARY_CATEGORY`; use that category consistently for its components.

Same aggregation pattern applies to `widgets.cpp`, `screens.cpp`, `display_drivers.cpp`, `touch_drivers.cpp`, and `custom_fonts.cpp`.

## Shell And Fragments

* `/` serves `shell.html`; `portal_nav.js` loads navigation from `/api/portal/nav` and content from `/api/section/{id}` into `#content-pane`. The URL hash selects a fragment, and `init_<id>_fragment()` (hyphens replaced with underscores) initializes it when available.
* `/home.html`, `/pads.html`, `/network.html`, and `/firmware.html` are compatibility redirects to `/#welcome`, `/#pad-editor`, `/#wifi`, and `/#ota-update`, respectively. Do not add new standalone pages for portal sections.
* Sections are `*.fragment.html` assets, including device-class fragments under `src/app/device_classes/*/web/`. Register their components so the nav API advertises them; feature-gated components and assets only appear on supported builds.
* Underscore-prefixed HTML files such as `_binding_help.html`, `_widget_*.html`, `_style_help.html`, `_health_widget.html`, and `_reboot_overlay.html` are build-time template includes, not navigable sections. `tools/minify-web-assets.sh` invokes `tools/_render_html_template.py` to resolve their `{{...}}` placeholders.

## Asset Bundles

* `portal.js.bundle` lists JavaScript modules in dependency order, grouped by `# [chunk:NAME]` or feature-gated `# [chunk:NAME HAS_FLAG]` markers. The build emits compile-time-selected, pre-gzipped variants; `handlePortalJS` serves the matching combined `/portal.js` in one response. Keep dependencies before their consumers, and keep `portal.js` last in the shell chunk.
* `shell.html` loads `/portal-all.css` and `/portal.js` once. `portal-all.css.bundle` assembles the shared styles. Components may advertise `portal_script` and `portal_style` assets through the nav API; `portal_nav.js` loads those separately. Do not assume every script or stylesheet belongs in the shared bundle.
* For a shared JS module, add it to `portal.js.bundle` in the appropriate chunk. For a device-class module, place it in that class's `web/` directory and reference it from the manifest. The minifier checks missing manifest entries and JavaScript syntax. Run focused asset checks after edits; a firmware build follows the build verification policy.

## Portal Modes

* In AP mode, `/` returns the shell with HTTP 200 for captive-portal detection. `/api/portal/nav` selects the `setup` fragment as the primary landing target and emits only the Device category.
* Outside AP mode, `/api/portal/nav` emits registered, available components grouped by category. A board may promote a custom category and primary fragment using `PORTAL_PRIMARY_CATEGORY` and `PORTAL_PRIMARY_FRAGMENT`.

## Responsive Design

* Keep the shared header, navigation, and content pane in `shell.html`; section-specific layout belongs in its fragment and styles.
* Check desktop and mobile layouts when changing fragments or navigation. The shell uses a collapsible navigation control on smaller screens.

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
- The health widget polls every 5s by default; `portal_health.js` uses `health_poll_interval_ms` from device info when available.

## UI Design

* Use the shared header and health badge in `shell.html`, and the two-level, hash-aware navigation in `portal_nav.js`.
* Keep section-specific controls inside their fragments. Do not duplicate shell-level navigation, badges, or overlays in a fragment.
