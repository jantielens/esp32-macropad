#include "binding_sources.h"
#if HAS_DISPLAY && (HAS_MQTT || HAS_HA_LIVE)
#include "pad_config.h"
#include "pad_binding.h"
#include "display_manager.h"
#include "fill_pattern.h"
#include "action_registry.h"

struct SourceContext { BindingSourceVisitor visitor; void* user; };

static void visit_action(const ButtonAction& action, SourceContext& context) {
    action_type_visit_bindable_fields(action_type_find(action.type),
        const_cast<ButtonAction&>(action),
        [](char* field, size_t, bool, void* argument) {
            auto& source = *static_cast<SourceContext*>(argument);
            source.visitor(field, source.user);
            return true;
        }, &context);
}

void binding_sources_visit(BindingSourceVisitor visitor, void* user) {
    bool did_lock = false;
    display_manager_lock_if_needed(&did_lock);
    const PadBinding* previous = nullptr;
    uint8_t previous_count = 0;
    pad_binding_get_bindings(&previous, &previous_count);
    SourceContext context{visitor, user};
    for (uint8_t index = 0; index < MAX_PADS; ++index) {
        const PadConfig* pad = pad_config_acquire(index);
        if (!pad) continue;
        pad_binding_set_page(pad);
        visitor(pad->bg_color, user);
        for (uint8_t a = 0; a < pad->pad_action_count; ++a) visit_action(pad->pad_actions[a], context);
        for (uint8_t b = 0; b < pad->button_count; ++b) {
            const auto& button = pad->buttons[b];
            const char* fields[] = {button.label_top, button.label_center, button.label_bottom,
                button.bg_color, button.fg_color, button.border_color, button.border_width,
                button.corner_radius, button.btn_state, button.label_top_color_bind,
                button.label_center_color_bind, button.label_bottom_color_bind};
            for (const char* field : fields) visitor(field, user);
            if (button.fill_style)
                for (unsigned i = 0; i < 7; ++i) visitor(button.fill_style->color(i), user);
            for (uint8_t i = 0; i < MAX_WIDGET_BINDINGS; ++i) visitor(button.widget.data_binding[i], user);
            for (uint8_t i = 0; i < button.action_count; ++i) visit_action(button.actions[i], context);
            for (uint8_t i = 0; i < button.lp_action_count; ++i) visit_action(button.lp_actions[i], context);
        }
        pad_config_release(pad);
    }
    pad_binding_set_bindings(previous, previous_count);
    display_manager_unlock_if_needed(did_lock);
}
#endif
