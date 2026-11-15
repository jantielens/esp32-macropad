#pragma once

void display_manager_lock_if_needed(bool* did_lock);
void display_manager_unlock_if_needed(bool did_lock);
// Queue local binding invalidation; the display owner resolves it, never the caller.
void display_manager_request_binding_refresh();