#include "../../../../src/app/device_classes/heart_monitor/heart_bpm.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 5) return 1;
    heart_monitor::BpmConfig config;
    config.highpass_hz = std::strtof(argv[1], nullptr);
    config.lowpass_hz = std::strtof(argv[2], nullptr);
    config.window_s = std::strtof(argv[3], nullptr);
    config.min_bpm = std::strtof(argv[4], nullptr);
    static heart_monitor::BpmEstimator estimator;
    if (!estimator.reset(100, config)) return 2;
    std::cout << "time_s,state,bpm,candidate,correlation,distinctness,dispersion,flags,peak_bpm,red_bpm,rms,updated_ms\n";
    uint32_t ir = 0, red = 0;
    uint64_t sample_index = 0;
    while (std::cin >> ir >> red) {
        estimator.push(ir, red);
        const auto result = estimator.result();
        std::cout << sample_index++ / 100.0 << ',' << heart_monitor::bpm_state_name(result.state)
            << ',' << result.bpm << ',' << result.candidate_bpm << ',' << result.correlation
            << ',' << result.correlation_distinctness << ',' << result.interval_dispersion
            << ',' << result.quality_flags << ',' << result.peak_bpm << ',' << result.red_bpm
            << ',' << result.rms_counts << ',' << result.updated_ms << '\n';
    }
    return std::cin.eof() ? 0 : 3;
}