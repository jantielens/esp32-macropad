#include <iostream>
#include <iomanip>
#include <limits>
#ifdef REFERENCE_RF
#include "algorithm_by_RF.h"
#else
#include "spo2_algorithm.h"
#endif

int main() {
    uint32_t infrared[100], red[100];
    std::cout << "spo2,spo2_valid,bpm,bpm_valid,autocorrelation,correlation\n";
    std::cout << std::setprecision(9);
    while (true) {
        for (int index = 0; index < 100; ++index) {
            if (!(std::cin >> infrared[index] >> red[index])) {
                return index == 0 && std::cin.eof() ? 0 : 1;
            }
        }
        int32_t bpm = -999;
        int8_t spo2_valid = 0, bpm_valid = 0;
        float autocorrelation = std::numeric_limits<float>::quiet_NaN();
        float correlation = std::numeric_limits<float>::quiet_NaN();
#ifdef REFERENCE_RF
        float spo2 = -999;
        rf_heart_rate_and_oxygen_saturation(infrared, 100, red, &spo2,
            &spo2_valid, &bpm, &bpm_valid, &autocorrelation, &correlation);
#else
        int32_t spo2 = -999;
        maxim_heart_rate_and_oxygen_saturation(infrared, 100, red, &spo2,
            &spo2_valid, &bpm, &bpm_valid);
#endif
        std::cout << spo2 << ',' << int(spo2_valid) << ',' << bpm << ','
                  << int(bpm_valid) << ',' << autocorrelation << ',' << correlation << '\n';
    }
}