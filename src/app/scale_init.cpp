#include "scale_init.h"

#if HAS_SCALE

#include "brew_binding.h"
#include "brew_log.h"
#include "brew_manager.h"
#include "brew_templates.h"
#include "scale_binding.h"

void scale_subsystem_init_storage() {
    brew_log_init();
}

void scale_subsystem_init() {
    scale_binding_init();
    brew_templates_init();
    brew_manager_init();
    brew_binding_init();
}

#endif // HAS_SCALE
