#ifndef VERSION_H
#define VERSION_H

#include "app/log_manager.h"

// Firmware version information
#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define VERSION_PATCH 57

// Build date (automatically set by compiler)
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// Convert version to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STRING TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH)
#define FIRMWARE_VERSION VERSION_STRING

// Function to print version information
inline void printVersionInfo() {
	LOGI("SYS", "Firmware Version");
	LOGI("SYS", "Version: %s", VERSION_STRING);
	LOGI("SYS", "Build Date: %s", BUILD_DATE);
	LOGI("SYS", "Build Time: %s", BUILD_TIME);
}

#endif // VERSION_H
