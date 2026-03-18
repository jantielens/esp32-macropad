#pragma once

// Register the "scale" binding scheme with the binding template engine.
// Provides [scale:key] and [scale:key;format] tokens for HX711 scale data
// (weight, flow_rate, calibration_factor, offset, available).
// Call once during setup(), after binding_template_init().
void scale_binding_init();
