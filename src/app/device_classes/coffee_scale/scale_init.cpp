#include "scale_init.h"

#if HAS_SCALE

// Phase 2 no-op skeleton. The file ports in its final shape so Phase 3/4 just
// edits in place (smaller, more obvious diff than introducing the file later).
//
// Legacy bodies (from feature/coffee-scale) that Phase 4 will re-introduce:
//   scale_subsystem_init_storage(): brew_log_init();
//   scale_subsystem_init():         scale_binding_init();
//                                   brew_templates_init();
//                                   brew_manager_init();
//                                   brew_binding_init();

void scale_subsystem_init_storage() {
    // TODO Phase 4: brew_log_init();
}

void scale_subsystem_init() {
    // TODO Phase 4: scale_binding_init();
    // TODO Phase 4: brew_templates_init();
    // TODO Phase 4: brew_manager_init();
    // TODO Phase 4: brew_binding_init();
}

#endif // HAS_SCALE
