// E-paper driver aggregation unit.
//
// arduino-cli only compiles .cpp files in the sketch root, so per-board driver
// implementations under src/app/drivers/ are #include-aggregated here. Each
// driver guards itself with its own board-specific macro so only one body is
// ever active per build.

#include "board_config.h"

#if IS_EPAPER_FRAME

#include "device_classes/epaper_frame/drivers/inkplate_driver.cpp"
#include "device_classes/epaper_frame/drivers/reterminal_e1003_driver.cpp"

#endif // IS_EPAPER_FRAME
