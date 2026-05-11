#pragma once

// Register the "list" binding scheme with the binding template engine.
// Provides [list:provider_id.selected] tokens that resolve to the last-selected
// item ID for a given provider. Each provider's state is independent.
//
// Keys:
//   [list:pads.selected]           — ID of the last tapped item in the "pads" list
//   [list:shutter_tests.selected]  — ID of the last tapped item in "shutter_tests"
//
// Provider IDs up to 14 chars fit within CONFIG_SCREEN_ID_MAX_LEN (32) when used
// as [list:provider_id.selected] in screen_id fields.
//
// Call once during setup().
void list_binding_init();

// Set the selected item ID for a specific provider. Called by the list widget
// click handler immediately before action_dispatch(). Must be called from the
// LVGL task. Creates the binding entry lazily on first call per provider_id.
void list_binding_set_selected(const char* provider_id, const char* item_id);
