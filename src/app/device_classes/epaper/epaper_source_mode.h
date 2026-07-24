#ifndef EPAPER_SOURCE_MODE_H
#define EPAPER_SOURCE_MODE_H

#include <stdint.h>

enum class EpaperImageSourceMode : uint8_t {
		SlotImages = 0,
		DisplayAssignments = 1,
};

enum class EpaperSourceConfigError : uint8_t {
		None = 0,
		MissingAssignmentSource,
		InvalidAssignmentInterval,
		MissingSlotSource,
};

bool epaper_source_uses_assignments(EpaperImageSourceMode mode);
bool epaper_source_advances_carousel(EpaperImageSourceMode mode, uint8_t carousel_count);
uint32_t epaper_source_refresh_interval(EpaperImageSourceMode mode,
		uint32_t assignment_interval_seconds, uint32_t slot_interval_seconds);
EpaperSourceConfigError epaper_source_config_error(EpaperImageSourceMode mode,
		bool assignment_source_present, uint32_t assignment_interval_seconds,
		bool slot_source_present, uint32_t maximum_interval_seconds);

#endif // EPAPER_SOURCE_MODE_H