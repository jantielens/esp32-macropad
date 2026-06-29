// ============================================================================
// mcp_tools_pads.cpp — MCP capability manifest (read-only).
//
// Aggregated into the build via mcp_components.cpp (arduino-cli only compiles
// .cpp in the sketch root). Gated HAS_MCP && HAS_DISPLAY. get_capabilities is a
// read tool — needs only the bearer token, no control/authoring permission.
//
// The widget LIST is generated from the live registry (widget_count/widget_at)
// so a new widget auto-appears with no edit here. Per-widget config FIELDS are
// a static schema below until widgets export their own field metadata.
// ============================================================================

#include "board_config.h"

#if HAS_MCP && HAS_DISPLAY

#include "mcp_tool_registry.h"
#include "pad_config.h"
#include "widgets/widget.h"

#include <ArduinoJson.h>
#include <string.h>

// ============================================================================
// Static widget config-field schema.
//
// TODO(widget authors): when adding a new widget type, add a "<type>" entry
// here with its config fields. The widget LIST is registry-generated, but
// per-field config metadata is not yet exported by the WidgetType vtable — this
// table is the single drift point. Phase 2+ replaces it with a registry export.
// ============================================================================
static void emit_widget_fields(const char* type, JsonObject w) {
    JsonArray f = w.createNestedArray("config_fields");
    auto add = [&](const char* n, const char* t, const char* note) {
        JsonObject o = f.createNestedObject();
        o["name"] = n; o["type"] = t; if (note && note[0]) o["note"] = note;
    };
    if (strcmp(type, "bar_chart") == 0 || strcmp(type, "bar") == 0) {
        add("min", "number", "scale min (binding ok)");
        add("max", "number", "scale max (binding ok)");
        add("color", "color", "fill color");
    } else if (strcmp(type, "gauge") == 0) {
        add("min", "number", "scale min"); add("max", "number", "scale max");
        add("units", "string", "displayed unit");
    } else if (strcmp(type, "sparkline") == 0) {
        add("window_secs", "number", "time window"); add("color", "color", "");
    } else if (strcmp(type, "table") == 0) {
        add("rows", "number", "max rows");
    } else if (strcmp(type, "list") == 0) {
        add("provider", "string", "list provider id, e.g. 'pads'");
    } else if (strcmp(type, "rocker") == 0 || strcmp(type, "numericrocker") == 0) {
        add("step", "number", ""); add("min", "number", ""); add("max", "number", "");
    }
    // data_binding[0..3] is common to all widgets (see ScreenButtonConfig.widget).
    add("data_binding", "binding[]", "primary + up to 3 extra binding templates");
}

static bool tool_get_capabilities(const JsonObject& args, JsonObject& result, String& err) {
    (void)args; (void)err;

    // --- Widget types (generated from the live registry) ---
    JsonArray widgets = result.createNestedArray("widgets");
    for (uint8_t i = 0; i < widget_count(); ++i) {
        const WidgetType* wt = widget_at(i);
        if (!wt) continue;
        JsonObject w = widgets.createNestedObject();
        w["type"] = wt->name;
        emit_widget_fields(wt->name, w);
    }

    // --- Grid limits ---
    JsonObject grid = result.createNestedObject("grid");
    grid["max_buttons"] = MAX_PAD_BUTTONS;
    grid["max_cols"] = MAX_GRID_COLS;
    grid["max_rows"] = MAX_GRID_ROWS;
    grid["max_pads"] = MAX_PADS;

    // --- Button schema ---
    JsonObject btn = result.createNestedObject("button");
    JsonArray bf = btn.createNestedArray("fields");
    const char* fields[] = {
        "col", "row", "col_span", "row_span",
        "label_top", "label_center", "label_bottom",
        "style_top", "style_center", "style_bottom",
        "bg_color", "fg_color", "border_color", "border_width", "corner_radius",
        "icon", "btn_state", "widget", "tap", "long_press"
    };
    for (const char* f : fields) bf.add(f);
    JsonArray states = btn.createNestedArray("btn_state");
    states.add("enabled"); states.add("disabled"); states.add("hidden");

    // --- LabelStyle DSL tokens ---
    JsonObject style = result.createNestedObject("label_style");
    style["format"] = "font:24;font_family:dseg7;font_upscale:1.4;align:right;y:-3;mode:scroll;color:#FF0000";
    JsonArray fam = style.createNestedArray("font_family");
    fam.add("dseg7"); fam.add("bebas"); fam.add("doto");
    JsonArray al = style.createNestedArray("align");
    al.add("left"); al.add("right"); al.add("center");
    JsonArray md = style.createNestedArray("mode");
    md.add("clip"); md.add("scroll"); md.add("dot"); md.add("wrap");

    // --- Action types + targets ---
    JsonArray actions = result.createNestedArray("action_types");
    const char* acts[] = {
        "screen->target", "mqtt->topic,payload", "back", "key->sequence",
        "ble_pair", "beep", "volume", "brightness", "timer", "sound",
        "notify", "system->system_command", "ha_service->entity_id,service"
    };
    for (const char* a : acts) actions.add(a);

    // --- Binding schemes (one example each) ---
    JsonObject b = result.createNestedObject("binding_schemes");
    b["mqtt"]   = "[mqtt:home/temp;$.value;%.1f]";
    b["time"]   = "[time:%H:%M]";
    b["expr"]   = "[expr:[mqtt:t]>20?\"hot\":\"ok\"]";
    b["pad"]    = "[pad:power]";   // pad-level named binding
    b["health"] = "[health:heap_free]";
    b["timer"]  = "[timer:1]";
    b["list"]   = "[list:pads.selected]";
    b["fallback"] = "[scheme:params|fallback]";
    result["named_bindings_note"] = "pad-level [pad:name] bindings declared in pad.bindings; template_pad inherits buttons into empty cells (rendered, not stored)";

    // --- Formats ---
    JsonObject fmt = result.createNestedObject("formats");
    fmt["color"] = "#RRGGBB";
    fmt["size"] = "integer pixels or binding template";
    return true;
}

static const McpTool s_tool_get_capabilities = {
    "get_capabilities",
    "Get the pad authoring manifest: widget types + config fields, button schema, label-style DSL, binding schemes (incl. [pad:name] + template_pad), grid limits, action targets, and color/size formats. Read-only.",
    "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
    tool_get_capabilities, true, false, false
};
REGISTER_MCP_TOOL(s_tool_get_capabilities);

#endif // HAS_MCP && HAS_DISPLAY
