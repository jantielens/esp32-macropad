#include "board_config.h"
#include "sensors/sensor_manager.h"

// Arduino build system only compiles .cpp files in the sketch root directory.
// This translation unit centralizes sensor implementation includes.

#if HAS_SENSOR_BME280
#include "sensors/bme280_sensor.cpp"
#endif

#if HAS_SENSOR_LD2410_OUT
#include "sensors/ld2410_out_sensor.cpp"
#endif

#if HAS_SENSOR_DUMMY
#include "sensors/dummy_sensor.cpp"
#endif

void sensor_manager_register_all(SensorRegistry &registry) {
	#if HAS_SENSOR_BME280
	register_bme280_sensor(registry);
	#endif

	#if HAS_SENSOR_LD2410_OUT
	register_ld2410_out_sensor(registry);
	#endif

	#if HAS_SENSOR_DUMMY
	register_dummy_sensor(registry);
	#endif
}

#include "sensors/sensor_manager.cpp"
