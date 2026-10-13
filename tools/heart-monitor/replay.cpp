#include "../../src/app/device_classes/heart_monitor/heart_bpm.h"

#include <iomanip>
#include <iostream>
#include <string>
#include <chrono>

int main() {
    float sample_rate_hz = 0.0f;
    heart_monitor::BpmConfig config;
    if (!(std::cin >> sample_rate_hz >> config.window_s >> config.startup_s >> config.update_s
          >> config.min_bpm >> config.max_bpm >> config.min_correlation >> config.min_rms_counts)) {
        std::cerr << "Invalid replay configuration\n";
        return 1;
    }
    static heart_monitor::BpmEstimator estimator;
    if (!estimator.reset(sample_rate_hz, config)) {
        std::cerr << "Unsupported estimator settings (window capacity: 1024 samples)\n";
        return 1;
    }
    std::cout << "time_s,state,candidate_bpm,bpm,correlation,rms_counts,lag_samples,waveform,pulse,beat_count,method,pulse_valid,quality_flags,signal_quality,beat_detected,beat_timestamp_ms,ibi_ms,rejected_intervals,peak_bpm,red_bpm,interval_dispersion,correlation_distinctness,supporting_samples,window_end_ms,updated_ms,processing_us\n" << std::flush;
    std::cout << std::fixed << std::setprecision(6);
    uint64_t sample_index = 0;
    uint64_t input = 0;
    uint64_t red_input = 0;
    while (std::cin >> input >> red_input) {
        if (input > 262143 || red_input > 262143) {
            std::cerr << "Optical sample exceeds the 18-bit range\n";
            return 1;
        }
        const auto started = std::chrono::steady_clock::now();
        estimator.push(static_cast<uint32_t>(input), static_cast<uint32_t>(red_input));
        const double processing_us = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - started).count();
        {
            const heart_monitor::BpmResult result = estimator.result();
            std::cout << static_cast<double>(sample_index) / sample_rate_hz << ','
                << heart_monitor::bpm_state_name(result.state) << ',';
            if (result.candidate_bpm > 0.0f) std::cout << result.candidate_bpm;
            std::cout << ',';
            if (result.state == heart_monitor::BpmState::Usable) std::cout << result.bpm;
            std::cout << ',' << result.correlation << ',' << result.rms_counts << ','
                << result.lag_samples << ',';
            if (result.waveform_valid) std::cout << result.waveform;
            std::cout << ',' << (result.pulse ? 1 : 0) << ',' << result.beat_count << ','
                << heart_monitor::bpm_method_name(result.method) << ',' << result.pulse_valid << ','
                << result.quality_flags << ',' << result.signal_quality << ',' << result.beat_detected << ','
                << result.beat_timestamp_ms << ',' << result.ibi_ms << ',' << result.rejected_intervals << ','
                << result.peak_bpm << ',' << result.red_bpm << ',' << result.interval_dispersion << ','
                << result.correlation_distinctness << ',' << result.supporting_samples << ','
                << result.window_end_ms << ',' << result.updated_ms << ',' << processing_us << '\n' << std::flush;
        }
        ++sample_index;
    }
    if (!std::cin.eof()) {
        std::cerr << "Invalid replay sample\n";
        return 1;
    }
    return 0;
}