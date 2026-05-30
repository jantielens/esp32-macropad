#ifndef NAU7802_SENSOR_H
#define NAU7802_SENSOR_H

#include "board_config.h"

#if HAS_SENSOR_NAU7802

#include "sensors/sensor_manager.h"

void register_nau7802_sensor(SensorRegistry &registry);

// ---- Scale public API (mirrors HX711 interface for scale HAL) ----

float nau7802_get_weight();
float nau7802_get_weight_ema();
float nau7802_get_flow_rate();
void  nau7802_tare();
void  nau7802_request_tare();
void  nau7802_request_tare_no_persist();
void  nau7802_request_calibrate();
void  nau7802_set_calibration(float factor);
void  nau7802_set_offset(long offset);
float nau7802_get_calibration_factor();
long  nau7802_get_offset();
float nau7802_get_value(int times);
bool  nau7802_is_available();

float nau7802_get_cal_weight();
void  nau7802_adjust_cal_weight(float delta);
void  nau7802_set_cal_weight(float value);
float nau7802_calibrate_with_cal_weight();
void  nau7802_request_persist();
const char* nau7802_get_status();
void  nau7802_apply_preset(uint8_t preset_index);

#endif // HAS_SENSOR_NAU7802

#endif // NAU7802_SENSOR_H
