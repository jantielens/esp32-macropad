// LED Test Rig — Profile-Driven Auto-Sweep Validator
//
// Drives 3 red LEDs with LEDC PWM on an ESP32-S3 Super Mini, generating
// shaped shutter-simulation envelopes from 1/1 s down to 1/1000 s. Used as a
// known-truth stimulus source for validating a BPW34-based shutter tester.
//
// Hardware: GPIO -> 100Ω -> LED anode -> cathode -> GND, one per channel.
// LEDs sit 2-3 mm from the BPW34 sensors.
//
// Timing strategy:
//   - WiFi disabled to minimize timer jitter.
//   - LEDC supplies the PWM carrier on three independent channels.
//   - esp_timer one-shot callbacks step ramp duty at scheduled instants only;
//     there are no busy waits or delayMicroseconds() bit-banged ramps.
//   - loop() polls a volatile flag set only after the last LED reaches duty 0.

#include <Arduino.h>
#include <WiFi.h>
#include "esp_timer.h"

#include "board_overrides.h"

#define LED_TEST_RIG_MODE_BINARY_SIMULTANEOUS 1
#define LED_TEST_RIG_MODE_BINARY_TRAVEL 2
#define LED_TEST_RIG_MODE_VERTICAL_SIM 3
#define LED_TEST_RIG_MODE_OM1 4
#define LED_TEST_RIG_MODE_BAD_CAMERA 5
#define LED_TEST_RIG_MODE_M6 6

#ifndef LED_TEST_RIG_MODE
#define LED_TEST_RIG_MODE LED_TEST_RIG_MODE_VERTICAL_SIM
#endif

namespace {

enum class ShutterProfileKind : uint8_t {
  VerticalFocalPlane,
  HorizontalFocalPlane,
  Leaf,
};

struct RigModeSpec {
  ShutterProfileKind kind;
  const char* name;
  uint32_t rise_us;
  uint32_t fall_us;
  uint32_t delay_s1_to_s2_us;
  uint32_t delay_s2_to_s3_us;
  uint32_t threshold_percent;
  int32_t sensor_start_offset_us[3];
  int32_t sensor_exposure_offset_us[3];
  uint32_t deterministic_jitter_us;
};

struct LedEnvelopePlan {
  uint32_t start_us;
  uint32_t rise_us;
  uint32_t plateau_us;
  uint32_t fall_us;
  uint16_t peak_duty;
};

struct SpeedEntry {
  const char* label;
  uint32_t duration_us;
};

struct PwmConfig {
  uint32_t frequency_hz;
  uint8_t resolution_bits;
};

constexpr uint8_t kLedPins[3] = {LED1_PIN, LED2_PIN, LED3_PIN};
constexpr uint8_t kLedChannels[3] = {
    LED1_PWM_CHANNEL,
    LED2_PWM_CHANNEL,
    LED3_PWM_CHANNEL,
};

// Try the preferred high-carrier configuration first, then fall back to a
// carrier that is still above the shutter tester ADC bandwidth target. 10-bit at
// 250 kHz would require a faster LEDC clock than the ESP32-S3 APB source.
constexpr PwmConfig kPwmConfigs[] = {
  {300000, 8},
  {250000, 8},
  {156000, 8},
};
constexpr size_t kPwmConfigCount = sizeof(kPwmConfigs) / sizeof(kPwmConfigs[0]);
constexpr uint32_t kRampStepUs = 20;

constexpr RigModeSpec kActiveMode =
#if LED_TEST_RIG_MODE == LED_TEST_RIG_MODE_BINARY_SIMULTANEOUS
  {ShutterProfileKind::Leaf, "BINARY_SIMULTANEOUS", 0, 0, 0, 0, 0,
   {0, 0, 0}, {0, 0, 0}, 0};
#elif LED_TEST_RIG_MODE == LED_TEST_RIG_MODE_BINARY_TRAVEL
  {ShutterProfileKind::VerticalFocalPlane, "BINARY_TRAVEL", 0, 0, 5000, 5000, 0,
   {0, 0, 0}, {0, 0, 0}, 0};
#elif LED_TEST_RIG_MODE == LED_TEST_RIG_MODE_VERTICAL_SIM
  {ShutterProfileKind::VerticalFocalPlane, "VERTICAL_SIM", 180, 220, 300, 300, 50,
   {0, 0, 0}, {0, 0, 0}, 0};
#elif LED_TEST_RIG_MODE == LED_TEST_RIG_MODE_OM1
  {ShutterProfileKind::VerticalFocalPlane, "OM1", 220, 280, 1000, 1000, 50,
   {0, 0, 0}, {0, 0, 0}, 0};
#elif LED_TEST_RIG_MODE == LED_TEST_RIG_MODE_BAD_CAMERA
  {ShutterProfileKind::VerticalFocalPlane, "BAD_CAMERA", 260, 340, 1100, 1450, 50,
   {0, 90, 260}, {180, -80, 260}, 45};
#elif LED_TEST_RIG_MODE == LED_TEST_RIG_MODE_M6
  {ShutterProfileKind::HorizontalFocalPlane, "M6", 160, 240, 900, 900, 50,
   {0, 0, 0}, {0, 0, 0}, 0};
#else
#error "Unsupported LED_TEST_RIG_MODE"
#endif

constexpr SpeedEntry kSweep[] = {
    {"1/1",    1000000},
    {"1/2",     500000},
    {"1/4",     250000},
    {"1/8",     125000},
    {"1/15",     66667},
    {"1/30",     33333},
    {"1/60",     16667},
    {"1/125",     8000},
    {"1/250",     4000},
    {"1/500",     2000},
    {"1/1000",    1000},
  #if LED_TEST_RIG_MODE == LED_TEST_RIG_MODE_BINARY_SIMULTANEOUS || \
    LED_TEST_RIG_MODE == LED_TEST_RIG_MODE_BINARY_TRAVEL
    {"1/2000",     500},
  #endif
};
constexpr size_t kSweepCount = sizeof(kSweep) / sizeof(kSweep[0]);

constexpr uint32_t kInterPulsePauseMs = 1000;
constexpr uint32_t kInterSweepExtraPauseMs = 1000;

volatile bool g_pulse_done = true;
volatile bool g_pulse_active = false;

esp_timer_handle_t g_envelope_timer = nullptr;
uint64_t g_pulse_start_us = 0;
LedEnvelopePlan g_active_plan[3] = {};
uint16_t g_current_duty[3] = {};
PwmConfig g_pwm_config = kPwmConfigs[kPwmConfigCount - 1];
uint16_t g_peak_duty = (1u << kPwmConfigs[kPwmConfigCount - 1].resolution_bits) - 1u;

uint32_t scale_duration(uint32_t total_us, uint32_t numerator,
                        uint32_t denominator) {
  if (denominator == 0) {
    return 0;
  }
  return static_cast<uint32_t>((static_cast<uint64_t>(total_us) * numerator) /
                               denominator);
}

uint32_t add_signed_offset(uint32_t value, int32_t offset) {
  if (offset < 0) {
    const uint32_t magnitude = static_cast<uint32_t>(-offset);
    return value > magnitude ? value - magnitude : 1;
  }
  return value + static_cast<uint32_t>(offset);
}

int32_t deterministic_jitter(uint32_t duration_us, size_t sensor_index,
                             const RigModeSpec& mode) {
  if (mode.deterministic_jitter_us == 0) {
    return 0;
  }

  const uint32_t hash = duration_us / 1000u + static_cast<uint32_t>(sensor_index * 37u);
  const uint32_t span = (2u * mode.deterministic_jitter_us) + 1u;
  return static_cast<int32_t>(hash % span) -
         static_cast<int32_t>(mode.deterministic_jitter_us);
}

LedEnvelopePlan build_envelope(uint32_t start_us, uint32_t exposure_us,
                               const RigModeSpec& mode) {
  LedEnvelopePlan plan = {};
  plan.start_us = start_us;
  plan.peak_duty = g_peak_duty;

  const uint32_t ramp_budget_us = mode.rise_us + mode.fall_us;
  const uint32_t threshold_compensation_us =
      scale_duration(ramp_budget_us, mode.threshold_percent, 100);
  const uint32_t envelope_exposure_us = exposure_us + threshold_compensation_us;

  if (envelope_exposure_us > ramp_budget_us) {
    plan.rise_us = mode.rise_us;
    plan.plateau_us = envelope_exposure_us - ramp_budget_us;
    plan.fall_us = mode.fall_us;
    return plan;
  }

  plan.rise_us = scale_duration(envelope_exposure_us, mode.rise_us,
                                ramp_budget_us);
  plan.fall_us = envelope_exposure_us - plan.rise_us;
  plan.plateau_us = 0;
  return plan;
}

uint32_t next_step_boundary(uint32_t elapsed_us, uint32_t phase_start_us,
                            uint32_t phase_duration_us) {
  const uint32_t phase_elapsed = elapsed_us - phase_start_us;
  uint32_t next_phase_elapsed = ((phase_elapsed / kRampStepUs) + 1u) * kRampStepUs;
  if (next_phase_elapsed > phase_duration_us) {
    next_phase_elapsed = phase_duration_us;
  }
  return phase_start_us + next_phase_elapsed;
}

uint16_t duty_for_plan(const LedEnvelopePlan& plan, uint32_t elapsed_us,
                       bool& has_next, uint32_t& next_due_us) {
  if (elapsed_us < plan.start_us) {
    has_next = true;
    next_due_us = plan.start_us;
    return 0;
  }

  const uint32_t local_us = elapsed_us - plan.start_us;
  const uint32_t rise_end_us = plan.rise_us;
  const uint32_t plateau_end_us = rise_end_us + plan.plateau_us;
  const uint32_t envelope_end_us = plateau_end_us + plan.fall_us;

  if (plan.rise_us > 0 && local_us < rise_end_us) {
    has_next = true;
    next_due_us = plan.start_us + next_step_boundary(local_us, 0, plan.rise_us);
    return scale_duration(plan.peak_duty, local_us, plan.rise_us);
  }

  if (local_us < plateau_end_us) {
    has_next = true;
    next_due_us = plan.start_us + plateau_end_us;
    return plan.peak_duty;
  }

  if (plan.fall_us > 0 && local_us < envelope_end_us) {
    const uint32_t fall_elapsed_us = local_us - plateau_end_us;
    has_next = true;
    next_due_us = plan.start_us + next_step_boundary(local_us, plateau_end_us,
                                                     plan.fall_us);
    return scale_duration(plan.peak_duty, plan.fall_us - fall_elapsed_us,
                          plan.fall_us);
  }

  has_next = false;
  next_due_us = 0;
  return 0;
}

void write_duty(size_t index, uint16_t duty) {
  if (g_current_duty[index] == duty) {
    return;
  }
  g_current_duty[index] = duty;
  ledcWrite(kLedPins[index], duty);
}

void envelope_timer_cb(void* /*arg*/) {
  if (!g_pulse_active) {
    return;
  }

  const uint64_t now_us = esp_timer_get_time();
  const uint32_t elapsed_us = now_us > g_pulse_start_us
                                  ? static_cast<uint32_t>(now_us - g_pulse_start_us)
                                  : 0;

  bool any_next = false;
  uint32_t next_due_us = UINT32_MAX;

  for (size_t i = 0; i < 3; ++i) {
    bool channel_has_next = false;
    uint32_t channel_next_due_us = 0;
    const uint16_t duty = duty_for_plan(g_active_plan[i], elapsed_us,
                                        channel_has_next,
                                        channel_next_due_us);
    write_duty(i, duty);
    if (channel_has_next && channel_next_due_us < next_due_us) {
      any_next = true;
      next_due_us = channel_next_due_us;
    }
  }

  if (!any_next) {
    for (size_t i = 0; i < 3; ++i) {
      write_duty(i, 0);
    }
    g_pulse_active = false;
    g_pulse_done = true;
    return;
  }

  const uint64_t target_time_us = g_pulse_start_us + next_due_us;
  const uint64_t current_time_us = esp_timer_get_time();
  const uint64_t delay_us = target_time_us > current_time_us
                                ? target_time_us - current_time_us
                                : 1;
  esp_timer_start_once(g_envelope_timer, delay_us);
}

void create_pulse_timers() {
  esp_timer_create_args_t args = {};
  args.callback = &envelope_timer_cb;
  args.arg = nullptr;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "led_envelope";
  esp_timer_create(&args, &g_envelope_timer);
}

void configure_leds() {
  bool configured = false;

  ledcSetClockSource(LEDC_USE_APB_CLK);

  for (size_t cfg_index = 0; cfg_index < kPwmConfigCount && !configured;
       ++cfg_index) {
    const PwmConfig& candidate = kPwmConfigs[cfg_index];
    configured = true;

    for (size_t i = 0; i < 3; ++i) {
      if (!ledcAttachChannel(kLedPins[i], candidate.frequency_hz,
                             candidate.resolution_bits, kLedChannels[i])) {
        configured = false;
        break;
      }
      ledcWrite(kLedPins[i], 0);
    }

    if (!configured) {
      for (size_t i = 0; i < 3; ++i) {
        ledcDetach(kLedPins[i]);
      }
    } else {
      g_pwm_config = candidate;
      g_peak_duty = (1u << candidate.resolution_bits) - 1u;
    }
  }

  if (!configured) {
    Serial.println("ERROR: LEDC PWM setup failed for all configured frequency/resolution pairs.");
    while (true) {
      delay(1000);
    }
  }

  for (size_t i = 0; i < 3; ++i) {
    ledcWrite(kLedPins[i], 0);
    g_current_duty[i] = 0;
  }
}

void fire_pulse(uint32_t duration_us) {
  esp_timer_stop(g_envelope_timer);

  const uint32_t base_start_us[3] = {
      0,
      kActiveMode.delay_s1_to_s2_us,
      kActiveMode.delay_s1_to_s2_us + kActiveMode.delay_s2_to_s3_us,
  };

  for (size_t i = 0; i < 3; ++i) {
    const uint32_t start_us = add_signed_offset(base_start_us[i],
                                               kActiveMode.sensor_start_offset_us[i]);
    const int32_t exposure_offset_us = kActiveMode.sensor_exposure_offset_us[i] +
                                       static_cast<int32_t>(deterministic_jitter(duration_us, i,
                                                                                kActiveMode));
    const uint32_t sensor_duration_us = add_signed_offset(duration_us, exposure_offset_us);
    g_active_plan[i] = build_envelope(start_us, sensor_duration_us, kActiveMode);
  }

  for (size_t i = 0; i < 3; ++i) {
    write_duty(i, 0);
  }

  g_pulse_start_us = esp_timer_get_time();
  g_pulse_done = false;
  g_pulse_active = true;
  envelope_timer_cb(nullptr);
}

void wait_for_pulse_end() {
  while (!g_pulse_done) {
    delay(1);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("LED Test Rig - Mode: %s (rise: %lu us, fall: %lu us, stagger: %lu/%lu us)\n",
                kActiveMode.name,
                static_cast<unsigned long>(kActiveMode.rise_us),
                static_cast<unsigned long>(kActiveMode.fall_us),
                static_cast<unsigned long>(kActiveMode.delay_s1_to_s2_us),
                static_cast<unsigned long>(kActiveMode.delay_s2_to_s3_us));
  Serial.printf("Measurement compensation: %lu%% threshold model\n",
                static_cast<unsigned long>(kActiveMode.threshold_percent));
  Serial.printf("Pins: LED1=GPIO%d  LED2=GPIO%d  LED3=GPIO%d\n",
                LED1_PIN, LED2_PIN, LED3_PIN);

  WiFi.mode(WIFI_OFF);

  configure_leds();
  create_pulse_timers();

  Serial.printf("PWM: %lu Hz, %u-bit\n",
                static_cast<unsigned long>(g_pwm_config.frequency_hz),
                g_pwm_config.resolution_bits);
  Serial.println("Starting sweep loop...");
}

void loop() {
  for (size_t i = 0; i < kSweepCount; ++i) {
    const SpeedEntry& s = kSweep[i];
    Serial.printf("Simulating %ss (%lu us)...\n", s.label,
                  static_cast<unsigned long>(s.duration_us));
    fire_pulse(s.duration_us);
    wait_for_pulse_end();
    delay(kInterPulsePauseMs);
  }
  delay(kInterSweepExtraPauseMs);
}
