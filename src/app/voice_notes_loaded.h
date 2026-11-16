#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES && HAS_DISPLAY && HAS_PSRAM
#include "binding_template.h"

class LoadedDataSnapshot;
bool voice_notes_load_request(const char* id);
void voice_notes_loaded_loop();
// The caller owns the returned reference and must release it.
LoadedDataSnapshot* voice_notes_loaded_acquire();
BindingResolverStatus voice_notes_loaded_resolve(const char* key, char* out, size_t size);
#endif
