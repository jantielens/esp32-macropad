#pragma once

// Register core binding schemes in their required initialization order.
// Device-class schemes register through their device-class setup hooks.
void binding_builtin_schemes_init();