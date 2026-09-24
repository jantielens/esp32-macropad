---
description: "Use when adding NVS settings, /api/config fields, or web portal configuration controls. Covers storage, config API, fragment load/save, and field registration."
applyTo: "**/config_manager.*, **/web_portal_config.*, **/web/*.fragment.html, **/web/portal_core.js, **/web/portal_config.js, **/web/portal_fragment_init.js"
---

# Adding New Configuration Settings

When adding a setting, follow its storage, API, and fragment paths. See
`docs/dev/web-portal.md` for portal architecture and REST API details.

## 1. Backend: Configuration Storage

**Update `src/app/config_manager.h`:**

* Add a field to `DeviceConfig` and a maximum length constant for strings.
* Use the appropriate type for numeric or boolean settings.

**Update `src/app/config_manager.cpp`:**

* Add an NVS key and update `config_manager_load()` and
  `config_manager_save()` with a suitable default and matching get/put types.
* Add non-secret settings to `config_manager_print()` when useful; never log
  passwords or tokens.

## 2. Backend: Web API

**Update `src/app/web_portal_config.cpp`:**

* Add the field to `handleGetConfig()` and process it in `handlePostConfig()`.
  Guard updates with `doc.containsKey()` so omitted fields remain unchanged;
  validate and bound values according to the field type.
* For secrets, return an empty value plus a `_set` status flag; only replace
  the stored secret when a non-empty value is submitted.

## 3. Frontend: HTML Form

**Update the relevant `src/app/web/*.fragment.html`:**

* Give the input a `name` matching the API key; use the same `id` for loading
  registered fields. Set type, bounds/maxlength, labels, and help text as
  appropriate. String `maxlength` is at most the buffer size minus one.
* Place it in the relevant fragment with a save button; follow a neighboring
  fragment's layout and initialization pattern.

## 4. Frontend: JavaScript

**Update the shared frontend path:**

* For core settings, add the name to `saveFragmentConfig()`'s field list in
  `src/app/web/portal_fragment_init.js` and populate it in `loadConfig()` in
  `src/app/web/portal_config.js` (use the matching input, checkbox, or radio
  helper). Add validation in `validateConfig()` where needed.
* For feature or device-class fields, call `registerConfigFields([...])` from
  the feature's init/module before `loadConfig()` runs. It is defined in
  `src/app/web/portal_core.js`; both save and load use its registered names.
  Registered inputs need matching `id` and `name` and are populated directly
  from `/api/config`. Handle write-only secrets separately: leave the input
  empty and display the `_set` status.
* In `src/app/web/portal_fragment_init.js`, wire the fragment's save button
  via `initConfigFragment(saveBtnId, requiresReboot)` or its existing custom
  initializer. The shared save helper posts only fields present in the DOM to
  `/api/config?no_reboot=1`; choose the reboot flag for the setting's effect.

## 5. Usage in Application Code

Read the new field from the loaded `DeviceConfig` at the owning subsystem's
initialization or update point; handle settings that take effect only after a
reboot accordingly.

## Common Mistakes to Avoid

* Check a representative round trip before finishing. For `ha_url`,
  `config_manager_load()`/`config_manager_save()` persist the NVS key,
  `handleGetConfig()`/`handlePostConfig()` expose it, and
  `ha-discovery.fragment.html` supplies `id="ha_url" name="ha_url"`.
  `init_ha_discovery_fragment()` registers it before `initConfigFragment()`;
  `loadConfig()` populates it and `saveFragmentConfig()` posts it.
* Verify the mapping from NVS key to API field and keep the API name
  consistent across registration and DOM. Never expose secrets in GET
  responses or logs.
* For actual firmware/UI changes, run focused checks and follow
  [build verification](agent-guidelines.instructions.md); instruction-only
  edits do not need a firmware build.
