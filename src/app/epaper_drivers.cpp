// E-paper driver aggregation unit.
//
// arduino-cli only compiles .cpp files in the sketch root, so per-board driver
// implementations under src/app/drivers/ are #include-aggregated here. Each
// driver guards itself with its own board-specific macro so only one body is
// ever active per build.

#include "board_config.h"

#if HAS_EPAPER

#include "drivers/inkplate5v2_driver.cpp"

#endif // HAS_EPAPER
