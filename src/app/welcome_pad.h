#ifndef WELCOME_PAD_H
#define WELCOME_PAD_H

#include "board_config.h"

#if HAS_DISPLAY && HAS_FILESYSTEM && !IS_EPAPER_FRAME
// Boot-only, before portal startup. Configured devices return without I/O or allocation.
bool welcome_pad_provision(bool already_configured);
#endif

#endif
