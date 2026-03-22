// Test stub: sensors/sensor_manager.h — minimal for hx711_sensor.h to compile
#pragma once
#include <cstdint>

#if HAS_MQTT
class MqttManager;
#endif

struct SensorCallbacks {
    const char *name;
    void (*init)();
    void (*loop)();
    void (*append_api)(void*);
    void (*append_mqtt)(void*);
#if HAS_MQTT
    void (*publish_ha)(MqttManager &mqtt);
#endif
};

class SensorRegistry {
public:
    bool add(const SensorCallbacks &) { return true; }
};
