#include "epaper_timing.h"

#if HAS_EPAPER

#include <esp_attr.h>
#ifdef ARDUINO
#include <esp_system.h>
#endif

// RTC-retained. Survives deep sleep; zeroed on cold boot.
RTC_DATA_ATTR EpaperTimingBudget epaper_timing_last = {};
RTC_DATA_ATTR static uint32_t s_epaper_wake_id = 0;
RTC_DATA_ATTR static EpaperPreviousDelivery s_epaper_previous_delivery = {};

struct EpaperWakeJournal {
		uint32_t magic;
		uint64_t session_id;
		uint8_t head;
		uint8_t count;
		uint32_t dropped_count;
		EpaperWakeRecord records[EPAPER_WAKE_JOURNAL_CAPACITY];
};

constexpr uint32_t kEpaperWakeJournalMagic = 0x4550574a;
RTC_DATA_ATTR static EpaperWakeJournal s_epaper_wake_journal = {};

static uint64_t next_session_id() {
#ifdef ARDUINO
		return ((uint64_t)esp_random() << 32) | esp_random();
#else
		static uint64_t host_session_id = 0;
		return ++host_session_id;
#endif
}

static void ensure_wake_journal() {
		if (s_epaper_wake_journal.magic == kEpaperWakeJournalMagic) return;
		s_epaper_wake_journal = {};
		s_epaper_previous_delivery = {};
		s_epaper_wake_journal.magic = kEpaperWakeJournalMagic;
		s_epaper_wake_journal.session_id = next_session_id();
}

static EpaperWakeRecord* current_wake_record() {
		if (s_epaper_wake_journal.count == 0) return nullptr;
		const uint8_t index = (uint8_t)((s_epaper_wake_journal.head +
				s_epaper_wake_journal.count - 1) % EPAPER_WAKE_JOURNAL_CAPACITY);
		return &s_epaper_wake_journal.records[index];
}

uint32_t epaper_timing_begin_wake(EpaperWakeReason wake_reason) {
		ensure_wake_journal();
		EpaperWakeRecord* previous = current_wake_record();
		if (previous && previous->result == EpaperWakeResult::InProgress) {
			previous->result = EpaperWakeResult::Interrupted;
		}

		const uint32_t next_wake_id = ++s_epaper_wake_id;
		epaper_timing_last = {};
		epaper_timing_last.session_id = s_epaper_wake_journal.session_id;
		epaper_timing_last.wake_id = next_wake_id;

		if (s_epaper_wake_journal.count == EPAPER_WAKE_JOURNAL_CAPACITY) {
			s_epaper_wake_journal.head = (uint8_t)((s_epaper_wake_journal.head + 1) %
					EPAPER_WAKE_JOURNAL_CAPACITY);
			--s_epaper_wake_journal.count;
			++s_epaper_wake_journal.dropped_count;
		}
		const uint8_t index = (uint8_t)((s_epaper_wake_journal.head +
				s_epaper_wake_journal.count) % EPAPER_WAKE_JOURNAL_CAPACITY);
		EpaperWakeRecord& record = s_epaper_wake_journal.records[index];
		record = {};
		record.timing = epaper_timing_last;
		record.previous_delivery = s_epaper_previous_delivery;
		record.wake_reason = wake_reason;
		record.last_stage = EpaperWakeStage::Boot;
		record.result = EpaperWakeResult::InProgress;
		record.refresh_result = EpaperWakeResult::InProgress;
		++s_epaper_wake_journal.count;
		return next_wake_id;
}

uint64_t epaper_wake_journal_session_id() {
		ensure_wake_journal();
		return s_epaper_wake_journal.session_id;
}

void epaper_wake_journal_checkpoint(EpaperWakeStage stage) {
		ensure_wake_journal();
		EpaperWakeRecord* record = current_wake_record();
		if (!record) return;
		record->timing = epaper_timing_last;
		record->last_stage = stage;
}

void epaper_wake_journal_finalize(EpaperWakeResult result, uint16_t battery_mv,
															int16_t sidecar_http_status) {
		ensure_wake_journal();
		EpaperWakeRecord* record = current_wake_record();
		if (!record || record->result != EpaperWakeResult::InProgress) return;
		record->timing = epaper_timing_last;
		record->battery_mv = battery_mv;
		record->sidecar_http_status = sidecar_http_status;
		record->result = result;
		record->refresh_result = result;
}

void epaper_wake_journal_mark_delivery_failed(EpaperWakeResult result) {
		ensure_wake_journal();
		EpaperWakeRecord* record = current_wake_record();
		if (!record || record->result == EpaperWakeResult::InProgress) return;
		record->result = result;
}

void epaper_wake_journal_complete_delivery(uint32_t mqtt_connect_ms,
																	 uint32_t mqtt_publish_ms, uint32_t total_active_ms) {
		ensure_wake_journal();
		s_epaper_previous_delivery.session_id = epaper_timing_last.session_id;
		s_epaper_previous_delivery.wake_id = epaper_timing_last.wake_id;
		s_epaper_previous_delivery.mqtt_connect_ms = mqtt_connect_ms;
		s_epaper_previous_delivery.mqtt_publish_ms = mqtt_publish_ms;
		s_epaper_previous_delivery.total_active_ms = total_active_ms;
		s_epaper_previous_delivery.valid = true;
}

bool epaper_wake_journal_peek(EpaperWakeRecord* record) {
		ensure_wake_journal();
		if (!record || s_epaper_wake_journal.count == 0) return false;
		*record = s_epaper_wake_journal.records[s_epaper_wake_journal.head];
		return true;
}

void epaper_wake_journal_remove_oldest() {
		ensure_wake_journal();
		if (s_epaper_wake_journal.count == 0) return;
		s_epaper_wake_journal.head = (uint8_t)((s_epaper_wake_journal.head + 1) %
				EPAPER_WAKE_JOURNAL_CAPACITY);
		--s_epaper_wake_journal.count;
}

uint32_t epaper_wake_journal_dropped_count() {
		ensure_wake_journal();
		return s_epaper_wake_journal.dropped_count;
}

void epaper_wake_journal_clear_dropped_count() {
		ensure_wake_journal();
		s_epaper_wake_journal.dropped_count = 0;
}

void epaper_timing_reset_draw_steps() {
		epaper_timing_last.resolve_ms = 0;
		epaper_timing_last.fetch_ms = 0;
		epaper_timing_last.draw_ms = 0;
		epaper_timing_last.image_from_cache = 0;
}

void epaper_timing_set_resolve_ms(uint32_t ms) {
		epaper_timing_last.resolve_ms = ms;
}

void epaper_timing_set_fetch(uint32_t ms, bool from_cache) {
		epaper_timing_last.fetch_ms = ms;
		epaper_timing_last.image_from_cache = from_cache ? 1 : 0;
}

void epaper_timing_set_draw_ms(uint32_t ms) {
		epaper_timing_last.draw_ms = ms;
}

#endif // HAS_EPAPER
