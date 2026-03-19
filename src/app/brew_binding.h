#pragma once

// Register the "brew" binding scheme with the binding template engine.
// Provides [brew:key] and [brew:key;format] tokens for brew state machine data.
//
// Keys: weight, flow_rate, timer, phase, active
//
// Call once during setup(), after binding_template_init() and brew_manager_init().
void brew_binding_init();
