#ifndef LD2410_OUT_SENSOR_H
#define LD2410_OUT_SENSOR_H

#include "board_config.h"
#include <Arduino.h>
#include <ArduinoJson.h>

class Ld2410OutSensor {
public:
		bool begin();
		void loop();
		void appendJson(JsonObject &doc);
		void handleChangeFromISR();

private:
		bool _initialized = false;
		bool _available = false;
		volatile bool _presence = false;
		volatile bool _changed = false;
		volatile unsigned long _last_isr_ms = 0;

		bool _pending_publish = false;
		bool _pending_presence = false;

		void consumeChangeLog(bool presence);
};

class SensorRegistry;
void register_ld2410_out_sensor(SensorRegistry &registry);

#endif // LD2410_OUT_SENSOR_H
