#pragma once

#include "board_config.h"

#if HAS_DISPLAY
#include "pad_config.h"
struct DialogTemplates;
struct DialogAppearance;

// Thread-safe entry points. Creation and replacement are transactional under
// the display lock. Appearance is copied when the dialog is created.
bool device_dialog_notify(const char* message, uint32_t duration_ms);
bool device_dialog_confirm(const ConfirmPayload& content, uint32_t token);
bool device_dialog_test(bool confirmation, const DialogAppearance& appearance);
void device_dialog_close();
void device_dialog_dismiss_notification();
bool device_dialog_active();
uint32_t device_dialog_input_generation();
bool device_dialog_navigate(const char* command);
void device_dialog_loop();
void device_dialog_set_templates(const DialogTemplates& templates);
#endif
