#include "component_registry.h"
static ComponentDef fonts_component = {
	"fonts", "pads", "Fonts", 40,
	nullptr, nullptr, nullptr, nullptr, nullptr, 0,
	"fonts", "/portal_fonts.js", nullptr
};
REGISTER_COMPONENT(fonts)