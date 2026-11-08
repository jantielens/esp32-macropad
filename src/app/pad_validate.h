#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <ArduinoJson.h>
#include <string.h>

struct PadValidationGeometry {
	uint16_t display_width;
	uint16_t display_height;
	uint16_t spacing_px;
	uint16_t inset_left_px;
	uint16_t inset_right_px;
	uint16_t inset_top_px;
	uint16_t inset_bottom_px;
	uint16_t pixel_shift_distance_px;
};

// ============================================================================
// Pad validation — single source of truth for pad JSON validity.
//
// Shared by every write path so a bad pad can never be persisted (both
// pad_config_save_raw and the MCP write tools truncate/store in place):
//   - MCP: validate_pad (dry-run) + every set_button/set_buttons/set_pad write
//   - Web portal: the POST /api/pad save handler
//
// Checks: grid bounds, span overflow, template_pad range, pad-level binding
// name/value limits (incl. the one-level [pad:] rule), per-button colors,
// widget types + widget config field caps, action arrays, and binding tokens
// (unknown scheme / bad health key / list provider / timer id) in any field.
//
// Returns nullptr on success, or a short human-readable error string. The
// returned pointer is either a string literal or a static buffer valid until
// the next call — validation is single-threaded (web task), so this is safe.
//
// tolerate_offgrid: when true, buttons whose col/row+span extend past the grid
// are accepted instead of rejected. The web portal sets this because it treats
// off-grid buttons as a feature (they are hidden when the grid shrinks and
// reappear when it grows back); MCP authoring leaves it false so a mis-placed
// button is caught up front. All other checks are identical for both callers.
// ============================================================================
const char* pad_validate(JsonObjectConst pad, bool tolerate_offgrid = false,
						 const PadValidationGeometry* geometry = nullptr);

inline bool pad_icon_source_id_valid(const char* source_id) {
    if (!source_id || strlen(source_id) != 26 || source_id[0] != 's' || source_id[1] != '_')
        return false;
    for (size_t index = 2; index < 26; ++index) {
        const char value = source_id[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;
    }
    return true;
}

inline bool pad_icon_catalog_kind_valid(const char* catalog_id, const char* kind) {
    if (!catalog_id || !kind) return false;
    const size_t length = strlen(catalog_id);
    if (!length || length > 128) return false;
    const char* name = nullptr;
    bool material = false;
    if (strncmp(catalog_id, "material-symbols:", 17) == 0) {
        name = catalog_id + 17;
        material = true;
    } else if (strncmp(catalog_id, "noto:", 5) == 0) {
        name = catalog_id + 5;
    } else {
        return false;
    }
    if (!name[0]) return false;
    for (const char* cursor = name; *cursor; ++cursor) {
        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '-')) return false;
    }
    return strcmp(kind, material ? "mono" : "color") == 0;
}

inline const char* pad_validate_icon_metadata(JsonObjectConst icon) {
    const char* source_id = icon["source_id"] | "";
    if (!pad_icon_source_id_valid(source_id))
        return "icon source_id must be s_ followed by 24 lowercase hexadecimal digits";
    const char* kind = icon["kind"] | "";
    const bool has_catalog = icon.containsKey("catalog_id");
    if (icon.size() != (has_catalog ? 3 : 2) ||
        (has_catalog && !pad_icon_catalog_kind_valid(icon["catalog_id"] | "", kind)) ||
        (!has_catalog && strcmp(kind, "color") != 0))
        return "icon metadata must contain valid source_id, kind, and optional catalog_id";
    return nullptr;
}
uint32_t pad_icon_render_key(JsonObjectConst pad, JsonObjectConst button);
uint32_t pad_option_icon_render_key(JsonObjectConst pad, JsonObjectConst button, uint8_t slot);

#endif // HAS_DISPLAY
