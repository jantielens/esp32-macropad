#ifndef DUMMY_SENSOR_H
#define DUMMY_SENSOR_H

#include "board_config.h"

#if HAS_SENSOR_DUMMY

#include "sensors/sensor_manager.h"
#if HAS_MQTT
#include "mqtt_manager.h"
#endif

class DummySensor {
public:
		bool begin();
		void appendJson(JsonObject &doc);
#if HAS_MQTT
		void publishHaDiscovery(MqttManager &mqtt);
#endif

private:
		void update();

		bool _initialized = false;
		bool _available = false;
		float _value = 0.0f;
};

void register_dummy_sensor(SensorRegistry &registry);

#endif // HAS_SENSOR_DUMMY

#endif // DUMMY_SENSOR_H
