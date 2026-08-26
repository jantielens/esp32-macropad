#include "mqtt_camera.h"

#if HAS_CAMERA && HAS_MQTT

#include "camera.h"
#include "log_manager.h"
#include "mqtt_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>

namespace {

constexpr const char* kTag = "MqttCamera";
char s_latest_topic[128] = {};
char s_roll_topic[128] = {};
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_capture_pending = false;
volatile CameraCaptureSaveTo s_pending_destination = CAMERA_CAPTURE_SAVE_LATEST;

} // namespace

void mqtt_camera_init() {
	const char* base = mqtt_manager.baseTopic();
	snprintf(s_latest_topic, sizeof(s_latest_topic), "%s/camera/snapshot/latest", base);
	snprintf(s_roll_topic, sizeof(s_roll_topic), "%s/camera/snapshot/roll", base);
	LOGI(kTag, "Init: latest=%s", s_latest_topic);
}

void mqtt_camera_on_connected() {
	mqtt_manager.subscribe(s_latest_topic);
	mqtt_manager.subscribe(s_roll_topic);
}

void mqtt_camera_on_message(const char* topic, const uint8_t*, unsigned int) {
	if (!topic) return;

	CameraCaptureSaveTo destination;
	if (strcmp(topic, s_latest_topic) == 0) {
		destination = CAMERA_CAPTURE_SAVE_LATEST;
	} else if (strcmp(topic, s_roll_topic) == 0) {
		destination = CAMERA_CAPTURE_SAVE_BOTH;
	} else {
		return;
	}

	portENTER_CRITICAL(&s_mux);
	s_pending_destination = destination;
	s_capture_pending = true;
	portEXIT_CRITICAL(&s_mux);
}

void mqtt_camera_loop() {
	CameraCaptureSaveTo destination;
	portENTER_CRITICAL(&s_mux);
	if (!s_capture_pending) {
		portEXIT_CRITICAL(&s_mux);
		return;
	}
	destination = s_pending_destination;
	s_capture_pending = false;
	portEXIT_CRITICAL(&s_mux);

	if (!camera_capture_save(destination)) {
		LOGW(kTag, "Camera snapshot capture failed");
	}
}

#endif