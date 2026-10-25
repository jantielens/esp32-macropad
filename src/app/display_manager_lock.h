#pragma once

void display_manager_lock_if_needed(bool* did_lock);
void display_manager_unlock_if_needed(bool did_lock);