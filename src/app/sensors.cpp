#include "board_config.h"
#include "board_hardware.h"
#include "sensors/sensor_manager.h"

// Arduino build system only compiles .cpp files in the sketch root directory.
// This translation unit centralizes sensor implementation includes.

#if HAS_SENSOR_BME280
#include "sensors/bme280_sensor.cpp"
#endif

#if HAS_SENSOR_AHT10
#include "sensors/aht10_sensor.cpp"
#endif

#if HAS_SENSOR_BATTERY_ADC
#include "sensors/battery_adc_sensor.cpp"
#endif

#ifdef BOARD_SENSOR_SOURCE
#include BOARD_SENSOR_SOURCE
#endif

#if HAS_SENSOR_LD2410_OUT
#include "sensors/ld2410_out_sensor.cpp"
#endif

#if HAS_SENSOR_DUMMY
#include "sensors/dummy_sensor.cpp"
#endif

#if HAS_CAMERA
#include "sensors/camera_presence_sensor.cpp"
#endif

void sensor_manager_register_all(SensorRegistry &registry) {
	#if HAS_BATTERY_INFO && IS_EPAPER_FRAME && (defined(BOARD_INKPLATE5V2_FRAME) || defined(BOARD_INKPLATE6FLICK_FRAME) || \
		(defined(EPAPER_FRAME_BATTERY_ADC_PIN) && EPAPER_FRAME_BATTERY_ADC_PIN >= 0))
	static const BatteryProvider frame_battery = {
		"E-Paper Frame: driver-owned voltage samples and approximate voltage-derived percentage. "
		"Sampling follows frame wake/refresh operations, not periodic polling; readings expire after three minutes of active runtime.",
		battery_field_mask(BatteryField::Voltage) | battery_field_mask(BatteryField::Percentage),
		nullptr,
	};
	SensorCallbacks frame = {};
	frame.name = "E-Paper Frame battery";
	frame.battery = &frame_battery;
	frame.event_driven = true;
	frame.sample_interval_ms = 60000;
	registry.add(frame);
	#endif
	#ifdef BOARD_SENSOR_SOURCE
	board_register_sensors(registry);
	#endif
	#if HAS_SENSOR_BME280
	register_bme280_sensor(registry);
	#endif

	#if HAS_SENSOR_AHT10
	register_aht10_sensor(registry);
	#endif

	#if HAS_SENSOR_BATTERY_ADC
	register_battery_adc_sensor(registry);
	#endif

	#if HAS_SENSOR_LD2410_OUT
	register_ld2410_out_sensor(registry);
	#endif

	#if HAS_SENSOR_DUMMY
	register_dummy_sensor(registry);
	#endif

	#if HAS_CAMERA
	register_camera_presence_sensor(registry);
	#endif

}

#include "sensors/sensor_manager.cpp"
