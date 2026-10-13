#include "heart_bpm.h"

#include <math.h>

namespace heart_monitor {

void Biquad::configure(float sample_rate, float frequency, bool highpass) {
    highpass_ = highpass;
    const float angle = 6.283185307307f * frequency / sample_rate;
    const float cosine = cosf(angle);
    const float alpha = sinf(angle) * 0.707106781187f;
    const float inverse = 1.0f / (1.0f + alpha);
    numerator_[0] = (highpass ? 1.0f + cosine : 1.0f - cosine) * 0.5f * inverse;
    numerator_[1] = (highpass ? -2.0f : 2.0f) * numerator_[0];
    numerator_[2] = numerator_[0];
    denominator_[0] = -2.0f * cosine * inverse;
    denominator_[1] = (1.0f - alpha) * inverse;
    prime(0);
}

void Biquad::prime(float input) {
    const float output = highpass_ ? 0.0f : input;
    state_[0] = output - numerator_[0] * input;
    state_[1] = numerator_[2] * input - denominator_[1] * output;
}

float Biquad::push(float input) {
    const float output = numerator_[0] * input + state_[0];
    state_[0] = numerator_[1] * input - denominator_[0] * output + state_[1];
    state_[1] = numerator_[2] * input - denominator_[1] * output;
    return output;
}

void Bandpass::configure(float sample_rate, float low_hz, float high_hz) {
    highpass_.configure(sample_rate, low_hz, true);
    lowpass_.configure(sample_rate, high_hz, false);
    reset();
}

float Bandpass::push(float input) {
    if (!initialized_) {
        highpass_.prime(input);
        lowpass_.prime(0);
        initialized_ = true;
    }
    return lowpass_.push(highpass_.push(input));
}

void PeakDetector::reset(float sample_rate, const BpmConfig& config) {
    previous_ = preceding_ = trough_ = amplitude_ = slope_noise_ = 0;
    minimum_ = config.min_rms_counts * 2;
    alpha_ = 1.0f - expf(-1.0f / sample_rate);
    refractory_ = static_cast<uint32_t>(ceilf(sample_rate * config.refractory_s));
    last_peak_ = 0;
    have_peak_ = false;
}

bool PeakDetector::push(float value, uint64_t sample_index, uint64_t& peak_index) {
    value = -value;
    amplitude_ += alpha_ * (value * value - amplitude_);
    slope_noise_ += alpha_ * (fabsf(value - previous_) - slope_noise_);
    trough_ = fminf(trough_, value);
    const float prominence = previous_ - trough_;
    const float rms = sqrtf(fmaxf(0, amplitude_));
    const float threshold = fmaxf(minimum_, fmaxf(rms * 2, slope_noise_ * 4));
    const bool accepted = sample_index > 0 && previous_ > preceding_ && previous_ >= value &&
        prominence >= threshold && previous_ >= rms &&
        (!have_peak_ || sample_index - 1 - last_peak_ >= refractory_);
    preceding_ = previous_;
    previous_ = value;
    if (accepted) {
        peak_index = last_peak_ = sample_index - 1;
        have_peak_ = true;
        trough_ = previous_;
    }
    return accepted;
}

bool BpmEstimator::reset(float sample_rate_hz, const BpmConfig& config) {
    sample_rate_hz_ = 0;
    result_ = BpmResult();
    const float settings[] = {sample_rate_hz, config.window_s, config.startup_s,
        config.update_s, config.min_bpm, config.max_bpm, config.highpass_hz,
        config.lowpass_hz, config.min_correlation, config.min_rms_counts, config.refractory_s};
    for (float setting : settings) if (!isfinite(setting)) return false;
    if (sample_rate_hz < 20 || sample_rate_hz > 200 || config.window_s < 4 ||
        config.window_s * sample_rate_hz > capacity || config.startup_s < 0 || config.startup_s > 60 ||
        config.update_s < 1.0f / sample_rate_hz || config.update_s > config.window_s ||
        config.min_bpm <= 0 || config.max_bpm <= config.min_bpm || config.highpass_hz <= 0 ||
        config.lowpass_hz <= config.highpass_hz || config.lowpass_hz >= sample_rate_hz / 2 ||
        config.min_correlation <= 0 || config.min_correlation > 1 || config.min_rms_counts <= 0 ||
        config.refractory_s < 0.2f || config.refractory_s > 60.0f / config.max_bpm ||
        config.min_contact_counts >= 262143) return false;
    const float maximum_lag = ceilf(sample_rate_hz * 60 / config.min_bpm);
    const float minimum_lag = floorf(sample_rate_hz * 60 / config.max_bpm);
    if (maximum_lag + 1 >= lag_capacity || minimum_lag < 2 || maximum_lag <= minimum_lag ||
        maximum_lag * 3 > config.window_s * sample_rate_hz) return false;
    config_ = config;
    sample_rate_hz_ = sample_rate_hz;
    window_samples_ = static_cast<size_t>(roundf(config.window_s * sample_rate_hz));
    update_samples_ = static_cast<size_t>(fmaxf(1, roundf(config.update_s * sample_rate_hz)));
    startup_samples_ = static_cast<uint64_t>(roundf(config.startup_s * sample_rate_hz));
    min_lag_ = static_cast<size_t>(minimum_lag);
    max_lag_ = static_cast<size_t>(maximum_lag);
    ir_filter_.configure(sample_rate_hz, config.highpass_hz, config.lowpass_hz);
    red_filter_.configure(sample_rate_hz, config.highpass_hz, config.lowpass_hz);
    total_samples_ = 0;
    beat_write_ = beat_filled_ = 0;
    result_.beat_count = result_.rejected_intervals = 0;
    previous_raw_ = repeated_samples_ = 0;
    invalidate(Settling);
    result_.state = BpmState::Measuring;
    return true;
}

void BpmEstimator::invalidate(uint32_t flag) {
    filled_ = write_index_ = interval_filled_ = interval_write_ = 0;
    clean_samples_ = last_beat_sample_ = last_analysis_ = 0;
    analyzing_ = red_available_ = false;
    previous_candidate_ = 0;
    consistent_windows_ = 0;
    ir_filter_.reset();
    red_filter_.reset();
    peaks_.reset(sample_rate_hz_, config_);
    const uint32_t beats = result_.beat_count, rejected = result_.rejected_intervals;
    result_ = BpmResult();
    result_.beat_count = beats;
    result_.rejected_intervals = rejected;
    result_.state = BpmState::WeakSignal;
    result_.quality_flags = flag;
}

bool BpmEstimator::push(uint32_t ir_sample, uint32_t red_sample) {
    if (sample_rate_hz_ == 0) return false;
    const uint64_t sample_index = total_samples_++;
    result_.beat_detected = false;
    uint32_t invalid = 0;
    if (ir_sample >= 262143) invalid = Clipped;
    else if (ir_sample < config_.min_contact_counts) invalid = NoContact;
    else if (previous_raw_ && fabsf(static_cast<float>(ir_sample) - previous_raw_) >
             fmaxf(5000, previous_raw_ * 0.05f)) invalid = AbruptChange;
    if (previous_raw_ == ir_sample) ++repeated_samples_;
    else repeated_samples_ = 0;
    if (repeated_samples_ >= sample_rate_hz_) invalid = Flatline;
    previous_raw_ = ir_sample;
    if (invalid) {
        invalidate(invalid);
        return true;
    }
    const float filtered = ir_filter_.push(static_cast<float>(ir_sample));
    const bool red_valid = red_sample >= config_.min_contact_counts && red_sample < 262143;
    const float red_filtered = red_valid ? red_filter_.push(static_cast<float>(red_sample)) : 0;
    if (!red_valid) red_filter_.reset();
    ++clean_samples_;
    if (clean_samples_ <= startup_samples_) {
        result_.state = BpmState::Measuring;
        result_.quality_flags = Settling;
        return false;
    }
    result_.waveform = filtered;
    result_.waveform_valid = true;
    uint64_t peak_index = 0;
    if (peaks_.push(filtered, sample_index, peak_index)) record_beat(peak_index);
    samples_[write_index_] = filtered;
    red_samples_[write_index_] = red_filtered;
    write_index_ = (write_index_ + 1) % window_samples_;
    if (filled_ < window_samples_) ++filled_;
    if (filled_ < window_samples_) result_.state = BpmState::Measuring;
    if (!analyzing_ && filled_ == window_samples_ &&
        (!last_analysis_ || total_samples_ - last_analysis_ >= update_samples_)) begin_analysis();
    const bool updated = analyzing_ && advance_analysis();
    result_.pulse = result_.pulse_valid && result_.method == BpmMethod::Peaks && last_beat_sample_ &&
        sample_index - last_beat_sample_ < sample_rate_hz_ * 0.2f;
    return updated;
}

void BpmEstimator::record_beat(uint64_t sample_index) {
    Beat beat;
    beat.sample_index = sample_index;
    beat.timestamp_ms = sample_index * 1000.0 / sample_rate_hz_;
    if (last_beat_sample_) {
        beat.ibi_ms = static_cast<float>((sample_index - last_beat_sample_) * 1000.0 / sample_rate_hz_);
        const uint64_t spacing = sample_index - last_beat_sample_;
        beat.ibi_valid = spacing >= min_lag_ && spacing <= max_lag_;
        if (!beat.ibi_valid) {
            ++result_.rejected_intervals;
            if (spacing < min_lag_) return;
            interval_filled_ = interval_write_ = 0;
        }
    }
    if (beat.ibi_valid) {
        intervals_[interval_write_] = beat.ibi_ms;
        interval_write_ = (interval_write_ + 1) % interval_capacity;
        if (interval_filled_ < interval_capacity) ++interval_filled_;
    }
    last_beat_sample_ = sample_index;
    beats_[beat_write_] = beat;
    beat_write_ = (beat_write_ + 1) % beat_capacity;
    if (beat_filled_ < beat_capacity) ++beat_filled_;
    ++result_.beat_count;
    result_.beat_detected = true;
    result_.beat_timestamp_ms = beat.timestamp_ms;
    result_.ibi_ms = beat.ibi_ms;
}

bool BpmEstimator::beat_at(size_t index, Beat& beat) const {
    if (index >= beat_filled_) return false;
    beat = beats_[(beat_write_ + beat_capacity - beat_filled_ + index) % beat_capacity];
    return true;
}

float BpmEstimator::interval_rate(float& dispersion) const {
    dispersion = 1;
    if (interval_filled_ < 4) return 0;
    float sorted[interval_capacity];
    for (size_t index = 0; index < interval_filled_; ++index) {
        sorted[index] = intervals_[index];
        for (size_t position = index; position > 0 && sorted[position] < sorted[position - 1]; --position) {
            const float temporary = sorted[position];
            sorted[position] = sorted[position - 1];
            sorted[position - 1] = temporary;
        }
    }
    const float median = (sorted[(interval_filled_ - 1) / 2] + sorted[interval_filled_ / 2]) * 0.5f;
    float deviation = 0;
    for (size_t index = 0; index < interval_filled_; ++index) deviation += fabsf(sorted[index] - median);
    dispersion = deviation / (interval_filled_ * median);
    return fmaxf(config_.min_bpm, fminf(config_.max_bpm, 60000 / median));
}

void BpmEstimator::begin_analysis() {
    float mean = 0, red_mean = 0;
    for (size_t index = 0; index < window_samples_; ++index) {
        scratch_[index] = samples_[(write_index_ + index) % window_samples_];
        red_scratch_[index] = red_samples_[(write_index_ + index) % window_samples_];
        mean += scratch_[index];
        red_mean += red_scratch_[index];
    }
    mean /= window_samples_;
    red_mean /= window_samples_;
    float energy = 0, red_energy = 0;
    for (size_t index = 0; index < window_samples_; ++index) {
        scratch_[index] -= mean;
        red_scratch_[index] -= red_mean;
        energy += scratch_[index] * scratch_[index];
        red_energy += red_scratch_[index] * red_scratch_[index];
    }
    analysis_rms_ = sqrtf(energy / window_samples_);
    red_rms_ = sqrtf(red_energy / window_samples_);
    red_available_ = red_rms_ >= config_.min_rms_counts;
    analysis_peak_bpm_ = interval_rate(analysis_dispersion_);
    last_analysis_ = total_samples_;
    analysis_end_ = total_samples_ - 1;
    analysis_lag_ = 2;
    analysis_index_ = analysis_lag_;
    product_ = first_energy_ = second_energy_ = 0;
    red_product_ = red_first_energy_ = red_second_energy_ = 0;
    analyzing_ = true;
}

bool BpmEstimator::advance_analysis() {
    unsigned budget = 2048;
    while (budget-- && analysis_lag_ <= max_lag_ + 1) {
        const float first = scratch_[analysis_index_], second = scratch_[analysis_index_ - analysis_lag_];
        product_ += first * second;
        first_energy_ += first * first;
        second_energy_ += second * second;
        if (red_available_) {
            const float red_first = red_scratch_[analysis_index_];
            const float red_second = red_scratch_[analysis_index_ - analysis_lag_];
            red_product_ += red_first * red_second;
            red_first_energy_ += red_first * red_first;
            red_second_energy_ += red_second * red_second;
        }
        if (++analysis_index_ == window_samples_) {
            const float denominator = sqrtf(first_energy_ * second_energy_);
            correlations_[analysis_lag_] = denominator > 0 ? product_ / denominator : 0;
            const float red_denominator = sqrtf(red_first_energy_ * red_second_energy_);
            red_correlations_[analysis_lag_] = red_denominator > 0 ? red_product_ / red_denominator : 0;
            ++analysis_lag_;
            analysis_index_ = analysis_lag_;
            product_ = first_energy_ = second_energy_ = 0;
            red_product_ = red_first_energy_ = red_second_energy_ = 0;
        }
    }
    if (analysis_lag_ <= max_lag_ + 1) return false;
    analyzing_ = false;
    publish();
    return true;
}

float BpmEstimator::find_period(const float* correlations, float& strength, float& distinctness, float preferred_bpm) const {
    strength = distinctness = 0;
    for (size_t lag = min_lag_; lag <= max_lag_; ++lag)
        if (correlations[lag] > correlations[lag - 1] && correlations[lag] >= correlations[lag + 1])
            strength = fmaxf(strength, correlations[lag]);
    if (strength <= 0) return 0;
    size_t chosen = 0;
    for (size_t lag = min_lag_; lag <= max_lag_; ++lag) {
        if (correlations[lag] > correlations[lag - 1] && correlations[lag] >= correlations[lag + 1] &&
            correlations[lag] >= 0.95f * strength) { chosen = lag; break; }
    }
    bool ambiguous_multiple = false;
    for (size_t lag = min_lag_; lag < chosen; ++lag) {
        const float multiple = static_cast<float>(chosen) / lag;
        if (multiple < 1.8f || fabsf(multiple - roundf(multiple)) > 0.08f ||
            correlations[lag] <= correlations[lag - 1] || correlations[lag] < correlations[lag + 1] ||
            correlations[lag] < 0.08f) continue;
        const float shorter_bpm = 60 * sample_rate_hz_ / lag;
        if (correlations[lag] >= fmaxf(0.45f, correlations[chosen] * 0.65f) &&
            preferred_bpm > 0 && fabsf(shorter_bpm / preferred_bpm - 1) <= 0.15f) {
            chosen = lag;
            ambiguous_multiple = false;
            break;
        }
        ambiguous_multiple = true;
    }
    strength = correlations[chosen];
    float competitor = 0;
    for (size_t lag = min_lag_; lag <= max_lag_; ++lag) {
        const float ratio = static_cast<float>(lag) / chosen;
        if (fabsf(ratio - roundf(ratio)) < 0.1f || fabsf(ratio - 1) < 0.15f) continue;
        if (correlations[lag] > correlations[lag - 1] && correlations[lag] >= correlations[lag + 1])
            competitor = fmaxf(competitor, correlations[lag]);
    }
    distinctness = ambiguous_multiple ? 0 : strength - competitor;
    const float curvature = correlations[chosen - 1] - 2 * strength + correlations[chosen + 1];
    float offset = curvature < -0.000001f ?
        0.5f * (correlations[chosen - 1] - correlations[chosen + 1]) / curvature : 0;
    offset = fmaxf(-0.5f, fminf(0.5f, offset));
    return 60 * sample_rate_hz_ / (chosen + offset);
}

void BpmEstimator::publish() {
    const float preferred_bpm = analysis_peak_bpm_ > 0 && analysis_dispersion_ <= 0.15f ?
        analysis_peak_bpm_ : (result_.pulse_valid ? result_.bpm : 0);
    result_.state = BpmState::WeakSignal;
    result_.method = BpmMethod::None;
    result_.pulse_valid = false;
    result_.bpm = result_.signal_quality = 0;
    result_.quality_flags = 0;
    result_.rms_counts = analysis_rms_;
    result_.candidate_bpm = find_period(correlations_, result_.correlation, result_.correlation_distinctness, preferred_bpm);
    result_.lag_samples = result_.candidate_bpm > 0 ?
        static_cast<size_t>(roundf(60 * sample_rate_hz_ / result_.candidate_bpm)) : 0;
    result_.peak_bpm = analysis_peak_bpm_;
    result_.interval_dispersion = analysis_dispersion_;
    result_.supporting_samples = static_cast<uint32_t>(window_samples_);
    result_.window_end_ms = analysis_end_ * 1000.0 / sample_rate_hz_;
    result_.updated_ms = (total_samples_ - 1) * 1000.0 / sample_rate_hz_;
    float red_strength = 0, red_distinctness = 0;
    result_.red_bpm = red_available_ ? find_period(red_correlations_, red_strength, red_distinctness, preferred_bpm) : 0;
    if (analysis_rms_ < config_.min_rms_counts) result_.quality_flags |= LowAmplitude;
    if (result_.correlation < config_.min_correlation || result_.candidate_bpm == 0)
        result_.quality_flags |= LowPeriodicity;
    if (result_.correlation_distinctness < 0.08f) result_.quality_flags |= AmbiguousPeriod;
    for (size_t lag = 3; lag < min_lag_; ++lag) {
        if (correlations_[lag] > correlations_[lag - 1] && correlations_[lag] >= correlations_[lag + 1] &&
            correlations_[lag] >= fmaxf(0.7f, result_.correlation * 0.95f))
            result_.quality_flags |= OutOfRange;
    }
    const bool recent = last_beat_sample_ && total_samples_ - 1 - last_beat_sample_ <= max_lag_;
    if (!recent) result_.quality_flags |= StaleBeat;
    if (analysis_dispersion_ > 0.15f || analysis_peak_bpm_ == 0) result_.quality_flags |= UnstableIntervals;
    const bool agrees = analysis_peak_bpm_ > 0 && result_.candidate_bpm > 0 &&
        fabsf(analysis_peak_bpm_ / result_.candidate_bpm - 1) <= 0.15f;
    if (analysis_peak_bpm_ > 0 && !agrees) result_.quality_flags |= RateDisagreement;
    if (red_strength >= 0.45f && red_distinctness >= 0.08f && result_.candidate_bpm > 0 &&
        fabsf(result_.red_bpm / result_.candidate_bpm - 1) > 0.15f) result_.quality_flags |= RedDisagreement;
    if (result_.candidate_bpm > 0 && previous_candidate_ > 0 &&
        fabsf(result_.candidate_bpm / previous_candidate_ - 1) < 0.1f) ++consistent_windows_;
    else consistent_windows_ = 1;
    previous_candidate_ = result_.candidate_bpm;
    const uint32_t fatal = LowAmplitude | LowPeriodicity | AmbiguousPeriod | RedDisagreement | OutOfRange;
    if (!(result_.quality_flags & fatal)) {
        if (recent && agrees && analysis_dispersion_ <= 0.15f) {
            result_.method = BpmMethod::Peaks;
            result_.bpm = analysis_peak_bpm_;
        } else if (consistent_windows_ >= 2 && result_.correlation >= fmaxf(0.45f, config_.min_correlation)) {
            result_.method = BpmMethod::Autocorrelation;
            result_.bpm = result_.candidate_bpm;
        }
        if (result_.method != BpmMethod::None) {
            result_.pulse_valid = true;
            result_.state = BpmState::Usable;
            result_.signal_quality = fminf(0.85f, result_.correlation *
                (result_.method == BpmMethod::Peaks ? 0.8f : 0.6f));
        }
    }
}

const char* bpm_state_name(BpmState state) {
    switch (state) {
        case BpmState::Measuring: return "measuring";
        case BpmState::Usable: return "usable";
        case BpmState::WeakSignal: return "weak_signal";
    }
    return "weak_signal";
}

const char* bpm_method_name(BpmMethod method) {
    switch (method) {
        case BpmMethod::Peaks: return "PEAKS";
        case BpmMethod::Autocorrelation: return "AUTOCORRELATION";
        case BpmMethod::None: return "NO_VALID_PULSE";
    }
    return "NO_VALID_PULSE";
}

}