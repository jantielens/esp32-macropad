#include "board_config.h"

#if HAS_EPAPER

#include "device_classes/epaper/epaper_config.h"
#include "device_classes/epaper/epaper_sd_cache.h"
#include "log_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

namespace {

constexpr const char* kAssignmentNvsNamespace = "device_cfg";
constexpr const char* kAssignmentStateKey = "ep_assign";

struct Assignment {
		uint32_t revision;
		char image_key[17];
		uint32_t content_crc32;
		char format[8];
};

void key_to_hex(const uint8_t key[8], char out[17]) {
		static const char digits[] = "0123456789abcdef";
		for (size_t i = 0; i < 8; ++i) {
			out[i * 2] = digits[key[i] >> 4];
			out[i * 2 + 1] = digits[key[i] & 0x0F];
		}
		out[16] = '\0';
}

bool hex_to_key(const char* value, uint8_t out[8]) {
		if (!value || strlen(value) != 16) return false;
		for (size_t i = 0; i < 8; ++i) {
			char pair[3] = {value[i * 2], value[i * 2 + 1], '\0'};
			char* end = nullptr;
			const unsigned long parsed = strtoul(pair, &end, 16);
			if (!end || *end != '\0') return false;
			out[i] = (uint8_t)parsed;
		}
		return true;
}

bool load_state(EpaperAssignmentState* state) {
		memset(state, 0, sizeof(*state));
		Preferences prefs;
		if (!prefs.begin(kAssignmentNvsNamespace, true)) return false;
		const bool ok = prefs.getBytesLength(kAssignmentStateKey) == sizeof(*state) &&
			prefs.getBytes(kAssignmentStateKey, state, sizeof(*state)) == sizeof(*state);
		prefs.end();
		return ok;
}

void persist_state(const EpaperAssignmentState& state) {
		EpaperAssignmentState existing = {};
		if (load_state(&existing) && memcmp(&existing, &state, sizeof(state)) == 0) return;
		Preferences prefs;
		if (!prefs.begin(kAssignmentNvsNamespace, false)) {
			LOGW("Epaper", "Assignment state NVS open failed");
			return;
		}
		prefs.putBytes(kAssignmentStateKey, &state, sizeof(state));
		prefs.end();
}

bool assignment_from_json(const String& body, Assignment* assignment) {
		StaticJsonDocument<512> doc;
		if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
		assignment->revision = doc["revision"] | 0U;
		strlcpy(assignment->image_key, doc["image_key"] | "", sizeof(assignment->image_key));
		assignment->content_crc32 = doc["content_crc32"] | 0U;
		strlcpy(assignment->format, doc["format"] | "", sizeof(assignment->format));
		uint8_t key[8];
		return assignment->revision != 0 && hex_to_key(assignment->image_key, key) &&
			(strcmp(assignment->format, "g16z") == 0 || strcmp(assignment->format, "jpeg") == 0);
}

int post_sync(const char* url, const EpaperAssignmentState& displayed,
		Assignment* assignment) {
		char displayed_key[17] = {0};
		if (displayed.revision) key_to_hex(displayed.image_key, displayed_key);
		StaticJsonDocument<128> request;
		request["last_displayed_revision"] = displayed.revision;
		request["image_key"] = displayed_key;
		String payload;
		serializeJson(request, payload);

		const bool https = strncmp(url, "https://", 8) == 0;
		HTTPClient http;
		WiFiClientSecure secure;
		WiFiClient plain;
		bool began = false;
		if (https) {
			secure.setInsecure();
			began = http.begin(secure, url);
		} else {
			began = http.begin(plain, url);
		}
		if (!began) return 0;
		http.setTimeout(8000);
		http.addHeader("Content-Type", "application/json");
		const int status = http.POST(payload);
		const String body = status == HTTP_CODE_OK ? http.getString() : String();
		http.end();
		delay(100);
		if (status == HTTP_CODE_OK && !assignment_from_json(body, assignment)) return 0;
		return status;
}

bool same_content(const EpaperAssignmentState& state, const Assignment& assignment) {
		uint8_t key[8];
		return assignment.content_crc32 != 0 && hex_to_key(assignment.image_key, key) &&
			memcmp(state.image_key, key, sizeof(key)) == 0 &&
			epaper_assignment_crc_allows_reuse(assignment.content_crc32,
				state.content_crc32);
}

EpaperAssignmentState accepted_state(const Assignment& assignment) {
		EpaperAssignmentState state = {};
		state.revision = assignment.revision;
		hex_to_key(assignment.image_key, state.image_key);
		state.content_crc32 = assignment.content_crc32;
		return state;
}

} // namespace

EpaperRefreshOutcome epaper_assignment_run(DeviceConfig* config, bool force) {
		EpaperRefreshOutcome failed = {EpaperRefreshResult::FailedFetch, 0, 0, 0, 0, 0};
		const uint32_t started = millis();
		char sync_url[384];
		if (!epaper_assignment_build_url(g_epaper_config.epaper_url,
			g_epaper_config.epaper_assignment_url, "sync", 0, sync_url, sizeof(sync_url))) {
			LOGW("Epaper", "Assignment disabled for non-/api/next carousel URL");
			return epaper_refresh_run(config, force);
		}

		EpaperAssignmentState displayed = {};
		load_state(&displayed);
		Assignment assignment = {};
		int status = post_sync(sync_url, displayed, &assignment);
		if (status == HTTP_CODE_CONFLICT) {
			EpaperAssignmentState empty = {};
			status = post_sync(sync_url, empty, &assignment);
		}

		if (status == HTTP_CODE_NO_CONTENT && !force) {
			return epaper_refresh_record_assignment_skip(displayed.content_crc32,
				millis() - started);
		}
		if (status == HTTP_CODE_OK && displayed.revision != 0 &&
			assignment.revision != displayed.revision &&
			!epaper_assignment_revision_newer(assignment.revision, displayed.revision)) {
			LOGW("Epaper", "Assignment sync returned a stale revision");
			status = HTTP_CODE_CONFLICT;
		}
		const bool sync_succeeded = status == HTTP_CODE_OK;
		const bool content_unchanged = sync_succeeded && same_content(displayed, assignment);
		const EpaperAssignmentRefreshAction refresh_action = epaper_assignment_refresh_action(
			force, sync_succeeded, displayed.revision != 0, content_unchanged);
		if (refresh_action == EpaperAssignmentRefreshAction::Fail) {
				failed.sidecar_http_status = status;
				failed.elapsed_ms = millis() - started;
				return failed;
		}
		if (refresh_action == EpaperAssignmentRefreshAction::UseAccepted) {
			assignment.revision = displayed.revision;
			key_to_hex(displayed.image_key, assignment.image_key);
			assignment.content_crc32 = displayed.content_crc32;
			assignment.format[0] = '\0';
		}

		if (refresh_action == EpaperAssignmentRefreshAction::SkipUnchanged) {
			const EpaperAssignmentState accepted = accepted_state(assignment);
			persist_state(accepted);
			Assignment successor = {};
			post_sync(sync_url, accepted, &successor);
			return epaper_refresh_record_assignment_skip(assignment.content_crc32,
				millis() - started);
		}

		char image_url[384];
		if (!epaper_assignment_build_url(g_epaper_config.epaper_url,
			g_epaper_config.epaper_assignment_url, "image", assignment.revision,
			image_url, sizeof(image_url))) {
			failed.elapsed_ms = millis() - started;
			return failed;
		}
		epaper_sd_cache_set_assignment_context(assignment.image_key,
			assignment.content_crc32, assignment.format);
		epaper_assignment_expect_transport_crc(assignment.content_crc32);
		EpaperRefreshOutcome outcome = epaper_refresh_show_url(config, image_url);
		epaper_assignment_expect_transport_crc(0);
		epaper_sd_cache_set_assignment_context(nullptr, 0, nullptr);
		if (outcome.result == EpaperRefreshResult::Updated) {
			const EpaperAssignmentState accepted = accepted_state(assignment);
			persist_state(accepted);
			Assignment successor = {};
			post_sync(sync_url, accepted, &successor);
		}
		return outcome;
}

#endif // HAS_EPAPER
