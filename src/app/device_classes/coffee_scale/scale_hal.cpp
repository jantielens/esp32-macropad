#include "scale_hal.h"

#if HAS_SCALE

// ============================================================================
// Compile-time dispatch to the active scale sensor backend.
// Only one may be enabled per build (they share physical pins).
// ============================================================================

#if HAS_SENSOR_HX711

#include "sensors/hx711_sensor.h"

float scale_get_weight()                { return hx711_get_weight(); }
float scale_get_flow_rate()             { return hx711_get_flow_rate(); }
bool  scale_is_available()              { return hx711_is_available(); }
void  scale_tare()                      { hx711_tare(); }
void  scale_request_tare()              { hx711_request_tare(); }
void  scale_request_tare_no_persist()   { hx711_request_tare_no_persist(); }
void  scale_request_calibrate()         { hx711_request_calibrate(); }
void  scale_set_calibration(float f)    { hx711_set_calibration(f); }
void  scale_set_offset(long ofs)        { hx711_set_offset(ofs); }
float scale_get_calibration_factor()    { return hx711_get_calibration_factor(); }
long  scale_get_offset()                { return hx711_get_offset(); }
float scale_get_value(int times)        { return hx711_get_value(times); }
float scale_get_cal_weight()            { return hx711_get_cal_weight(); }
void  scale_adjust_cal_weight(float d)  { hx711_adjust_cal_weight(d); }
void  scale_set_cal_weight(float v)     { hx711_set_cal_weight(v); }
float scale_calibrate_with_cal_weight() { return hx711_calibrate_with_cal_weight(); }
void  scale_request_persist()           { hx711_request_persist(); }
const char* scale_get_status()          { return hx711_get_status(); }
void  scale_apply_preset(uint8_t idx)   { hx711_apply_preset(idx); }
float scale_get_weight_ema()            { return hx711_get_weight_ema(); }

#elif HAS_SENSOR_NAU7802

#include "sensors/nau7802_sensor.h"

float scale_get_weight()                { return nau7802_get_weight(); }
float scale_get_flow_rate()             { return nau7802_get_flow_rate(); }
bool  scale_is_available()              { return nau7802_is_available(); }
void  scale_tare()                      { nau7802_tare(); }
void  scale_request_tare()              { nau7802_request_tare(); }
void  scale_request_tare_no_persist()   { nau7802_request_tare_no_persist(); }
void  scale_request_calibrate()         { nau7802_request_calibrate(); }
void  scale_set_calibration(float f)    { nau7802_set_calibration(f); }
void  scale_set_offset(long ofs)        { nau7802_set_offset(ofs); }
float scale_get_calibration_factor()    { return nau7802_get_calibration_factor(); }
long  scale_get_offset()                { return nau7802_get_offset(); }
float scale_get_value(int times)        { return nau7802_get_value(times); }
float scale_get_cal_weight()            { return nau7802_get_cal_weight(); }
void  scale_adjust_cal_weight(float d)  { nau7802_adjust_cal_weight(d); }
void  scale_set_cal_weight(float v)     { nau7802_set_cal_weight(v); }
float scale_calibrate_with_cal_weight() { return nau7802_calibrate_with_cal_weight(); }
void  scale_request_persist()           { nau7802_request_persist(); }
const char* scale_get_status()          { return nau7802_get_status(); }
void  scale_apply_preset(uint8_t idx)   { nau7802_apply_preset(idx); }
float scale_get_weight_ema()            { return nau7802_get_weight_ema(); }

#else
#error "HAS_SCALE is true but no scale sensor backend is enabled"
#endif

#endif // HAS_SCALE
