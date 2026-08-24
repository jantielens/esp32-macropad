#include "board_config.h"

#if HAS_CAMERA

#if CAMERA_DRIVER == CAMERA_DRIVER_OV02C10_P4
#include "drivers/ov02c10_p4_driver.cpp"
#else
#error "No camera driver selected or unknown camera driver type"
#endif

#endif // HAS_CAMERA
