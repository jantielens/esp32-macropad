#pragma once

// Register the "brew" binding scheme with the binding template engine.
// Provides [brew:key] and [brew:key;format] tokens for brew state machine data.
//
// Keys: weight, flow_rate, timer, stage, active, template, dose, water, ratio,
//       instruction, next_label, display_name,
//       stage_weight_*, stage_time_*, stage_flow_*,
//       stages_json, summary_json,
//       template_count, tpl_N_name, tpl_N_display_name, tpl_N_description, tpl_N_stages
//
// Call once during setup(), after binding_template_init() and brew_manager_init().
void brew_binding_init();
