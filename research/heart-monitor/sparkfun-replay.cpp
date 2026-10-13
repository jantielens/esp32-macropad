#include "heartRate.h"
#include <iomanip>
#include <iostream>

int main() {
    uint8_t rates[4] = {};
    unsigned rate_spot = 0;
    unsigned accepted_intervals = 0;
    uint64_t last_beat_ms = 0;
    uint64_t sample_index = 0;
    uint64_t nominal_time_us = 0;
    int32_t ir = 0;
    float beats_per_minute = 0;
    int beat_average = 0;
    std::cout << "sample_index,time_s,beat_detected,instant_bpm,average_bpm,average_updated,accepted_intervals,last_beat_ms,ir_below_example_contact\n";
    std::cout << std::fixed << std::setprecision(6);
    while (std::cin >> sample_index >> nominal_time_us >> ir) {
        const uint64_t now_ms = nominal_time_us / 1000;
        const bool detected = checkForBeat(ir);
        bool updated = false;
        if (detected) {
            const uint64_t delta_ms = now_ms - last_beat_ms;
            last_beat_ms = now_ms;
            beats_per_minute = delta_ms ? 60000.0f / delta_ms : 0;
            if (beats_per_minute < 255 && beats_per_minute > 20) {
                rates[rate_spot++] = static_cast<uint8_t>(beats_per_minute);
                rate_spot %= 4;
                beat_average = 0;
                for (unsigned index = 0; index < 4; ++index) {
                    beat_average += rates[index];
                }
                beat_average /= 4;
                ++accepted_intervals;
                updated = true;
            }
        }
        std::cout << sample_index << ',' << nominal_time_us / 1000000.0 << ','
                  << detected << ',' << beats_per_minute << ',' << beat_average << ','
                  << updated << ',' << accepted_intervals << ',' << last_beat_ms << ','
                  << (ir < 50000) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}