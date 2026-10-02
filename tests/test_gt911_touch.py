#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/drivers/gt911_touch_driver.cpp").read_text()
init_start = source.index("void GT911_TouchDriver::init()")
init_end = source.index("\nTouchReadStatus GT911_TouchDriver::gt911Read", init_start)
write_start = source.index("bool GT911_TouchDriver::writeReg(")
write_end = source.index("\nbool GT911_TouchDriver::readBlock", write_start)

harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#define TOUCH_RST -1
#define TOUCH_I2C_SDA 8
#define TOUCH_I2C_SCL 9
#define TOUCH_I2C_ADDR 0x5d
#define DISPLAY_WIDTH 100
#define DISPLAY_HEIGHT 200
#define GT911_POINT_INFO 0x814e
#define GT911_WIRE_NAME "MockWire"
static unsigned errors = 0;
static unsigned initialized = 0;
static unsigned lock_count = 0;
static int lock_depth = 0;
static void log_info(const char* format) {
    if (std::strstr(format, "Touch initialized")) ++initialized;
}
#define LOGI(tag, format, ...) log_info(format)
#define LOGE(...) (++errors)
#define GT911_I2C_LOCK() (++lock_count, ++lock_depth)
#define GT911_I2C_UNLOCK() (--lock_depth)
static uint8_t highByte(uint16_t value) { return value >> 8; }
static uint8_t lowByte(uint16_t value) { return value & 0xff; }
struct MockWire {
    std::array<uint8_t, 2> results{};
    std::array<uint8_t, 3> bytes{};
    unsigned transmissions = 0;
    unsigned writes = 0;
    void begin(int, int, int) {}
    void beginTransmission(uint8_t address) { assert(address == TOUCH_I2C_ADDR); }
    uint8_t endTransmission() {
        assert(transmissions < results.size());
        return results[transmissions++];
    }
    void write(uint8_t value) {
        assert(lock_depth == 1);
        assert(writes < bytes.size());
        bytes[writes++] = value;
    }
};
static MockWire wire;
#define GT911_WIRE wire
class GT911_TouchDriver {
    uint8_t addr = TOUCH_I2C_ADDR;
public:
    void init();
    bool writeReg(uint16_t reg, uint8_t value);
};
'''
harness += source[init_start:init_end] + source[write_start:write_end]
harness += r'''
int main() {
    for (const auto results : {std::array<uint8_t, 2>{0, 0},
                               std::array<uint8_t, 2>{0, 4},
                               std::array<uint8_t, 2>{4, 0}}) {
        wire = MockWire{};
        wire.results = results;
        errors = initialized = lock_count = 0;
        lock_depth = 0;
        GT911_TouchDriver driver;
        driver.init();
        assert(lock_depth == 0);
        if (results[0]) {
            assert(wire.transmissions == 1 && wire.writes == 0 && lock_count == 0);
        } else {
            assert(wire.transmissions == 2 && wire.writes == 3 && lock_count == 1);
            assert(wire.bytes[0] == 0x81 && wire.bytes[1] == 0x4e && wire.bytes[2] == 0);
        }
        assert(initialized == unsigned(!results[0] && !results[1]));
        assert(errors == unsigned(results[0] || results[1]));
    }
}
'''

with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "gt911.cpp"
    executable = pathlib.Path(directory) / "gt911"
    test_source.write_text(harness)
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(test_source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: GT911 initialization checks pending-data clear and releases the I2C lock")