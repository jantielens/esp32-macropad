// Shutter Tester device-class aggregator.
//
// arduino-cli only compiles `.cpp` files in the sketch root. All shutter-tester
// translation units live under `device_classes/shutter_tester/` and are pulled
// into the build by `#include`-ing them here under the `IS_SHUTTER_TESTER` gate,
// mirroring the e-paper aggregation pattern (see `epaper_device_class.cpp`).
//
// Ripping out the shutter tester device class requires deleting this file,
// the `device_classes/shutter_tester/` folder, and the small number of
// aggregation hooks elsewhere (see PRD §Aggregation exceptions).

#include "board_config.h"

#if IS_SHUTTER_TESTER

// Core modules
#include "shutter_tester/shutter_adc.cpp"
#include "shutter_tester/shutter_capture.cpp"
#include "shutter_tester/shutter_measure.cpp"
#include "shutter_tester/shutter_curtain_stats.cpp"
#include "shutter_tester/shutter_session.cpp"
#include "shutter_tester/shutter_session_actions.cpp"
#include "shutter_tester/shutter_test_scripts.cpp"
#include "shutter_tester/shutter_binding.cpp"
#include "shutter_tester/shutter_align_binding.cpp"

// Web portal endpoints and list providers
#include "shutter_tester/web/portal_shutter_sessions.cpp"
#include "shutter_tester/web/portal_shutter_tests.cpp"
#include "shutter_tester/web/list_provider_shutter_tests.cpp"

#endif // IS_SHUTTER_TESTER
