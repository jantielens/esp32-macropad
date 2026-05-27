#ifndef DUTY_CYCLE_H
#define DUTY_CYCLE_H

struct DeviceConfig;

// Non-const because the e-paper duty cycle persists a fresh sidecar CRC into
// the in-memory config via epaper_refresh_run(). Other modes do not mutate.
bool duty_cycle_run(DeviceConfig *config);

#endif // DUTY_CYCLE_H
