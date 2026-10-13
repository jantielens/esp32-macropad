#pragma once

#include <stddef.h>
#include <stdint.h>

namespace heart_monitor {

enum class BpmState { Measuring, Usable, WeakSignal };
enum class BpmMethod { None, Peaks, Autocorrelation };
enum BpmQualityFlag : uint32_t {
    Settling = 1U << 0, NoContact = 1U << 1, Clipped = 1U << 2,
    Flatline = 1U << 3, AbruptChange = 1U << 4, LowAmplitude = 1U << 5,
    LowPeriodicity = 1U << 6, AmbiguousPeriod = 1U << 7,
    RateDisagreement = 1U << 8, RedDisagreement = 1U << 9,
    UnstableIntervals = 1U << 10, StaleBeat = 1U << 11, OutOfRange = 1U << 12
};

struct BpmConfig {
    float window_s = 10.0f;
    float startup_s = 2.0f;
    float update_s = 1.0f;
    float min_bpm = 30.0f;
    float max_bpm = 180.0f;
    float highpass_hz = 0.5f;
    float lowpass_hz = 4.0f;
    float min_correlation = 0.35f;
    float min_rms_counts = 2.0f;
    float refractory_s = 0.3f;
    uint32_t min_contact_counts = 5000;
};

struct Beat {
    uint64_t sample_index = 0;
    double timestamp_ms = 0;
    float ibi_ms = 0;
    bool ibi_valid = false;
};

struct BpmResult {
    BpmState state = BpmState::Measuring;
    BpmMethod method = BpmMethod::None;
    bool pulse_valid = false;
    uint32_t quality_flags = Settling;
    float signal_quality = 0;
    float candidate_bpm = 0.0f;
    float bpm = 0.0f;
    float correlation = 0.0f;
    float rms_counts = 0.0f;
    size_t lag_samples = 0;
    float waveform = 0.0f;
    bool waveform_valid = false;
    bool pulse = false;
    uint32_t beat_count = 0;
    bool beat_detected = false;
    double beat_timestamp_ms = 0;
    float ibi_ms = 0;
    uint32_t rejected_intervals = 0;
    float peak_bpm = 0;
    float red_bpm = 0;
    float interval_dispersion = 0;
    float correlation_distinctness = 0;
    uint32_t supporting_samples = 0;
    double window_end_ms = 0;
    double updated_ms = 0;
};

class Biquad {
public:
    void configure(float sample_rate, float frequency, bool highpass);
    void prime(float input);
    float push(float input);
private:
    float numerator_[3] = {};
    float denominator_[2] = {};
    float state_[2] = {};
    bool highpass_ = false;
};

class Bandpass {
public:
    void configure(float sample_rate, float low_hz, float high_hz);
    float push(float input);
    void reset() { initialized_ = false; }
private:
    Biquad highpass_, lowpass_;
    bool initialized_ = false;
};

class PeakDetector {
public:
    void reset(float sample_rate, const BpmConfig& config);
    bool push(float value, uint64_t sample_index, uint64_t& peak_index);
private:
    float previous_ = 0, preceding_ = 0, trough_ = 0;
    float mean_square_ = 0, slope_noise_ = 0, minimum_ = 0, alpha_ = 0;
    uint64_t last_peak_ = 0;
    uint32_t refractory_ = 0;
    bool have_peak_ = false;
};

class BpmEstimator {
public:
    bool reset(float sample_rate_hz, const BpmConfig& config = BpmConfig());
    bool push(uint32_t ir_sample, uint32_t red_sample = 0);
    BpmResult result() const { return result_; }
    size_t beat_history_count() const { return beat_filled_; }
    bool beat_at(size_t index, Beat& beat) const;

private:
    static constexpr size_t capacity = 1024;
    static constexpr size_t lag_capacity = 512;
    static constexpr size_t beat_capacity = 32;
    static constexpr size_t interval_capacity = 8;
    void invalidate(uint32_t flag);
    void record_beat(uint64_t sample_index);
    void begin_analysis();
    bool advance_analysis();
    void publish();
    float find_period(const float* correlations, float& strength, float& distinctness, float preferred_bpm) const;
    float interval_rate(float& dispersion) const;
    BpmConfig config_;
    BpmResult result_;
    float samples_[capacity] = {};
    float red_samples_[capacity] = {};
    float scratch_[capacity] = {};
    float red_scratch_[capacity] = {};
    float correlations_[lag_capacity] = {};
    float red_correlations_[lag_capacity] = {};
    Beat beats_[beat_capacity];
    float intervals_[interval_capacity] = {};
    float sample_rate_hz_ = 0.0f;
    Bandpass ir_filter_, red_filter_;
    PeakDetector peaks_;
    size_t window_samples_ = 0;
    size_t update_samples_ = 0;
    size_t min_lag_ = 0;
    size_t max_lag_ = 0;
    size_t write_index_ = 0;
    size_t filled_ = 0;
    uint64_t total_samples_ = 0;
    uint64_t startup_samples_ = 0;
    uint64_t last_beat_sample_ = 0;
    uint64_t clean_samples_ = 0;
    uint64_t analysis_end_ = 0;
    uint64_t last_analysis_ = 0;
    uint32_t previous_raw_ = 0, repeated_samples_ = 0;
    size_t beat_write_ = 0, beat_filled_ = 0, interval_write_ = 0, interval_filled_ = 0;
    size_t analysis_lag_ = 0, analysis_index_ = 0;
    float product_ = 0, first_energy_ = 0, second_energy_ = 0;
    float red_product_ = 0, red_first_energy_ = 0, red_second_energy_ = 0;
    float analysis_rms_ = 0, red_rms_ = 0;
    float analysis_peak_bpm_ = 0, analysis_dispersion_ = 0;
    float previous_candidate_ = 0;
    unsigned consistent_windows_ = 0;
    bool analyzing_ = false;
    bool red_available_ = false;
};

const char* bpm_state_name(BpmState state);
const char* bpm_method_name(BpmMethod method);

}