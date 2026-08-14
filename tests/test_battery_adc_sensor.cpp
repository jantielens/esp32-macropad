#include <cassert>
#include <cstdio>

#include "sensors/battery_adc_sensor.h"

int main() {
	assert(battery_adc_lipo_percentage(2.90f) == 0);
	assert(battery_adc_lipo_percentage(3.79f) == 50);
	assert(battery_adc_lipo_percentage(4.20f) == 100);
	assert(battery_adc_lipo_percentage(4.25f) == 100);
	assert(battery_adc_lipo_percentage(3.54f) == 14);
	std::puts("battery_adc_sensor tests passed");
	return 0;
}