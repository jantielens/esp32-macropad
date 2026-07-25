#include "device_classes/epaper/epaper_assignment_logic.h"

#include <stdio.h>
#include <string.h>

namespace {

uint32_t s_expected_transport_crc = 0;

bool hex_digit(char value, uint8_t* decoded) {
	if (value >= '0' && value <= '9') *decoded = (uint8_t)(value - '0');
	else if (value >= 'a' && value <= 'f') *decoded = (uint8_t)(value - 'a' + 10);
	else if (value >= 'A' && value <= 'F') *decoded = (uint8_t)(value - 'A' + 10);
	else return false;
	return true;
}

bool query_value(const char* query, const char* name, char* out, size_t out_size) {
	if (!query || !name || !out || out_size == 0) return false;
	const size_t name_length = strlen(name);
	for (const char* field = query; *field; ) {
		if (*field == '?' || *field == '&') ++field;
		const char* end = strchr(field, '&');
		if (!end) end = field + strlen(field);
		const char* equals = (const char*)memchr(field, '=', (size_t)(end - field));
		if (equals && (size_t)(equals - field) == name_length &&
				memcmp(field, name, name_length) == 0) {
			size_t written = 0;
			for (const char* input = equals + 1; input < end; ++input) {
				char decoded = *input == '+' ? ' ' : *input;
				if (*input == '%' && input + 2 < end) {
					uint8_t high = 0, low = 0;
					if (!hex_digit(input[1], &high) || !hex_digit(input[2], &low)) {
						return false;
					}
					decoded = (char)((high << 4) | low);
					input += 2;
				}
				if (written + 1 >= out_size) return false;
				out[written++] = decoded;
			}
			out[written] = '\0';
			return written != 0;
		}
		field = end;
	}
	return false;
}

} // namespace

EpaperAssignmentRefreshAction epaper_assignment_refresh_action(bool force,
		bool sync_succeeded, bool accepted_state_present, bool content_unchanged) {
		if (!sync_succeeded) {
				return force && accepted_state_present
						? EpaperAssignmentRefreshAction::UseAccepted
						: EpaperAssignmentRefreshAction::Fail;
		}
		if (!force && accepted_state_present && content_unchanged) {
				return EpaperAssignmentRefreshAction::SkipUnchanged;
		}
		return EpaperAssignmentRefreshAction::UseSynchronized;
}

EpaperAssignmentTransportAction epaper_assignment_transport_action(bool cache_valid) {
		return cache_valid ? EpaperAssignmentTransportAction::UseCache
				: EpaperAssignmentTransportAction::Download;
}

bool epaper_assignment_revision_newer(uint32_t left, uint32_t right) {
		const uint32_t delta = left - right;
		return delta != 0 && delta < 0x80000000U;
}

bool epaper_assignment_crc_allows_reuse(uint32_t expected, uint32_t displayed) {
		return expected != 0 && expected == displayed;
}

uint32_t epaper_assignment_transport_crc32(const uint8_t* data, size_t len) {
		uint32_t crc = 0xFFFFFFFFU;
		for (size_t i = 0; i < len; ++i) {
			crc ^= data[i];
			for (uint8_t bit = 0; bit < 8; ++bit) {
				crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
			}
		}
		return crc ^ 0xFFFFFFFFU;
}

void epaper_assignment_expect_transport_crc(uint32_t expected) {
		s_expected_transport_crc = expected;
}

bool epaper_assignment_validate_transport(const uint8_t* data, size_t len) {
		return s_expected_transport_crc == 0 ||
			epaper_assignment_transport_crc32(data, len) == s_expected_transport_crc;
}

bool epaper_assignment_build_url(const char* carousel_url, const char* override_url,
		const char* action, uint32_t revision, char* out, size_t out_size) {
		if (!carousel_url || !action || !out || out_size == 0) return false;
		const bool has_override = override_url && override_url[0];
		const char* source = has_override ? override_url : carousel_url;
		const char* query = strchr(source, '?');
		if (!query && has_override) query = strchr(carousel_url, '?');

		const size_t source_length = query && query >= source
			? (size_t)(query - source) : strlen(source);
		const char* next_path = strstr(source, "/api/next");
		const char* assignment_path = strstr(source, "/api/assignment");
		size_t base_length = source_length;
		if (next_path && (size_t)(next_path - source) < source_length) {
			base_length = (size_t)(next_path - source);
		} else if (assignment_path && (size_t)(assignment_path - source) < source_length) {
			base_length = (size_t)(assignment_path - source);
		} else if (!has_override) {
			return false;
		} else {
			while (base_length && source[base_length - 1] == '/') --base_length;
		}

		const char* query_text = query ? query : "";
		const char* revision_separator = query_text[0] ? "&revision=" : "?revision=";
		const int written = revision
			? snprintf(out, out_size, "%.*s/api/assignment/%s%s%s%u",
				(int)base_length, source, action, query_text, revision_separator,
				(unsigned)revision)
			: snprintf(out, out_size, "%.*s/api/assignment/%s%s",
				(int)base_length, source, action, query_text);
		return written >= 0 && (size_t)written < out_size;
}

	bool epaper_assignment_extract_credentials(const char* source_url,
			char* device_id, size_t device_id_size, char* api_key,
			size_t api_key_size) {
		if (!source_url) return false;
		const char* query = strchr(source_url, '?');
		if (!query) return false;
		return query_value(query, "device_id", device_id, device_id_size) &&
			query_value(query, "key", api_key, api_key_size);
	}
