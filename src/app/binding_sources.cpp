#include "binding_sources.h"
#if HAS_DISPLAY && (HAS_MQTT || HAS_HA_LIVE)
#include "pad_config.h"
#include "pad_binding.h"
#include "display_manager_lock.h"
#include "fill_pattern.h"
#include "action_registry.h"
#include "widgets/widget_registry.h"

struct SourceContext { BindingSourceVisitor visitor; void* user; };

static void visit_action(const ButtonAction& action, SourceContext& context) {
    action_visit_templates(action,
        [](const char* field, void* argument) {
            auto& source = *static_cast<SourceContext*>(argument);
            source.visitor(field, source.user);
        }, &context);
}

void binding_sources_visit(BindingSourceVisitor visitor, void* user) {
    bool did_lock = false;
    display_manager_lock_if_needed(&did_lock);
    SourceContext context{visitor, user};
    for (uint8_t index = 0; index < MAX_PADS; ++index) {
        const PadConfig* pad = pad_config_acquire(index);
        if (!pad) continue;
        PadBindingScope binding_scope(pad->bindings, pad->binding_count);
        visitor(pad->bg_color, user);
        for (uint8_t a = 0; a < pad->pad_action_count; ++a) visit_action(pad->pad_actions[a], context);
        for (uint8_t b = 0; b < pad->button_count; ++b) {
            const auto& button = pad->buttons[b];
            const char* fields[] = {button.label_top, button.label_center, button.label_bottom,
                button.bg_color, button.fg_color, button.icon_color, button.icon_badge_color, button.border_color, button.border_width,
                button.corner_radius, button.btn_state, button.label_top_color_bind,
                button.label_center_color_bind, button.label_bottom_color_bind,
                button.bg_image_url};
            for (const char* field : fields) visitor(field, user);
            if (button.fill_style)
                for (unsigned i = 0; i < 7; ++i) visitor(button.fill_style->color(i), user);
            for (uint8_t i = 0; i < MAX_WIDGET_BINDINGS; ++i) visitor(button.widget.data_binding[i], user);
            const WidgetType* type = widget_find(button.widget.type);
            if (type && type->visitResources) {
                type->visitResources(button.widget.data,
                    [](WidgetResourceKind kind, const char*, uint8_t, const char* value, void* opaque) {
                        if (kind == WidgetResourceKind::Binding) {
                            auto& source = *static_cast<SourceContext*>(opaque);
                            source.visitor(value, source.user);
                        }
                    }, &context);
            }
            for (uint8_t i = 0; i < button.action_count; ++i) visit_action(button.actions[i], context);
            for (uint8_t i = 0; i < button.lp_action_count; ++i) visit_action(button.lp_actions[i], context);
        }
        pad_config_release(pad);
    }
    display_manager_unlock_if_needed(did_lock);
}
#endif
