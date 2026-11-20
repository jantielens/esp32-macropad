#pragma once

void ha_binding_init();

// Discover live entities through guarded expression and effective-pad aliases.
void ha_binding_collect_entities(const char* templ,
                                 void (*visitor)(const char*, void*), void* context);
