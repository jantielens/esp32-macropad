#include "board_config.h"

#if IS_HEART_MONITOR
#include "device_class.h"
#include "heart_capture.cpp"
#include "heart_bpm.cpp"
#include "heart_report.cpp"
#include "heart_catalog.cpp"
#include "heart_recorder.cpp"
#include "heart_actions.cpp"
#include "heart_binding.cpp"

namespace heart_monitor {
namespace {
void setup_late(DeviceConfig*, PowerMode) {
    heart_recorder_init();
    heart_binding_init();
}
const DeviceClass device_class = {
    "heart_monitor", PowerMode::AlwaysOn,
    nullptr, setup_late, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};
}
}

void heart_monitor_device_class_register() {
    device_class_register(&heart_monitor::device_class);
}
#endif