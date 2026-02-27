#include "board_config.h"

#if HAS_TOUCH

// Arduino build system only compiles .cpp files in the sketch root directory.
// This translation unit centralizes the required driver implementation includes
// so manager code stays focused on logic.

#if TOUCH_DRIVER == TOUCH_DRIVER_XPT2046
#include "drivers/xpt2046_driver.cpp"
#elif TOUCH_DRIVER == TOUCH_DRIVER_AXS15231B_I2C
#include "drivers/axs15231b_touch_driver.cpp"
#include "drivers/axs15231b/vendor/AXS15231B_touch.cpp"
#elif TOUCH_DRIVER == TOUCH_DRIVER_CST816S_WIRE
#include "drivers/wire_cst816s_touch_driver.cpp"
#elif TOUCH_DRIVER == TOUCH_DRIVER_GT911
#include "drivers/gt911_touch_driver.cpp"
#else
#error "No touch driver selected or unknown driver type"
#endif

#endif // HAS_TOUCH
