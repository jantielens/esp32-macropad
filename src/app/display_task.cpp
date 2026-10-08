// Display Manager - LVGL render task, present task, and flush callback
#include "board_config.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"

#include "data_stream.h"
#include "action_list.h"
#include "button_confirmation.h"
#include "pad_config.h"
#include "screen_saver_manager.h"
#if HAS_TOUCH
#include "touch_manager.h"
#endif
#include "timer_engine.h"
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

// Definition for the async-flush-busy flag declared in display_driver.h.
// Set true by async drivers (e.g. MIPI-DSI DMA2D) while a pixel transfer
// is in flight; cleared from the completion ISR.
volatile bool g_displayFlushBusy = false;

// LVGL v9 flush callback
void DisplayManager::flushCallback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
		DisplayManager* mgr = (DisplayManager*)lv_display_get_user_data(disp);

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
}

// FreeRTOS task for continuous LVGL rendering
void DisplayManager::lvglTask(void* pvParameter) {
		DisplayManager* mgr = (DisplayManager*)pvParameter;
		
		LOGI("Display", "LVGL render task start (core %d)", xPortGetCoreID());
		portENTER_CRITICAL(&g_perf_mux);
		g_perf_window.reset(millis());
		g_perf_ready = false;
		portEXIT_CRITICAL(&g_perf_mux);
		
		while (true) {
				device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_WAIT_LOCK);
				mgr->lock();
				const uint64_t cycle_start_us = esp_timer_get_time();
				uint32_t screen_update_us = 0;
				bool updated_after_screen_switch = false;
				if (mgr->lvglStopRequested) {
						mgr->unlock();
						mgr->lvglTaskStopped = true;
						vTaskSuspend(nullptr);
						continue;
				}
					if (ota_activity_is_active()) {
						#if HAS_TOUCH
						touch_manager_cancel_physical_input();
						#endif
						mgr->flushPending = false;
						mgr->unlock();
						vTaskDelay(pdMS_TO_TICKS(20));
						continue;
					}
					device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_DEFERRED_WORK);
					mgr->processDisplayJob();
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
				
				// Process pending screen switch (deferred from external calls)
				if (mgr->pendingScreen) {
						device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_SCREEN_SWITCH);
						Screen* target = mgr->pendingScreen;
						button_confirmation_cancel();
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
				}
				
				// Handle LVGL rendering (animations, timers, etc.)
				device_telemetry_mark_lvgl_task(DEVICE_RUNTIME_PHASE_LVGL_TIMER);
				const uint64_t lv_start_us = esp_timer_get_time();
				uint32_t delayMs = lv_timer_handler();
				const uint32_t lv_timer_us = (uint32_t)(esp_timer_get_time() - lv_start_us);

				// Check countdown timer expiry (fire beep on edge)
				timer_engine_tick();

				uint32_t data_stream_us = 0;
#if HAS_MQTT
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

#if HAS_MUSIC_ANALYSIS && !HAS_MQTT
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
						const uint64_t update_start_us = esp_timer_get_time();
						mgr->currentScreen->update();
						screen_update_us = static_cast<uint32_t>(esp_timer_get_time() - update_start_us);
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
				portENTER_CRITICAL(&g_perf_mux);
				g_perf_window.lv_timer.add(lv_timer_us);
				g_perf_window.data_stream.add(data_stream_us);
				g_perf_window.screen_update.add(screen_update_us);
				g_perf_window.cycle.add(cycle_us);
				if (g_perf_window.publish(sample_ms, g_perf)) g_perf_ready = true;
				portEXIT_CRITICAL(&g_perf_mux);
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
		}
		portEXIT_CRITICAL(&g_perf_mux);
		return ok;
}

#endif // HAS_DISPLAY
