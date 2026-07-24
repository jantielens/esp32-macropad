#include "epaper_source_mode.h"

bool epaper_source_uses_assignments(EpaperImageSourceMode mode) {
		return mode == EpaperImageSourceMode::DisplayAssignments;
}

bool epaper_source_advances_carousel(EpaperImageSourceMode mode, uint8_t carousel_count) {
		return !epaper_source_uses_assignments(mode) && carousel_count > 0;
}

uint32_t epaper_source_refresh_interval(EpaperImageSourceMode mode,
		uint32_t assignment_interval_seconds, uint32_t slot_interval_seconds) {
		return epaper_source_uses_assignments(mode)
				? assignment_interval_seconds : slot_interval_seconds;
}

EpaperSourceConfigError epaper_source_config_error(EpaperImageSourceMode mode,
		bool assignment_source_present, uint32_t assignment_interval_seconds,
		bool slot_source_present, uint32_t maximum_interval_seconds) {
		if (epaper_source_uses_assignments(mode)) {
				if (!assignment_source_present) {
						return EpaperSourceConfigError::MissingAssignmentSource;
				}
				if (assignment_interval_seconds == 0 ||
						assignment_interval_seconds > maximum_interval_seconds) {
						return EpaperSourceConfigError::InvalidAssignmentInterval;
				}
				return EpaperSourceConfigError::None;
		}
		return slot_source_present
				? EpaperSourceConfigError::None
				: EpaperSourceConfigError::MissingSlotSource;
}