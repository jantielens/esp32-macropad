// Display Manager - LVGL render task, present task, and flush callback
#include "board_config.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "log_manager.h"
#include "rtos_task_utils.h"

#include "data_stream.h"
#include "button_confirmation.h"
#include "pad_config.h"
#include "screen_saver_manager.h"
#include "timer_engine.h"

#include <esp_timer.h>

extern portMUX_TYPE g_splash_status_mux;
extern portMUX_TYPE g_perf_mux;
extern DisplayPerfStats g_perf;
extern bool g_perf_ready;
extern uint32_t g_perf_window_start_ms;
extern uint16_t g_perf_frames_in_window;

DisplayTaskDispatchResult DisplayManager::dispatch(
		DisplayTaskExec exec, DisplayTaskCleanup cleanup,
		const void* ctx, size_t ctxLen,
		uint32_t timeoutMs, bool* outOk, char* outMsg, size_t outMsgLen) {
		if (!exec || !displayJobDone || ctxLen > sizeof(displayJobCtx)) {
				return DISPLAY_TASK_DISPATCH_UNAVAILABLE;
		}

		portENTER_CRITICAL(&displayJobMux);
		if (displayJobBusy) {
				portEXIT_CRITICAL(&displayJobMux);
				return DISPLAY_TASK_DISPATCH_BUSY;
		}
		displayJobBusy = true;
		displayJobPending = false;
		portEXIT_CRITICAL(&displayJobMux);

		xSemaphoreTake(displayJobDone, 0);

		portENTER_CRITICAL(&displayJobMux);
		displayJobDoneFlag = false;
		displayJobWaiter = true;
		displayJobExec = exec;
		displayJobCleanup = cleanup;
		displayJobCtxLen = ctxLen;
		if (ctx && ctxLen) memcpy(displayJobCtx, ctx, ctxLen);
		displayJobOk = false;
		displayJobMessage[0] = '\0';
		displayJobPending = true;
		portEXIT_CRITICAL(&displayJobMux);

		if (xSemaphoreTake(displayJobDone, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
				portENTER_CRITICAL(&displayJobMux);
				if (outOk) *outOk = displayJobOk;
				if (outMsg && outMsgLen) strlcpy(outMsg, displayJobMessage, outMsgLen);
				displayJobBusy = false;
				displayJobWaiter = false;
				portEXIT_CRITICAL(&displayJobMux);
				return DISPLAY_TASK_DISPATCH_OK;
		}

		portENTER_CRITICAL(&displayJobMux);
		if (displayJobDoneFlag) {
				portEXIT_CRITICAL(&displayJobMux);
				xSemaphoreTake(displayJobDone, portMAX_DELAY);
				portENTER_CRITICAL(&displayJobMux);
				if (outOk) *outOk = displayJobOk;
				if (outMsg && outMsgLen) strlcpy(outMsg, displayJobMessage, outMsgLen);
				displayJobBusy = false;
				displayJobWaiter = false;
				portEXIT_CRITICAL(&displayJobMux);
				return DISPLAY_TASK_DISPATCH_OK;
		}
		displayJobWaiter = false;
		portEXIT_CRITICAL(&displayJobMux);
		return DISPLAY_TASK_DISPATCH_TIMEOUT;
}

void DisplayManager::processDisplayJob() {
		DisplayTaskExec exec = nullptr;
		DisplayTaskCleanup cleanup = nullptr;
		portENTER_CRITICAL(&displayJobMux);
		if (displayJobPending) {
				displayJobPending = false;
				exec = displayJobExec;
				cleanup = displayJobCleanup;
		}
		portEXIT_CRITICAL(&displayJobMux);
		if (!exec) return;

		bool ok = false;
		char msg[160] = {0};
		exec(displayJobCtx, &ok, msg, sizeof(msg));

		portENTER_CRITICAL(&displayJobMux);
		displayJobOk = ok;
		strlcpy(displayJobMessage, msg, sizeof(displayJobMessage));
		displayJobDoneFlag = true;
		const bool notify = displayJobWaiter;
		portEXIT_CRITICAL(&displayJobMux);
		if (notify) {
				xSemaphoreGive(displayJobDone);
		} else {
				if (cleanup) cleanup(displayJobCtx);
				portENTER_CRITICAL(&displayJobMux);
				displayJobBusy = false;
				portEXIT_CRITICAL(&displayJobMux);
		}
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
		
		while (true) {
				mgr->lock();
				mgr->processDisplayJob();

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
						Screen* target = mgr->pendingScreen;
						button_confirmation_cancel();
						if (mgr->currentScreen) {
								mgr->currentScreen->hide();
						}

						// Push current screen onto history (skip for splash and goBack)
						if (mgr->currentScreen && !mgr->skipHistoryPush
								&& mgr->currentScreen != &mgr->splashScreen) {
								if (mgr->screenHistoryCount < SCREEN_HISTORY_MAX) {
										mgr->screenHistory[mgr->screenHistoryCount++] = mgr->currentScreen;
								} else {
										memmove(&mgr->screenHistory[0], &mgr->screenHistory[1],
												(SCREEN_HISTORY_MAX - 1) * sizeof(Screen*));
										mgr->screenHistory[SCREEN_HISTORY_MAX - 1] = mgr->currentScreen;
								}
						}
						mgr->skipHistoryPush = false;

						mgr->currentScreen = target;
						mgr->currentScreen->show();
						mgr->pendingScreen = nullptr;

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
				const uint64_t lv_start_us = esp_timer_get_time();
				uint32_t delayMs = lv_timer_handler();
				const uint32_t lv_timer_us = (uint32_t)(esp_timer_get_time() - lv_start_us);

				// Check countdown timer expiry (fire beep on edge)
				timer_engine_tick();
				
#if HAS_MQTT
				// Poll data stream registry (background ring buffers for
				// history-based widgets, independent of active screen).
				{
						static uint32_t s_ds_generation = UINT32_MAX;
						uint32_t gen = pad_config_get_generation();
						if (gen != s_ds_generation) {
								s_ds_generation = gen;
								data_stream_rebuild();
						}
						data_stream_poll();
				}
#endif

				// Update current screen (data refresh)
				if (mgr->currentScreen && !screen_saver_manager_is_fully_asleep()) {
						mgr->currentScreen->update();
				}
				
				// Flush canvas buffer only when LVGL produced draw data.
				if (mgr->flushPending) {
						if (mgr->driver->renderMode() == DisplayDriver::RenderMode::Buffered
								&& mgr->presentSem) {
								// Buffered mode: delegate present() to the async present task.
								// This frees the LVGL mutex during the slow QSPI panel transfer,
								// allowing touch input and animations to continue processing.
								mgr->sharedLvTimerUs = lv_timer_us;
								xSemaphoreGive(mgr->presentSem);
						} else {
								// Direct mode: present() is a no-op. Update perf stats inline.
								const uint32_t now_ms = millis();
								if (g_perf_window_start_ms == 0) {
										g_perf_window_start_ms = now_ms;
										g_perf_frames_in_window = 0;
								}

								g_perf_frames_in_window++;

								const uint32_t elapsed = now_ms - g_perf_window_start_ms;
								if (elapsed >= 1000) {
										const uint16_t fps = g_perf_frames_in_window;
										

										portENTER_CRITICAL(&g_perf_mux);
										g_perf.fps = fps;
										g_perf.lv_timer_us = lv_timer_us;
										g_perf.present_us = 0;
										g_perf_ready = true;
										portEXIT_CRITICAL(&g_perf_mux);

										g_perf_window_start_ms = now_ms;
										g_perf_frames_in_window = 0;
								}
						}
						mgr->flushPending = false;
				}

				mgr->unlock();
				
				// Sleep based on LVGL's suggested next timer deadline.
				// Clamp to keep UI responsive while avoiding busy looping on static screens.
				if (delayMs < 1) delayMs = 1;
				if (delayMs > 20) delayMs = 20;

				// Throttle the render loop while the screensaver is fully asleep.
				// The display is blanked and panel is sleeping — no need for fast ticks.
				if (screen_saver_manager_is_fully_asleep()) {
						delayMs = SCREENSAVER_SLEEP_TICK_MS;

						// No rendering during sleep — publish fps=0 so /api/health
						// doesn't show a stale value from before the screensaver.
						portENTER_CRITICAL(&g_perf_mux);
						g_perf.fps = 0;
						g_perf.lv_timer_us = 0;
						g_perf.present_us = 0;
						g_perf_ready = true;
						portEXIT_CRITICAL(&g_perf_mux);

						// Reset the perf window so the first frame after wake
						// starts a clean count (no partial-window spike).
						g_perf_window_start_ms = 0;
						g_perf_frames_in_window = 0;
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
				
				// Time the QSPI panel transfer
				const uint64_t start_us = esp_timer_get_time();
				mgr->driver->present();
				const uint32_t present_us = (uint32_t)(esp_timer_get_time() - start_us);
				
				// Update perf stats (frame count + periodic publish).
				// These statics are only accessed from one task context per board
				// (either here for Buffered, or inline in lvglTask for Direct).
				const uint32_t now_ms = millis();
				if (g_perf_window_start_ms == 0) {
						g_perf_window_start_ms = now_ms;
						g_perf_frames_in_window = 0;
				}
				g_perf_frames_in_window++;
				
				const uint32_t elapsed = now_ms - g_perf_window_start_ms;
				if (elapsed >= 1000) {
						const uint16_t fps = g_perf_frames_in_window;
						const uint32_t lv_us = mgr->sharedLvTimerUs;
						portENTER_CRITICAL(&g_perf_mux);
						g_perf.fps = fps;
						g_perf.lv_timer_us = lv_us;
						g_perf.present_us = present_us;
						g_perf_ready = true;
						portEXIT_CRITICAL(&g_perf_mux);
						
						g_perf_window_start_ms = now_ms;
						g_perf_frames_in_window = 0;
				}
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
