#include "board_config.h"

#if IS_VOICE_ASSISTANT

#include "component_registry.h"

REGISTER_NAV_COMPONENT(voice, "voice", "voice", "Voice Assistant", 10, "voice")

#endif // IS_VOICE_ASSISTANT