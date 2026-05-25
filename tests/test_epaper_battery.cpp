// Host-native unit test for epaper_battery_percent — a pure function with
// no Arduino dependencies.

#include "epaper_battery.h"

#include <cassert>
#include <cstdio>

static int failures = 0;

#define CHECK_EQ(expr, expected) do { \
		auto _got = (expr); \
		auto _exp = (expected); \
		if (_got != _exp) { \
				printf("FAIL: %s = %u, expected %u\n", #expr, (unsigned)_got, (unsigned)_exp); \
				++failures; \
		} \
} while (0)

int main() {
		// Endpoints + clamping
		CHECK_EQ(epaper_battery_percent(0),     0);
		CHECK_EQ(epaper_battery_percent(2999),  0);
		CHECK_EQ(epaper_battery_percent(3000),  0);
		CHECK_EQ(epaper_battery_percent(4200),  100);
		CHECK_EQ(epaper_battery_percent(5000),  100);

		// Midpoint (3600 mV = 50%)
		CHECK_EQ(epaper_battery_percent(3600),  50);

		// Linear samples rounded to nearest 5%
		CHECK_EQ(epaper_battery_percent(3300),  25);
		CHECK_EQ(epaper_battery_percent(3900),  75);

		// Output must always be a multiple of 5.
		for (uint32_t mv = 3000; mv <= 4200; ++mv) {
				uint8_t p = epaper_battery_percent((uint16_t)mv);
				if (p % 5u != 0u) {
						printf("FAIL: percent(%u)=%u not multiple of 5\n", (unsigned)mv, (unsigned)p);
						++failures;
						break;
				}
				if (p > 100u) {
						printf("FAIL: percent(%u)=%u > 100\n", (unsigned)mv, (unsigned)p);
						++failures;
						break;
				}
		}

		// Monotonic non-decreasing across the input range.
		uint8_t prev = 0;
		for (uint32_t mv = 3000; mv <= 4200; mv += 10) {
				uint8_t p = epaper_battery_percent((uint16_t)mv);
				if (p < prev) {
						printf("FAIL: monotonicity broken at mv=%u (%u < %u)\n",
									 (unsigned)mv, (unsigned)p, (unsigned)prev);
						++failures;
						break;
				}
				prev = p;
		}

		if (failures == 0) {
				printf("OK: epaper_battery all checks passed\n");
				return 0;
		}
		printf("FAILED: %d check(s)\n", failures);
		return 1;
}
