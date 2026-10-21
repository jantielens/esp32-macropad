// Display Manager - LVGL render task, present task, and flush callback
#include "board_config.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"

#include "data_stream.h"
#include "ha_live.h"
#include "action_list.h"
#include "device_dialog.h"
#include "pad_config.h"
#include "screen_saver_manager.h"
#if HAS_TOUCH
#include "touch_manager.h"
#endif
#include "timer_engine.h"
#if HAS_DISPLAY_DIAGNOSTICS && HAS_TTF_FONTS && HAS_FREETYPE_FONTS
#include "freetype_mono_font.h"
#endif
#if HAS_MUSIC_ANALYSIS
#include "music_analysis.h"
#endif

#include <esp_timer.h>

extern portMUX_TYPE g_splash_status_mux;
extern portMUX_TYPE g_perf_mux;
extern DisplayPerfStats g_perf;
extern DisplayPerfWindow g_perf_window;
extern bool g_perf_ready;

DisplayTaskDispatchResult DisplayManager::dispatch(
		DisplayTaskExec exec, DisplayTaskCleanup cleanup,
		const void* ctx, size_t ctxLen,
		uint32_t timeoutMs, bool* outOk, char* outMsg, size_t outMsgLen) {
		return static_cast<DisplayTaskDispatchResult>(displayJobSlot().dispatch(
				exec, cleanup, ctx, ctxLen, timeoutMs, isInLvglTask(),
				xPortInIsrContext(), outOk, outMsg, outMsgLen));
}

void DisplayManager::processDisplayJob() {
		displayJobSlot().drain();
}

static void sample_lvgl_stack(const void*, bool* ok, char* msg, size_t msg_len) {
		const uint32_t minimum_free_bytes = uxTaskGetStackHighWaterMark(nullptr);
		const int written = snprintf(msg, msg_len, "%lu", static_cast<unsigned long>(minimum_free_bytes));
		*ok = written > 0 && static_cast<size_t>(written) < msg_len;
}

bool display_manager_get_stack_watermark(uint32_t* minimum_free_bytes, uint32_t* configured_bytes) {
		if (!minimum_free_bytes || !configured_bytes || ota_activity_is_active()) return false;
		bool ok = false;
		char result[16] = {};
		const DisplayTaskDispatchResult status = display_manager_dispatch(
				sample_lvgl_stack, nullptr, nullptr, 0, 250, &ok, result, sizeof(result));
		if (status != DISPLAY_TASK_DISPATCH_OK || !ok) return false;
		char* end = nullptr;
		const unsigned long value = strtoul(result, &end, 10);
		if (end == result || *end != '\0' || value > UINT32_MAX) return false;
		*minimum_free_bytes = static_cast<uint32_t>(value);
		*configured_bytes = DisplayManager::stackSizeBytes();
		return true;
}

// Definition for the async-flush-busy flag declared in display_driver.h.
// Set true by async drivers (e.g. MIPI-DSI DMA2D) while a pixel transfer
// is in flight; cleared from the completion ISR.
volatile bool g_displayFlushBusy = false;

#if HAS_DISPLAY_DIAGNOSTICS
void DisplayManager::displayEventCallback(lv_event_t* event) {
		auto* mgr = static_cast<DisplayManager*>(lv_event_get_user_data(event));
		const lv_event_code_t code = lv_event_get_code(event);
		if (code == LV_EVENT_INVALIDATE_AREA) {
			const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
			if (area && area->x2 >= area->x1 && area->y2 >= area->y1) {
				mgr->cycleRefresh.stats.invalidations.add(lv_area_get_width(area), lv_area_get_height(area),
						mgr->invalidationSource);
			}
			return;
		}
		if (mgr->invalidationSource != DisplayInvalidationSource::Timer) return;
		uint64_t glyph_miss_us = 0;
#if HAS_TTF_FONTS && HAS_FREETYPE_FONTS
		if (code == LV_EVENT_RENDER_START || code == LV_EVENT_RENDER_READY)
			glyph_miss_us = freetype_mono_font_get_stats().miss_us;
#endif
		switch (code) {
			case LV_EVENT_REFR_START:
				mgr->cycleRefresh.beginRefresh(esp_timer_get_time());
				break;
			case LV_EVENT_REFR_READY:
				mgr->cycleRefresh.endRefresh(esp_timer_get_time());
				break;
			case LV_EVENT_RENDER_START:
				mgr->cycleRefresh.beginRender(esp_timer_get_time(), mgr->cycleFlush.total_us, glyph_miss_us);
				break;
			case LV_EVENT_RENDER_READY:
				mgr->cycleRefresh.endRender(esp_timer_get_time(), mgr->cycleFlush.total_us, glyph_miss_us);
				break;
			default:
				break;
		}
}
#endif

// LVGL v9 flush callback
void DisplayManager::flushCallback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
		DisplayManager* mgr = (DisplayManager*)lv_display_get_user_data(disp);
#if HAS_DISPLAY_DIAGNOSTICS
		const uint64_t flush_start_us = esp_timer_get_time();
#endif

		uint32_t w = (area->x2 - area->x1 + 1);
		uint32_t h = (area->y2 - area->y1 + 1);
		
		// v9 stride: rows in px_map may be padded for cache-line alignment.
		// Tell the driver so it can advance the source pointer correctly.
		uint32_t stride = lv_draw_buf_width_to_stride(w, lv_display_get_color_format(disp));
		mgr->driver->flushSrcStride = stride;
		
		// Push pixels to display via driver HAL.
		bool swap = (mgr->driver->renderMode() != DisplayDriver::RenderMode::Buffered);
		mgr->driver->startWrite();
		mgr->driver->setAddrWindow(area->x1, area->y1, w, h);
		mgr->driver->pushColors((uint16_t *)px_map, w * h, swap);
		mgr->driver->endWrite();

		// Signal that the driver may need a post-render present() step.
		if (mgr) {
				mgr->flushPending = true;
		}
		
		// For async drivers (DMA2D), flush_ready is called from the DMA
		// completion callback.  For sync drivers, signal it here.
		if (!mgr->driver->asyncFlush()) {
				lv_display_flush_ready(disp);
		}
#if HAS_DISPLAY_DIAGNOSTICS
		mgr->cycleFlush.add(w * h, static_cast<uint32_t>(esp_timer_get_time() - flush_start_us));
#endif
}

// FreeRTOS task for continuous LVGL rendering
#if HAS_TTF_FONTS
#include "font_store.h"
#endif
void DisplayManager::lvglTask(void* pvParameter) {
		DisplayManager* mgr = (DisplayManager*)pvParameter;
#if HAS_DISPLAY_DIAGNOSTICS
		DisplayGlyphStats previous_glyphs;
#endif
#if HAS_TTF_FONTS
        uint32_t font_generation_seen = font_store_generation();
#endif
		
		LOGI("Display", "LVGL render task start (core %d)", xPortGetCoreID());
		portENTER_CRITICAL(&g_perf_mux);
		g_perf_window.reset(millis());
		g_perf_ready = false;
		portEXIT_CRITICAL(&g_perf_mux);
		
		while (true) {
				device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_WAIT_LOCK);
#if HAS_DISPLAY_DIAGNOSTICS
				const uint64_t lock_start_us = esp_timer_get_time();
#endif
				mgr->lock();
				const uint64_t cycle_start_us = esp_timer_get_time();
				uint32_t screen_update_us = 0;
#if HAS_DISPLAY_DIAGNOSTICS
				const uint32_t lock_wait_us = static_cast<uint32_t>(cycle_start_us - lock_start_us);
				uint32_t screen_switch_us = 0;
				bool screen_switched = false;
				mgr->cycleFlush = {};
#endif
				bool updated_after_screen_switch = false;
				if (mgr->lvglStopRequested) {
						device_dialog_close();
						mgr->unlock();
						mgr->lvglTaskStopped = true;
						vTaskSuspend(nullptr);
						continue;
				}
					if (ota_activity_is_active()) {
						device_dialog_close();
						#if HAS_TOUCH
						touch_manager_cancel_physical_input();
						#endif
						mgr->flushPending = false;
						mgr->unlock();
						vTaskDelay(pdMS_TO_TICKS(20));
						continue;
					}
					device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_DEFERRED_WORK);
#if HAS_DISPLAY_DIAGNOSTICS
					mgr->invalidationSource = DisplayInvalidationSource::Deferred;
					const uint64_t deferred_start_us = esp_timer_get_time();
#endif
#if HAS_TTF_FONTS
					const uint32_t font_generation = font_store_generation();
					if (font_generation != font_generation_seen) {
						LOGD("Fonts", "Store generation %lu -> %lu: invalidate pad caches", static_cast<unsigned long>(font_generation_seen), static_cast<unsigned long>(font_generation));
						for (uint8_t index = 0; index < MAX_PADS; ++index) {
							if (mgr->padScreens[index]) mgr->padScreens[index]->evict();
						}
						font_generation_seen = font_generation;
					}
#endif
					mgr->processDisplayJob();
					device_dialog_loop();
					action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LVGL);

				// Apply any deferred splash status update.
				if (mgr->pendingSplashStatusSet) {
						char text[sizeof(mgr->pendingSplashStatus)];
						bool has = false;
						portENTER_CRITICAL(&g_splash_status_mux);
						if (mgr->pendingSplashStatusSet) {
								strlcpy(text, mgr->pendingSplashStatus, sizeof(text));
								mgr->pendingSplashStatusSet = false;
								has = true;
						}
						portEXIT_CRITICAL(&g_splash_status_mux);
						if (has) {
								mgr->splashScreen.setStatus(text);
						}
				}
				
#if HAS_DISPLAY_DIAGNOSTICS
				const uint32_t deferred_us = static_cast<uint32_t>(esp_timer_get_time() - deferred_start_us);
				mgr->invalidationSource = DisplayInvalidationSource::Other;
#endif
				// Process pending screen switch (deferred from external calls)
				if (mgr->pendingScreen) {
#if HAS_DISPLAY_DIAGNOSTICS
						screen_switched = true;
						mgr->invalidationSource = DisplayInvalidationSource::ScreenSwitch;
						const uint64_t switch_start_us = esp_timer_get_time();
#endif
						device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_SCREEN_SWITCH);
						Screen* target = mgr->pendingScreen;
						if (target == &mgr->wifiSetupScreen) {
								mgr->wifiSetupScreen.setLogo(mgr->splashScreen.getLogo());
						} else if (target == &mgr->infoScreen) {
								mgr->infoScreen.setLogo(mgr->splashScreen.getLogo());
						}
						device_dialog_close();
						#if HAS_TOUCH
						touch_manager_cancel_physical_input();
						#endif
						if (mgr->currentScreen) {
								mgr->currentScreen->hide();
						}

						// Push current screen onto history (skip for splash and goBack)
						Screen* historyScreen = mgr->pendingHistoryScreen ? mgr->pendingHistoryScreen : mgr->currentScreen;
						if (historyScreen && !mgr->skipHistoryPush
								&& historyScreen != &mgr->splashScreen) {
								if (mgr->screenHistoryCount < SCREEN_HISTORY_MAX) {
										mgr->screenHistory[mgr->screenHistoryCount++] = historyScreen;
								} else {
										memmove(&mgr->screenHistory[0], &mgr->screenHistory[1],
												(SCREEN_HISTORY_MAX - 1) * sizeof(Screen*));
										mgr->screenHistory[SCREEN_HISTORY_MAX - 1] = historyScreen;
								}
						}
						mgr->skipHistoryPush = false;
						mgr->pendingHistoryScreen = nullptr;

						mgr->currentScreen = target;
						mgr->currentScreen->show();
						mgr->pendingScreen = nullptr;
						if (!screen_saver_manager_is_rendering_suspended()) {
							// Build an evicted pad before LVGL renders the newly loaded screen.
							const uint64_t update_start_us = esp_timer_get_time();
							mgr->currentScreen->update();
							screen_update_us = static_cast<uint32_t>(esp_timer_get_time() - update_start_us);
							updated_after_screen_switch = true;
						}

						// LRU promotion for pad screens — track which pads have arrays allocated
						for (uint8_t pi = 0; pi < MAX_PADS; pi++) {
								if (target == mgr->padScreens[pi]) {
										mgr->lruPromote(pi);
										break;
								}
						}

						// Reset LVGL input device state so leftover PRESSED from the
						// previous screen doesn't fire a phantom CLICKED on the new screen.
						lv_indev_reset(NULL, NULL);

						const char* screenId = mgr->getScreenIdForInstance(mgr->currentScreen);
						LOGI("Display", "Switched to %s", screenId ? screenId : "(unregistered)");

						// Apply current pixel shift offset (burn-in prevention).
						if (mgr->currentScreen != &mgr->splashScreen) {
								int dx = 0, dy = 0;
								screen_saver_manager_get_pixel_shift(&dx, &dy);
								lv_obj_set_style_translate_x(lv_scr_act(), dx, 0);
								lv_obj_set_style_translate_y(lv_scr_act(), dy, 0);
						}
						// Initial update is already reported separately.
#if HAS_DISPLAY_DIAGNOSTICS
						screen_switch_us = static_cast<uint32_t>(esp_timer_get_time() - switch_start_us) - screen_update_us;
#endif
				}
				
				// Handle LVGL rendering (animations, timers, etc.)
				device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_TIMER);
#if HAS_DISPLAY_DIAGNOSTICS
				mgr->invalidationSource = DisplayInvalidationSource::Timer;
				const uint32_t flush_before_timer_us = mgr->cycleFlush.total_us;
#endif
				const uint64_t lv_start_us = esp_timer_get_time();
				uint32_t delayMs = lv_timer_handler();
				const uint32_t lv_timer_us = (uint32_t)(esp_timer_get_time() - lv_start_us);
#if HAS_DISPLAY_DIAGNOSTICS
				const uint32_t lv_timer_flush_us = mgr->cycleFlush.total_us - flush_before_timer_us;
				mgr->invalidationSource = DisplayInvalidationSource::Other;
#endif

				// Check countdown timer expiry (fire beep on edge)
				timer_engine_tick();

				uint32_t data_stream_us = 0;
#if HAS_HA_LIVE
				ha_live_tick();
#endif
#if HAS_DISPLAY
				// Poll data stream registry (background ring buffers for
				// history-based widgets, independent of active screen).
				{
						const uint64_t stream_start_us = esp_timer_get_time();
						static uint32_t s_ds_generation = UINT32_MAX;
						uint32_t gen = pad_config_get_generation();
						if (gen != s_ds_generation) {
								s_ds_generation = gen;
								data_stream_rebuild();
#if HAS_MUSIC_ANALYSIS
								music_analysis_rebuild_demand();
#endif
						}
						data_stream_poll();
						data_stream_us = static_cast<uint32_t>(esp_timer_get_time() - stream_start_us);
				}
#endif

#if HAS_MUSIC_ANALYSIS && !HAS_MQTT && !HAS_HA_LIVE
				{
						static uint32_t s_music_generation = UINT32_MAX;
						const uint32_t generation = pad_config_get_generation();
						if (generation != s_music_generation) {
							music_analysis_rebuild_demand();
							s_music_generation = generation;
						}
				}
#endif

				// Update current screen (data refresh)
				if (!updated_after_screen_switch && mgr->currentScreen
						&& !screen_saver_manager_is_rendering_suspended()) {
						device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_SCREEN_UPDATE);
#if HAS_DISPLAY_DIAGNOSTICS
						mgr->invalidationSource = DisplayInvalidationSource::ScreenUpdate;
#endif
						const uint64_t update_start_us = esp_timer_get_time();
						mgr->currentScreen->update();
						screen_update_us = static_cast<uint32_t>(esp_timer_get_time() - update_start_us);
#if HAS_DISPLAY_DIAGNOSTICS
						mgr->invalidationSource = DisplayInvalidationSource::Other;
#endif
				}
				
				// Flush canvas buffer only when LVGL produced draw data.
				if (mgr->flushPending) {
						device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_FLUSH);
					bool flushAccepted = false;
					if (mgr->driver->renderMode() == DisplayDriver::RenderMode::Buffered) {
						if (mgr->presentSem) {
								// Buffered mode: delegate present() to the async present task.
								// This frees the LVGL mutex during the slow QSPI panel transfer,
								// allowing touch input and animations to continue processing.
								xSemaphoreGive(mgr->presentSem);
							flushAccepted = true;
						}
						} else {
								// Direct mode: present() is a no-op. Update perf stats inline.
								portENTER_CRITICAL(&g_perf_mux);
								++g_perf_window.frames;
								portEXIT_CRITICAL(&g_perf_mux);
									flushAccepted = true;
						}
								// The render task can begin before the buffered present task exists.
								// Retain the first invalidated frame until a consumer accepts it.
								if (flushAccepted) mgr->flushPending = false;
				}

				const uint32_t cycle_us = static_cast<uint32_t>(esp_timer_get_time() - cycle_start_us);
				const uint32_t sample_ms = millis();
#if HAS_DISPLAY_DIAGNOSTICS
				DisplayGlyphStats glyphs;
#if HAS_TTF_FONTS && HAS_FREETYPE_FONTS
				glyphs = freetype_mono_font_get_stats();
#endif
#endif
				portENTER_CRITICAL(&g_perf_mux);
				g_perf_window.lv_timer.add(lv_timer_us);
				g_perf_window.data_stream.add(data_stream_us);
				g_perf_window.screen_update.add(screen_update_us);
				g_perf_window.cycle.add(cycle_us);
#if HAS_DISPLAY_DIAGNOSTICS
				g_perf_window.render.add({sample_ms, lock_wait_us, deferred_us, screen_switch_us,
						lv_timer_us, lv_timer_flush_us, data_stream_us, screen_update_us, cycle_us, mgr->cycleFlush,
						screen_switched, mgr->cycleRefresh.stats, glyphs.since(previous_glyphs)});
#endif
				if (g_perf_window.publish(sample_ms, g_perf)) g_perf_ready = true;
				portEXIT_CRITICAL(&g_perf_mux);
#if HAS_DISPLAY_DIAGNOSTICS
				previous_glyphs = glyphs;
				mgr->cycleRefresh = {};
#endif
				mgr->unlock();
				
				// Sleep based on LVGL's suggested next timer deadline.
				// Clamp to keep UI responsive while avoiding busy looping on static screens.
				if (delayMs < 1) delayMs = 1;
				if (delayMs > 20) delayMs = 20;

				// Throttle the render loop only when the screensaver suspends rendering.
				// The display is blanked and panel is sleeping — no need for fast ticks.
				if (screen_saver_manager_is_rendering_suspended()) {
						device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_SLEEP);
						delayMs = SCREENSAVER_SLEEP_TICK_MS;
						#if HAS_TOUCH
						if (touch_manager_contact_capacity() > 0 && delayMs > 20) delayMs = 20;
						#endif

						// No rendering during sleep — publish fps=0 so /api/health
						// doesn't show a stale value from before the screensaver.
						portENTER_CRITICAL(&g_perf_mux);
						g_perf = {};
#if HAS_DISPLAY_DIAGNOSTICS
						g_perf.render = g_perf_window.render;
#endif
						g_perf_window.reset(millis());
						g_perf_ready = true;
						portEXIT_CRITICAL(&g_perf_mux);

				}

				vTaskDelay(pdMS_TO_TICKS(delayMs));
		}
}

// FreeRTOS task: async QSPI panel transfer for Buffered render mode.
// Runs concurrently with the LVGL task — present() reads the PSRAM
// framebuffer while pushColors() may be writing to it.  The dirty-
// row spinlock in the driver ensures no tracking data is lost; pixel-
// level overlap is harmless (minor one-frame tear, self-correcting).
void DisplayManager::presentTask(void* pvParameter) {
		DisplayManager* mgr = (DisplayManager*)pvParameter;
		
		LOGI("Display", "Present task start (core %d)", xPortGetCoreID());
		
		while (true) {
				// Wait for signal from LVGL task
				xSemaphoreTake(mgr->presentSem, portMAX_DELAY);
				if (ota_activity_is_active()) continue;
				
				// Time the QSPI panel transfer
				const uint64_t start_us = esp_timer_get_time();
				mgr->driver->present();
				const uint32_t present_us = (uint32_t)(esp_timer_get_time() - start_us);
				
				portENTER_CRITICAL(&g_perf_mux);
				if (!screen_saver_manager_is_rendering_suspended()) {
						g_perf_window.present.add(present_us);
						++g_perf_window.frames;
				}
				portEXIT_CRITICAL(&g_perf_mux);
		}
}

bool display_manager_get_perf_stats(DisplayPerfStats* out) {
		if (!out) return false;
		bool ok = false;
		portENTER_CRITICAL(&g_perf_mux);
		ok = g_perf_ready;
		if (ok) {
				*out = g_perf;
#if HAS_DISPLAY_DIAGNOSTICS
				out->render = g_perf_window.render;
#endif
		}
		portEXIT_CRITICAL(&g_perf_mux);
		return ok;
}

#if HAS_DISPLAY_DIAGNOSTICS
bool DisplayManager::getPresentationStats(DisplayPresentationStats* out) {
		return driver && out && driver->getPresentationStats(out);
}

bool display_manager_get_presentation_stats(DisplayPresentationStats* out) {
		return displayManager && displayManager->getPresentationStats(out);
}
#endif

#endif // HAS_DISPLAY
